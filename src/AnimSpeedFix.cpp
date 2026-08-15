// AnimSpeedFix - lock stealth movement animations at 1.0x playback, the way
// vanilla did, while leaving everything else on stock 3.3.5a behaviour.
// Target: World of Warcraft 3.3.5a build 12340 (Wow.exe, imagebase 0x400000, x86)
//
// WHAT THIS CHANGES
// -----------------
// WotLK scales movement-animation playback rate by the unit's real speed. In
// CGUnit_C's animation update (0x7385C0):
//
//     0x738896  mov   esi, dword ptr [ebp-0x10]  ; esi = animation id
//     ...
//     0x7388B4  fldz
//     0x7388B6  fcomp dword ptr [ebp-0x164]      ; sequence.movespeed
//     0x7388BC  fnstsw ax
//     0x7388BE  test  ah, 0x44
//     0x7388C1  jnp   0x73898F                   ; movespeed == 0 -> skip scaling
//     0x7388C7  mov   eax, esi                   ; else: the scaling path
//     0x7388EE  call  0x987570                   ; GetCurrentSpeed(unit)
//     0x738902  fdivp                            ; rate = speed / |movespeed|
//     0x738904  fstp  dword ptr [ebp-0x14]
//     ...
//     0x73898F  (carry on; rate stays the 1.0 set by fld1 @ 0x73883C)
//
// We replace the conditional jump at 0x7388C1 with a jump to GateHook, which
// forces the "skip" path for the animations listed in AnimSpeedFix.ini and
// otherwise reproduces the original `jnp` exactly. Mounts, running, swimming
// and everything else keep stock behaviour.
//
// esi holds the animation id here - the client itself relies on that at
// 0x7388C7, and esi is callee-saved across the intervening calls.
//
// Defaults are StealthWalk (119) and StealthRun (223): the only two animations
// that are both stealth and in the engine's movement set (table @ 0x714EAC).
// StealthStand (120) and the FlyStealth* variants are not movement animations,
// so they never scale in the first place.

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "trace.h"
#include "playercollide.h"
#include "goeditor.h"

#define DEFAULT_LOCK_ANIMS   "119,223"     // StealthWalk, StealthRun
#define DEFAULT_ATTACK_ANIMS "16,17,18,19,46,49,85,86,87,88,107,117,482"
// Death, knockdown and stun must always win over a swing.
#define DEFAULT_NEVER_SUPPRESS "0,1,6,7,14,466,467"
#define DEFAULT_PROTECT_MS   20            // ~one frame at 60fps, plus margin
// Measured slot3 -> slot0 re-issue delays: 183, 211, 214 ms. 300 leaves margin
// while staying well under any real second swing.
#define DEFAULT_REPLAY_MS    300
// The re-issue is never early: a second attack anim arriving sooner than this
// after the slot-3 placement is a genuine batched attack (auto + Eviscerate
// dispatched and rendered in the same frame), not the flush.
#define DEFAULT_REPLAY_MIN_MS 120
// A combat packet within this window is treated as the CAUSE of the anim being
// played now - genuine attacks render within a frame or two of their packet.
#define DEFAULT_CAUSE_MS     60

// 0x7388C1: jnp 0x73898F
static const DWORD kGateSite  = 0x007388C1;
static const BYTE  kGateSig[] = { 0x0F, 0x8B, 0xC8, 0x00, 0x00, 0x00 };

#define MAX_ANIM 1024

static BYTE  g_lock[MAX_ANIM];   // 1 = force rate 1.0 for this animation id
static DWORD g_skip = 0;         // original jnp destination: rate stays 1.0
static DWORD g_cont = 0;         // fall-through: the stock scaling path
static int   g_enabled = 1;
static int   g_showErrors = 0;   // off by default: a modal on every launch would
                                 // be miserable if a client update ever moved the
                                 // patch site. Failures go to AnimSpeedFix.log.
static char  g_dir[MAX_PATH];    // folder this DLL was loaded from

// ------------------------------------------------------------------ the hook

__declspec(naked) static void GateHook()
{
    __asm {
        pushfd                              // PF from `test ah, 0x44` must survive
        push eax
        mov  eax, esi                       // animation id
        cmp  eax, MAX_ANIM
        jae  passthru
        cmp  byte ptr g_lock[eax], 0
        jne  locked
    passthru:
        pop  eax
        popfd
        jnp  toskip                         // exactly the original `jnp <skip>`
        push g_cont                         // push/ret: no register, no flags
        ret
    toskip:
        push g_skip
        ret
    locked:
        pop  eax
        popfd
        push g_skip                         // force rate 1.0
        ret
    }
}

// ------------------------------------------------------------------ patching

// The patch site is a hardcoded address; never touch it without proving the
// page is actually committed and readable first, or a smaller/different image
// turns a clean "not applied" into an access violation.
static bool IsReadable(const void* addr, size_t len)
{
    MEMORY_BASIC_INFORMATION mbi;
    if (VirtualQuery(addr, &mbi, sizeof(mbi)) == 0)
        return false;
    if (mbi.State != MEM_COMMIT)
        return false;
    const DWORD readable = PAGE_READONLY | PAGE_READWRITE | PAGE_WRITECOPY |
                           PAGE_EXECUTE_READ | PAGE_EXECUTE_READWRITE |
                           PAGE_EXECUTE_WRITECOPY;
    if (!(mbi.Protect & readable) || (mbi.Protect & PAGE_GUARD))
        return false;
    // the range must not run off the end of this region
    const BYTE* end = (const BYTE*)mbi.BaseAddress + mbi.RegionSize;
    return (const BYTE*)addr + len <= end;
}

static bool InstallAt(DWORD site, char* err, size_t errLen)
{
    BYTE* p = (BYTE*)site;
    if (!IsReadable(p, sizeof(kGateSig))) {
        _snprintf_s(err, errLen, _TRUNCATE,
                    "0x%08X is not readable in this process", site);
        return false;
    }
    if (memcmp(p, kGateSig, sizeof(kGateSig)) != 0) {
        _snprintf_s(err, errLen, _TRUNCATE,
                    "byte signature mismatch at 0x%08X "
                    "(expected %02X %02X %02X %02X %02X %02X, found %02X %02X %02X %02X %02X %02X)",
                    site, kGateSig[0], kGateSig[1], kGateSig[2],
                    kGateSig[3], kGateSig[4], kGateSig[5],
                    p[0], p[1], p[2], p[3], p[4], p[5]);
        return false;
    }

    g_skip = site + 6 + *(DWORD*)(p + 2);   // where the original jcc went
    g_cont = site + 6;                      // the instruction it fell through to

    DWORD old = 0;
    if (!VirtualProtect(p, sizeof(kGateSig), PAGE_EXECUTE_READWRITE, &old)) {
        _snprintf_s(err, errLen, _TRUNCATE, "VirtualProtect failed at 0x%08X", site);
        return false;
    }
    p[0] = 0xE9;                            // jmp rel32 -> GateHook
    *(DWORD*)(p + 1) = (DWORD)GateHook - (site + 5);
    p[5] = 0x90;
    VirtualProtect(p, sizeof(kGateSig), old, &old);
    FlushInstructionCache(GetCurrentProcess(), p, sizeof(kGateSig));
    return true;
}

// Used only by AnimSpeedSelfTest.exe, which cannot reserve memory at the real
// address (the main thread's stack reservation sits there).
extern "C" __declspec(dllexport)
BOOL AnimSpeedFix_InstallAt(DWORD site, char* err, int errLen)
{
    return InstallAt(site, err, (size_t)errLen) ? TRUE : FALSE;
}

static void ReportFailure(const char* err);          // defined below, with LoadSettings
static void MarkIdList(const char* s, BYTE* table);  // ditto

// Generic detour used by the tracer: verify the exact original bytes, then
// overwrite with `jmp rel32` plus nop padding. *retOut receives site+sigLen,
// which is where the stub resumes after replaying the stolen instructions.
static bool PlaceDetour(DWORD site, const BYTE* sig, size_t sigLen, void* target,
                        DWORD* retOut, char* err, size_t errLen)
{
    BYTE* p = (BYTE*)site;
    if (!IsReadable(p, sigLen)) {
        _snprintf_s(err, errLen, _TRUNCATE, "0x%08X is not readable", site);
        return false;
    }
    if (memcmp(p, sig, sigLen) != 0) {
        _snprintf_s(err, errLen, _TRUNCATE,
                    "byte signature mismatch at 0x%08X (found %02X %02X %02X %02X)",
                    site, p[0], p[1], p[2], p[3]);
        return false;
    }
    DWORD old = 0;
    if (!VirtualProtect(p, sigLen, PAGE_EXECUTE_READWRITE, &old)) {
        _snprintf_s(err, errLen, _TRUNCATE, "VirtualProtect failed at 0x%08X", site);
        return false;
    }
    memset(p, 0x90, sigLen);
    p[0] = 0xE9;
    *(DWORD*)(p + 1) = (DWORD)target - (site + 5);
    VirtualProtect(p, sigLen, old, &old);
    FlushInstructionCache(GetCurrentProcess(), p, sigLen);
    *retOut = site + (DWORD)sigLen;
    return true;
}

// Self-test only: arm SwingGuard and hook PlayAnimation at a scratch address,
// so the suppress path's stack arithmetic can be verified without the game.
extern "C" __declspec(dllexport)
BOOL AnimSpeedFix_TestArmGuard(DWORD animSite, int windowMs, char* err, int errLen)
{
    LARGE_INTEGER f;
    QueryPerformanceFrequency(&f);
    g_guardTicks = f.QuadPart * windowMs / 1000;
    g_replayTicks = f.QuadPart * DEFAULT_REPLAY_MS / 1000;
    g_replayMinTicks = f.QuadPart * DEFAULT_REPLAY_MIN_MS / 1000;
    g_causeTicks = f.QuadPart * DEFAULT_CAUSE_MS / 1000;
    MarkIdList(DEFAULT_ATTACK_ANIMS, g_isAttack);
    MarkIdList(DEFAULT_NEVER_SUPPRESS, g_neverSuppress);
    g_guardOn = 1;
    g_traceAnims = 0;
    return PlaceDetour(animSite, kAnimSig, sizeof(kAnimSig), (void*)AnimHook,
                       &g_animRet, err, (size_t)errLen) ? TRUE : FALSE;
}

extern "C" __declspec(dllexport)
unsigned AnimSpeedFix_TestGuardHits(void) { return g_guardHits; }

// Reports what LoadSettings resolved, so the shipped no-ini defaults can be
// verified rather than assumed.
extern "C" __declspec(dllexport)
void AnimSpeedFix_Status(int* gateOn, int* guardOn, int* dropReplay, int* forceSlot0)
{
    if (gateOn)     *gateOn = g_enabled;
    if (guardOn)    *guardOn = g_guardOn;
    if (dropReplay) *dropReplay = g_dropReplay;
    if (forceSlot0) *forceSlot0 = g_forceSlot0;
}

static void StartTrace(void)
{
    char err[256] = { 0 };
    const bool wantTrace = g_tracePath[0] != 0;
    // SwingGuard needs the PlayAnimation hook even with tracing switched off
    const bool wantAnimHook = wantTrace ? (g_traceAnims != 0) : false;

    InitializeCriticalSection(&g_traceLock);

    if (wantTrace) {
        g_traceFile = CreateFileA(g_tracePath, GENERIC_WRITE, FILE_SHARE_READ, NULL,
                                  CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
        if (g_traceFile == INVALID_HANDLE_VALUE) {
            ReportFailure("trace: could not create the log file");
            return;
        }
    }

    LARGE_INTEGER freq, start;
    QueryPerformanceFrequency(&freq);
    QueryPerformanceCounter(&start);
    char hdr[512];
    int n = _snprintf_s(hdr, sizeof(hdr), _TRUNCATE,
                        "# AnimSpeedFix trace v1\r\n"
                        "# qpc_freq=%I64d\r\n"
                        "# qpc_start=%I64d\r\n"
                        "# opcodes=%d anims=%d slots=%d skipstand=%d gate_patch=%d guard=%d\r\n"
                        "# ticks,event,a,b,c   (OP: a=opcode | ANIM: a=animId b=rateBits c=model"
                        " | SLOT: a=seqIndex b=slot c=model | DROP: suppressed anim)\r\n",
                        freq.QuadPart, start.QuadPart, g_traceOpcodes, g_traceAnims,
                        g_traceSlots, g_traceSkipStand, g_enabled, g_guardOn);
    if (wantTrace) {
        DWORD written = 0;
        WriteFile(g_traceFile, hdr, (DWORD)n, &written, NULL);
    }

    // DropSlot3Replay's packet gate needs this hook too, not just tracing
    if (((g_traceOpcodes && wantTrace) || g_dropReplay) &&
        !PlaceDetour(kOpcodeSite, kOpcodeSig, sizeof(kOpcodeSig), (void*)OpcodeHook,
                     &g_opcodeRet, err, sizeof(err))) {
        ReportFailure(err);
        g_traceOpcodes = 0;
        g_dropReplay = 0;              // no packet gate -> do not risk it
    }
    if ((wantAnimHook || g_guardOn) &&
        !PlaceDetour(kAnimSite, kAnimSig, sizeof(kAnimSig), (void*)AnimHook,
                     &g_animRet, err, sizeof(err))) {
        ReportFailure(err);
        g_traceAnims = 0;
        g_guardOn = 0;                 // no hook -> guard cannot run
    }
    // ForceAttackSlot0 and DropSlot3Replay both need this hook, not just tracing
    if (((g_traceSlots && wantTrace) || g_forceSlot0 || g_dropReplay) &&
        !PlaceDetour(kSlotSite, kSlotSig, sizeof(kSlotSig), (void*)SlotHook,
                     &g_slotRet, err, sizeof(err))) {
        ReportFailure(err);
        g_traceSlots = 0;
        g_forceSlot0 = 0;
        g_dropReplay = 0;
    }

    g_traceOn = wantTrace && (g_traceOpcodes || g_traceAnims || g_traceSlots);
}

static void StopTrace(void)
{
    if (g_traceFile == INVALID_HANDLE_VALUE)
        return;
    g_traceOn = 0;
    EnterCriticalSection(&g_traceLock);
    TraceFlushLocked();
    LeaveCriticalSection(&g_traceLock);
    CloseHandle(g_traceFile);
    g_traceFile = INVALID_HANDLE_VALUE;
}

// ------------------------------------------------------------------ settings

// Set table[id] = 1 for every id in a "1, 2 3" style list.
static void MarkIdList(const char* s, BYTE* table)
{
    memset(table, 0, MAX_ANIM_ID);
    while (*s) {
        while (*s && (*s < '0' || *s > '9'))
            ++s;
        if (!*s)
            break;
        int id = 0;
        while (*s >= '0' && *s <= '9')
            id = id * 10 + (*s++ - '0');
        if (id >= 0 && id < MAX_ANIM_ID)
            table[id] = 1;
    }
}

static void ParseIdList(const char* s)
{
    memset(g_lock, 0, sizeof(g_lock));
    while (*s) {
        while (*s && (*s < '0' || *s > '9'))
            ++s;
        if (!*s)
            break;
        int id = 0;
        while (*s >= '0' && *s <= '9')
            id = id * 10 + (*s++ - '0');
        if (id >= 0 && id < MAX_ANIM)
            g_lock[id] = 1;
    }
}

static void ArmGuard(int protectMs, const char* attackList, const char* neverList)
{
    LARGE_INTEGER f;
    QueryPerformanceFrequency(&f);
    g_guardTicks = f.QuadPart * protectMs / 1000;
    g_replayTicks = f.QuadPart * DEFAULT_REPLAY_MS / 1000;
    g_replayMinTicks = f.QuadPart * DEFAULT_REPLAY_MIN_MS / 1000;
    g_causeTicks = f.QuadPart * DEFAULT_CAUSE_MS / 1000;
    MarkIdList(attackList, g_isAttack);
    MarkIdList(neverList, g_neverSuppress);
    g_guardOn = 1;
    // ON, but only safe because a duplicate must match the full re-issue
    // fingerprint in TraceAnim: same animation as the slot-3 placement, inside
    // the measured ReplayMinMs..ReplayMs band, and with no combat packet within
    // CauseMs of the anim itself. A plain time window cannot tell Eviscerate
    // from the client's own re-issue - never weaken this to time alone.
    g_dropReplay = 1;
    g_forceSlot0 = 0;                 // measured to make things worse
}

static void LoadSettings(HMODULE self)
{
    // Compiled defaults. Both fixes are ON here, because the DLL is distributed
    // without an ini - the ini only exists to turn things off or tune them.
    ParseIdList(DEFAULT_LOCK_ANIMS);
    ArmGuard(DEFAULT_PROTECT_MS, DEFAULT_ATTACK_ANIMS, DEFAULT_NEVER_SUPPRESS);

    GetModuleFileNameA(self, g_dir, MAX_PATH);
    char* slash = strrchr(g_dir, '\\');
    if (slash)
        *(slash + 1) = 0;                   // keep the trailing backslash

    char ini[MAX_PATH];
    _snprintf_s(ini, sizeof(ini), _TRUNCATE, "%sAnimSpeedFix.ini", g_dir);
    if (GetFileAttributesA(ini) == INVALID_FILE_ATTRIBUTES)
        return;                             // no ini is the normal shipped case

    g_enabled = GetPrivateProfileIntA("AnimSpeed", "Enabled", 1, ini);
    g_showErrors = GetPrivateProfileIntA("AnimSpeed", "ShowErrors", 0, ini);

    char buf[256];
    GetPrivateProfileStringA("AnimSpeed", "LockAnimations", "", buf, sizeof(buf), ini);
    if (buf[0])
        ParseIdList(buf);

    // [SwingGuard] - stop spell animations eating melee swings
    if (!GetPrivateProfileIntA("SwingGuard", "Enabled", 1, ini)) {
        g_guardOn = 0;
    } else {
        char atk[256], never[256];
        GetPrivateProfileStringA("SwingGuard", "AttackAnims", DEFAULT_ATTACK_ANIMS,
                                 atk, sizeof(atk), ini);
        GetPrivateProfileStringA("SwingGuard", "NeverSuppress", DEFAULT_NEVER_SUPPRESS,
                                 never, sizeof(never), ini);
        ArmGuard(GetPrivateProfileIntA("SwingGuard", "ProtectMs", DEFAULT_PROTECT_MS, ini),
                 atk, never);
        g_forceSlot0 = GetPrivateProfileIntA("SwingGuard", "ForceAttackSlot0", 0, ini);
        g_dropReplay = GetPrivateProfileIntA("SwingGuard", "DropSlot3Replay", 1, ini);
        LARGE_INTEGER rf;
        QueryPerformanceFrequency(&rf);
        g_replayTicks = rf.QuadPart *
            GetPrivateProfileIntA("SwingGuard", "ReplayMs", DEFAULT_REPLAY_MS, ini) / 1000;
        g_replayMinTicks = rf.QuadPart *
            GetPrivateProfileIntA("SwingGuard", "ReplayMinMs", DEFAULT_REPLAY_MIN_MS, ini) / 1000;
        g_causeTicks = rf.QuadPart *
            GetPrivateProfileIntA("SwingGuard", "CauseMs", DEFAULT_CAUSE_MS, ini) / 1000;
    }

    // [Trace] - diagnostics, off by default
    if (GetPrivateProfileIntA("Trace", "Enabled", 0, ini)) {
        g_traceOpcodes = GetPrivateProfileIntA("Trace", "Opcodes", 1, ini);
        g_traceAnims = GetPrivateProfileIntA("Trace", "Animations", 1, ini);
        g_traceSkipStand = GetPrivateProfileIntA("Trace", "SkipStand", 1, ini);
        g_traceSlots = GetPrivateProfileIntA("Trace", "Slots", 1, ini);
        GetPrivateProfileStringA("Trace", "File", "AnimTrace.csv", buf, sizeof(buf), ini);
        if (buf[1] == ':' || buf[0] == '\\')
            strcpy_s(g_tracePath, sizeof(g_tracePath), buf);   // absolute
        else
            _snprintf_s(g_tracePath, sizeof(g_tracePath), _TRUNCATE, "%s%s", g_dir, buf);
    }
}

static void ReportFailure(const char* err)
{
    char log[MAX_PATH];
    _snprintf_s(log, sizeof(log), _TRUNCATE, "%sAnimSpeedFix.log", g_dir);

    HANDLE h = CreateFileA(log, FILE_APPEND_DATA, FILE_SHARE_READ, NULL,
                           OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (h != INVALID_HANDLE_VALUE) {
        SYSTEMTIME t;
        GetLocalTime(&t);
        char line[640];
        int n = _snprintf_s(line, sizeof(line), _TRUNCATE,
                            "[%04d-%02d-%02d %02d:%02d:%02d] not applied: %s\r\n",
                            t.wYear, t.wMonth, t.wDay, t.wHour, t.wMinute, t.wSecond, err);
        DWORD written = 0;
        if (n > 0)
            WriteFile(h, line, (DWORD)n, &written, NULL);
        CloseHandle(h);
    }

    if (g_showErrors) {
        char msg[768];
        _snprintf_s(msg, sizeof(msg), _TRUNCATE,
                    "AnimSpeedFix could not patch this executable and has been disabled.\n\n%s\n\n"
                    "This build only supports Wow.exe 3.3.5a build 12340.", err);
        MessageBoxA(NULL, msg, "AnimSpeedFix", MB_OK | MB_ICONWARNING);
    }
}

static void Install(HMODULE self)
{
    LoadSettings(self);
    // AnimSpeedSelfTest.exe drives InstallAt itself against scratch memory
    if (GetEnvironmentVariableA("ANIMSPEEDFIX_SELFTEST", NULL, 0) != 0)
        return;

    if (g_enabled) {
        char err[256] = { 0 };
        if (!InstallAt(kGateSite, err, sizeof(err)))
            ReportFailure(err);
    }

    // Tracing and SwingGuard are independent of the gameplay patch, so you can
    // run either with [AnimSpeed] Enabled=0.
    if (g_tracePath[0] || g_guardOn)
        StartTrace();

    // Player collision is fully independent of everything above; it reads its own
    // [PlayerCollide] section and installs its own detour (OFF unless configured).
    PlayerCollide_LoadSettings(g_dir);
    PlayerCollide_Install();

    // GameObject editor / cursor-pick probe: its own [GOEditorProbe] section, own
    // detour, OFF unless Enabled=1. Read-only and independent of everything above.
    GOEditor_LoadSettings(g_dir);
    GOEditor_Install();
}

// -------------------------------------------------------- dinput8.dll proxy
//
// Built as a second output (see build.bat). Wow.exe statically imports
// DirectInput8Create from DINPUT8.dll, and dinput8 is not a KnownDLL, so a copy
// sitting next to Wow.exe is loaded in preference to the system one. That gets
// us loaded during process init - before any game code runs - with no injector
// and no launcher changes. Dropping the file in is the whole install; deleting
// it is the whole uninstall.
//
// The real dinput8 is resolved lazily on first call rather than in DllMain, to
// keep LoadLibrary out from under the loader lock.
#ifdef BUILD_DINPUT8_PROXY

typedef HRESULT(WINAPI* DI8Create_t)(void*, DWORD, void*, void**, void*);
static DI8Create_t g_realCreate = NULL;

extern "C" HRESULT WINAPI DirectInput8Create(void* hinst, DWORD version,
                                             void* riid, void** out, void* outer)
{
    if (!g_realCreate) {
        char path[MAX_PATH];
        UINT n = GetSystemDirectoryA(path, MAX_PATH);   // SysWOW64 for a 32-bit process
        if (n == 0 || n >= MAX_PATH - 16)
            return E_FAIL;
        strcpy_s(path + n, MAX_PATH - n, "\\dinput8.dll");
        HMODULE real = LoadLibraryA(path);
        if (!real)
            return E_FAIL;
        g_realCreate = (DI8Create_t)GetProcAddress(real, "DirectInput8Create");
        if (!g_realCreate)
            return E_FAIL;
    }
    return g_realCreate(hinst, version, riid, out, outer);
}

#endif // BUILD_DINPUT8_PROXY

BOOL APIENTRY DllMain(HMODULE hModule, DWORD reason, LPVOID)
{
    if (reason == DLL_PROCESS_ATTACH) {
        DisableThreadLibraryCalls(hModule);
        Install(hModule);
    } else if (reason == DLL_PROCESS_DETACH) {
        StopTrace();
        GOEditor_Shutdown();
    }
    return TRUE;
}
