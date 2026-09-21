// nametag.cpp - stage 1 of the world/tournament/bot marker: a read-only probe
// on the per-unit name tag, plus the toggle the finished feature will use.
//
// REVERSING (Wow.exe 3.3.5a 12340, VAs; full notes in docs/NAME_TAG_RENDER_RE.md)
// -----------------------------------------------------------------------------
//   0x00D380A0  the name-display flags: every UnitName* CVar is one bit in it
//               (Own 1, NPC 2, PlayerGuild 4, PlayerPVPTitle 8). Registered at
//               0x007E6150, all through the callback 0x007E60E0, which is also
//               the shape this file copies to register its own CVar.
//   0x007E5640  THE PER-UNIT NAME TAG. __thiscall. this = a tag record:
//                 +0x10/+0x14  guid of the unit it belongs to
//                 +0x18        flags; bit 1 consumed and cleared at 0x007E56C0
//               It resolves the unit through 0x004D4DB0, asks
//               [unitVtable + 0xD0] what to show (the 0x00D380A0 mask in ecx)
//               and splits the answer into `& 3` and `>> 2` - the name line and
//               the guild line under it - then emits four 0x18-byte vertices
//               into the batch at [0x00C5DF88].
//               Nothing CALLS it: it is reached through the dispatch table at
//               0x00D380B0, so the hook goes on the function itself.
//   0x004D4DB0  ObjectPtr(guidLo, guidHi, typeMask, file, line) -> unit or null
//   0x00767FC0  CVar registration, cdecl, 9 args, caller cleans 0x24
//
// WHAT THE PROBE IS FOR
// ---------------------
// Two things static reading could not settle. First, where the letters are
// drawn: the quad this function emits carries a colour whose only varying
// component is alpha, which reads as a backdrop rather than glyphs, so the text
// is emitted further on or by whatever consumes the batch. Second, where the
// screen position comes from: 0x007E52A0 nearby is only a distance gate
// (camera at [0x00B7436C] + 0x7E20), not a projection, and the position globals
// at 0x00AF46AC are already written by the time this runs.
//
// So the probe logs, for the first few tags of a session: the tag record, the
// guid, the unit and the unit's vtable. The vtable gives [+0xD0] as a concrete
// address to go and read, which is the shortest path to both answers.
//
// It writes nothing into the client and changes no behaviour: the hook runs the
// original prologue and jumps back.

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>
#include <stdarg.h>
#include <string.h>

#include "nametag.h"

static char g_dir[MAX_PATH] = { 0 };
static int  g_enabled = 0;          // [NameTag] Enabled
static int  g_probeLines = 12;      // [NameTag] ProbeLines, 0 = no probe logging
static int  g_defaultOn = 1;        // [NameTag] DefaultOn -> the CVar's default
static int  g_cvarValue = 1;        // what the CVar currently says
static int  g_probed = 0;

static const DWORD kNameTagSite = 0x007E5640;
static const BYTE  kNameTagSig[] = {
    0x55,                               // push ebp
    0x8B, 0xEC,                         // mov ebp, esp
    0x81, 0xEC, 0xEC, 0x04, 0x00, 0x00, // sub esp, 0x4EC
    0x56,                               // push esi
    0x8B, 0xF1                          // mov esi, ecx
};
// The whole prologue above is relocation-free, so all nine bytes before the
// `push esi` can be stolen for the jmp and replayed in the trampoline.
static const size_t kStolen = 9;

// Both read off the file with peexplore (disasm), not remembered: the first
// build guessed ObjectPtr's prologue and the fail-safe refused to install.
static const DWORD kObjectPtr    = 0x004D4DB0;
static const BYTE  kObjectPtrSig[] = {
    0x55,                                   // push ebp
    0x8B, 0xEC,                             // mov ebp, esp
    0x64, 0x8B, 0x0D, 0x2C, 0x00, 0x00, 0x00 // mov ecx, fs:[0x2C]  (the objmgr is per-thread)
};
static const DWORD kCVarRegister = 0x00767FC0;
static const BYTE  kCVarRegSig[] = {
    0x55,                                   // push ebp
    0x8B, 0xEC,                             // mov ebp, esp
    0x83, 0xEC, 0x08,                       // sub esp, 8
    0x53                                    // push ebx
};
static const DWORD kNameFlags    = 0x00D380A0;

// STAGE 2 - the marker line. Inside 0x007E5640's dirty block:
//
//     0x007E5754  call edx              ; [unitVtable+0xCC](mask, buf, 0x400)
//     0x007E5756  test eax, eax         ; eax = number of lines written
//     0x007E5758  mov [ebp-0x10], eax
//     0x007E575B  fild dword [ebp-0x10] ; -> the tag's height
//
// The five bytes at 0x007E5756 are relocation-free. The hook prepends a line
// to the buffer (ebp-0x4EC) and returns the new count, then replays them. The
// jge at 0x007E575E reads the flags of that replayed `test`; fild leaves the
// flags alone.
static const DWORD kMarkerSite   = 0x007E5756;
static const BYTE  kMarkerSig[]  = { 0x85, 0xC0, 0x89, 0x45, 0xF0, 0xDB, 0x45, 0xF0 };
static const size_t kMarkerStolen = 5;
static const DWORD kMarkerResume = 0x007E575B;

// The unit's auras, read exactly the way the client's own name builder
// (0x0072D4F0) and its count accessor (0x004F8850) read them:
//   count = [unit+0xDD0]; if that is -1, count = [unit+0xC54] and the entries
//   live at [unit+0xC58], otherwise inline from unit+0xC50. 0x18 bytes each,
//   spell id at +0x08.
static const DWORD kAuraInlineCount = 0xDD0;
static const DWORD kAuraHeapCount   = 0xC54;
static const DWORD kAuraHeapPtr     = 0xC58;
static const DWORD kAuraInline      = 0xC50;
static const DWORD kAuraStride      = 0x18;
static const DWORD kAuraSpell       = 0x08;

enum Marker { MARK_NONE = 0, MARK_WORLD, MARK_TOURNAMENT, MARK_BOT };

static int  g_testMarker = MARK_NONE;   // [NameTag] TestMarker: every player gets it
static DWORD g_auraFor[4] = { 0 };      // [NameTag] WorldAura / TournamentAura / BotAura
static int  g_markerHooked = 0;
static int  g_markerLogged = 0;
static DWORD g_markerResume = kMarkerResume;

// The CVar callback cannot be a pointer into this DLL. The client checks every
// function pointer it is about to call against Wow.exe's own .text and treats
// anything else as fatal error #134 "Invalid function pointer" - the same check
// gluebridge.cpp meets for Lua (0x0086B5A0). The first build registered
// CVarChanged directly; the game called it on registration and died on the
// spot with its address in the message.
//
// So the callback that gets registered is a 5-byte jmp written into int3
// padding inside .text, which then jumps on to the DLL. The padding used is the
// run right after the client's OWN UnitName* callback (0x007E60E0 ... ret at
// 0x007E6140), fifteen bytes of CC that nothing else in this DLL claims.
static const DWORD kCVarThunkSite  = 0x007E6141;
static const BYTE  kCVarThunkSig[] = { 0xCC, 0xCC, 0xCC, 0xCC, 0xCC };
static int         g_thunkReady = 0;

typedef void* (__cdecl* ObjectPtr_t)(DWORD guidLo, DWORD guidHi, int typeMask,
                                     const char* file, int line);

// ------------------------------------------------------------------- logging
static void Log(const char* fmt, ...)
{
    char path[MAX_PATH];
    _snprintf_s(path, sizeof(path), _TRUNCATE, "%sAnimSpeedFix.log", g_dir);

    char msg[512];
    va_list ap;
    va_start(ap, fmt);
    int n = _vsnprintf_s(msg, sizeof(msg) - 2, _TRUNCATE, fmt, ap);
    va_end(ap);
    if (n < 0) n = (int)strlen(msg);

    char line[600];
    int len = _snprintf_s(line, sizeof(line), _TRUNCATE,
                          "[nametag pid%lu] %s\r\n", GetCurrentProcessId(), msg);
    if (len < 0) return;

    HANDLE h = CreateFileA(path, FILE_APPEND_DATA,
                           FILE_SHARE_READ | FILE_SHARE_WRITE, NULL,
                           OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (h == INVALID_HANDLE_VALUE) return;
    DWORD w = 0;
    WriteFile(h, line, (DWORD)len, &w, NULL);
    CloseHandle(h);
}

static bool Readable(void* p, size_t n)
{
    MEMORY_BASIC_INFORMATION mbi;
    if (!VirtualQuery(p, &mbi, sizeof(mbi))) return false;
    if (mbi.State != MEM_COMMIT) return false;
    if (mbi.Protect & (PAGE_NOACCESS | PAGE_GUARD)) return false;
    return (SIZE_T)((BYTE*)p - (BYTE*)mbi.BaseAddress) + n <= mbi.RegionSize;
}

static bool VerifySig(DWORD addr, const BYTE* sig, size_t n, const char* name)
{
    if (!Readable((void*)addr, n)) {
        Log("0x%08X (%s) not readable - disabled", addr, name);
        return false;
    }
    if (memcmp((void*)addr, sig, n) != 0) {
        Log("0x%08X (%s) signature mismatch - disabled (wrong Wow.exe build?)", addr, name);
        return false;
    }
    return true;
}

// ---------------------------------------------------------------- the toggle
//
// The client's own UnitName* CVars are registered at 0x007E6150 as:
//
//     push 0 / push <userArg> / push 0 / push 4 / push <callback>
//     push <defaultValue> / push 0x10 / push 0 / push <name>
//     call 0x00767FC0                       (cdecl, caller cleans 0x24)
//
// and their shared callback reads the new value at [ebp+0x10] and its own
// userArg at [ebp+0x14]. This registers `centurionNameTags` the same way, with
// a callback that keeps the value here. Until the first run confirms the
// argument order, the ini default is what the marker actually obeys - hence
// NameTag_Enabled() preferring the CVar only once the callback has spoken.
static int g_cvarSpoke = 0;

// Returns true like the client's own callback (0x007E613D: mov al, 1 / ret),
// which reads as "accept the new value" - a void callback would leave whatever
// happened to be in al, and the CVar could randomly refuse to change.
static bool __cdecl CVarChanged(void* cvar, void* a, const char* newValue, int userArg)
{
    (void)cvar; (void)a; (void)userArg;
    if (newValue && *newValue)
        g_cvarValue = (newValue[0] != '0');
    g_cvarSpoke = 1;
    Log("centurionNameTags -> %d", g_cvarValue);
    return true;
}

typedef void* (__cdecl* CVarRegister_t)(const char* name, int unknown, int flags,
                                        const char* defaultValue, void* callback,
                                        int category, int unk0, int userArg, int unk1);

static void RegisterCVar()
{
    if (!VerifySig(kCVarRegister, kCVarRegSig, sizeof(kCVarRegSig), "CVar::Register"))
        return;

    // Without the in-.text thunk there is no callback the client will accept,
    // and registering with a DLL pointer is a guaranteed fatal error.
    if (!g_thunkReady) {
        Log("no callback thunk - centurionNameTags not registered");
        return;
    }

    // Every argument exactly as the client passes its own UnitName* CVars
    // (0x007E6150), confirmed against the file rather than guessed: arg4 is the
    // default value - 0x009E14A0 is "0" and 0x009E1464 is "1", the real 3.3.5
    // defaults for Own/NPC and Guild/PVPTitle. arg2's meaning is unknown and the
    // client always passes 0 there, so this does too.
    CVarRegister_t reg = (CVarRegister_t)kCVarRegister;
    void* cvar = reg("centurionNameTags",
                     0,
                     0x10,
                     g_defaultOn ? "1" : "0",
                     (void*)kCVarThunkSite,     // -> CVarChanged, see kCVarThunkSite
                     4, 0, 1, 0);
    Log("registered centurionNameTags (cvar=%p, default %d)", cvar, g_defaultOn);
}

bool NameTag_Enabled()
{
    if (!g_enabled) return false;
    return g_cvarSpoke ? (g_cvarValue != 0) : (g_defaultOn != 0);
}

// ----------------------------------------------------------------- the probe
//
// Called with the tag record before the original body runs. Read-only, apart
// from registering the CVar the first time through.
//
// The CVar is registered HERE and not in NameTag_Install, and that is not a
// style choice. Install runs from DllMain, during process start and under the
// loader lock, before Wow.exe has initialised anything - its CVar manager and
// its allocator included. Every other module only patches bytes there; calling
// into the game from there failed the whole process with 0xC0000142
// (STATUS_DLL_INIT_FAILED) before a window ever opened. By the time a name tag
// is drawn the game is fully up and this is its own main thread.
static int g_cvarRegistered = 0;

extern "C" void __cdecl NameTagProbe(void* tag)
{
    if (!g_cvarRegistered) {
        g_cvarRegistered = 1;
        RegisterCVar();
    }

    if (g_probed >= g_probeLines) return;
    if (!Readable(tag, 0x20)) return;

    DWORD guidLo = *(DWORD*)((BYTE*)tag + 0x10);
    DWORD guidHi = *(DWORD*)((BYTE*)tag + 0x14);
    DWORD flags  = *(DWORD*)((BYTE*)tag + 0x18);
    if (!guidLo && !guidHi) return;

    void* unit = NULL;
    void* vtable = NULL;
    void* nameFn = NULL;
    ObjectPtr_t objectPtr = (ObjectPtr_t)kObjectPtr;
    unit = objectPtr(guidLo, guidHi, 1, "nametag.cpp", 0);
    if (unit && Readable(unit, 4)) {
        vtable = *(void**)unit;
        if (vtable && Readable(vtable, 0xD4))
            nameFn = *(void**)((BYTE*)vtable + 0xD0);
    }

    // One line per unit: the first run spent all twelve on the same player.
    static DWORD seen[64];
    for (int i = 0; i < g_probed && i < 64; ++i)
        if (seen[i] == guidLo) return;
    if (g_probed < 64) seen[g_probed] = guidLo;

    // The first few aura spell ids, read the way the marker reads them, so the
    // log shows whether that reader lines up with the buffs actually on the unit.
    char auras[160] = { 0 };
    if (unit) {
        BYTE* u = (BYTE*)unit;
        if (Readable(u + kAuraInline, kAuraInlineCount - kAuraInline + 4)) {
            DWORD count = *(DWORD*)(u + kAuraInlineCount);
            BYTE* entries = u + kAuraInline;
            if (count == 0xFFFFFFFF) {
                count = *(DWORD*)(u + kAuraHeapCount);
                entries = *(BYTE**)(u + kAuraHeapPtr);
            }
            size_t at = _snprintf_s(auras, sizeof(auras), _TRUNCATE, "%u:", count);
            for (DWORD i = 0; i < count && i < 12 && entries && Readable(entries + i * kAuraStride, kAuraStride); ++i) {
                int n = _snprintf_s(auras + at, sizeof(auras) - at, _TRUNCATE, " %u",
                                    *(DWORD*)(entries + i * kAuraStride + kAuraSpell));
                if (n < 0) break;
                at += n;
            }
        }
    }

    ++g_probed;
    Log("tag=%p guid=%08X%08X flags=0x%X unit=%p vtable=%p auras=[%s] mask=0x%X",
        tag, guidHi, guidLo, flags, unit, vtable, auras, *(DWORD*)kNameFlags);
    (void)nameFn;

    if (g_probed == g_probeLines)
        Log("probe budget spent");
}

// The hook: log, then run the stolen prologue and jump back past it.
static DWORD g_resume = kNameTagSite + kStolen;

__declspec(naked) static void NameTagHook()
{
    __asm {
        pushad
        pushfd
        push ecx                    // this (the tag record)
        call NameTagProbe
        add  esp, 4
        popfd
        popad

        push ebp                    // the stolen prologue, replayed
        mov  ebp, esp
        sub  esp, 0x4EC
        jmp  [g_resume]
    }
}

// ---------------------------------------------------------------- the marker

// Whether the unit carries the aura spellId.
static bool AnyAura(void* unit, DWORD spellId)
{
    BYTE* u = (BYTE*)unit;
    if (!spellId || !Readable(u + kAuraInline, kAuraInlineCount - kAuraInline + 4))
        return false;

    DWORD count = *(DWORD*)(u + kAuraInlineCount);
    BYTE* entries = u + kAuraInline;
    if (count == 0xFFFFFFFF) {
        count = *(DWORD*)(u + kAuraHeapCount);
        entries = *(BYTE**)(u + kAuraHeapPtr);
    }
    if (!entries || count > 255 || !Readable(entries, count * kAuraStride))
        return false;

    for (DWORD i = 0; i < count; ++i)
        if (*(DWORD*)(entries + i * kAuraStride + kAuraSpell) == spellId)
            return true;
    return false;
}

static int MarkerFor(void* tag, void* unit)
{
    // Players only: a player guid's high word is zero (HighGuid::Player).
    DWORD guidHi = *(DWORD*)((BYTE*)tag + 0x14);
    if (guidHi & 0xFFFF0000)
        return MARK_NONE;

    for (int m = MARK_WORLD; m <= MARK_BOT; ++m)
        if (AnyAura(unit, g_auraFor[m]))
            return m;
    return g_testMarker;
}

// |c codes: whether the font string honours them is exactly what the first
// build of this settles. If it does not, the codes print as text and the
// marker has to take the name's own colour instead.
static const char* MarkerText(int m)
{
    switch (m) {
    case MARK_WORLD:      return "|cff73bfffWorld|r\n";
    case MARK_TOURNAMENT: return "|cffff9933Tournament|r\n";
    case MARK_BOT:        return "|cffb266ffBot|r\n";
    }
    return NULL;
}

extern "C" int __cdecl NameTagMarker(void* tag, void* unit, char* buf, int lines)
{
    if (lines <= 0 || !NameTag_Enabled() || !buf || !unit || !Readable(tag, 0x20))
        return lines;

    const char* text = MarkerText(MarkerFor(tag, unit));
    if (!text)
        return lines;

    size_t have = strnlen(buf, 0x400);
    size_t add  = strlen(text);
    if (have + add >= 0x400)
        return lines;

    memmove(buf + add, buf, have + 1);
    memcpy(buf, text, add);

    if (g_markerLogged < 4) {
        ++g_markerLogged;
        Log("marker: lines %d -> %d, text \"%s\"", lines, lines + 1, buf);
    }
    return lines + 1;
}

__declspec(naked) static void MarkerHook()
{
    __asm {
        pushad
        push eax                    // lines written
        lea  edx, [ebp - 0x4EC]
        push edx                    // the text buffer
        push ebx                    // the unit
        push esi                    // the tag
        call NameTagMarker
        add  esp, 16
        mov  [esp + 0x1C], eax      // pushad's eax slot
        popad

        test eax, eax               // the stolen bytes, replayed
        mov  [ebp - 0x10], eax
        jmp  [g_markerResume]
    }
}

// ------------------------------------------------------------------ install
// A jmp at `at`; bytes after it up to `span` become nops, so a debugger reading
// a hooked site does not show half an instruction. span = 5 writes the jmp alone.
static bool WriteJump(DWORD at, void* to, size_t span)
{
    DWORD old = 0;
    if (!VirtualProtect((void*)at, span, PAGE_EXECUTE_READWRITE, &old)) return false;
    BYTE* p = (BYTE*)at;
    p[0] = 0xE9;
    *(DWORD*)(p + 1) = (DWORD)to - (at + 5);
    for (size_t i = 5; i < span; ++i) p[i] = 0x90;
    DWORD ignored = 0;
    VirtualProtect((void*)at, span, old, &ignored);
    FlushInstructionCache(GetCurrentProcess(), (void*)at, span);
    return true;
}

void NameTag_LoadSettings(const char* dir)
{
    if (dir) {
        strncpy_s(g_dir, sizeof(g_dir), dir, _TRUNCATE);
    }

    char ini[MAX_PATH];
    _snprintf_s(ini, sizeof(ini), _TRUNCATE, "%sAnimSpeedFix.ini", g_dir);

    g_enabled    = GetPrivateProfileIntA("NameTag", "Enabled", 0, ini);
    g_probeLines = GetPrivateProfileIntA("NameTag", "ProbeLines", 12, ini);
    g_defaultOn  = GetPrivateProfileIntA("NameTag", "DefaultOn", 1, ini);
    g_cvarValue  = g_defaultOn;

    char mark[32] = { 0 };
    GetPrivateProfileStringA("NameTag", "TestMarker", "", mark, sizeof(mark), ini);
    g_testMarker = !_stricmp(mark, "World")      ? MARK_WORLD
                 : !_stricmp(mark, "Tournament") ? MARK_TOURNAMENT
                 : !_stricmp(mark, "Bot")        ? MARK_BOT
                 : MARK_NONE;

    g_auraFor[MARK_WORLD]      = GetPrivateProfileIntA("NameTag", "WorldAura", 0, ini);
    g_auraFor[MARK_TOURNAMENT] = GetPrivateProfileIntA("NameTag", "TournamentAura", 0, ini);
    g_auraFor[MARK_BOT]        = GetPrivateProfileIntA("NameTag", "BotAura", 0, ini);
}

void NameTag_Install()
{
    if (!g_enabled) return;

    if (!VerifySig(kNameTagSite, kNameTagSig, sizeof(kNameTagSig), "name tag"))
        return;
    if (!VerifySig(kObjectPtr, kObjectPtrSig, sizeof(kObjectPtrSig), "ObjectPtr"))
        return;

    // Nothing here may call into the game - see NameTagProbe. Only bytes are
    // checked and written; the CVar is registered on the first name tag.

    // The CVar's callback, in .text where the client will accept it. Without it
    // the probe still runs; only the toggle is missing.
    if (VerifySig(kCVarThunkSite, kCVarThunkSig, sizeof(kCVarThunkSig), "CVar callback padding") &&
        WriteJump(kCVarThunkSite, (void*)&CVarChanged, sizeof(kCVarThunkSig)))
        g_thunkReady = 1;

    if (!WriteJump(kNameTagSite, (void*)&NameTagHook, kStolen)) {
        Log("could not write the name tag hook - disabled");
        return;
    }
    Log("probe installed at 0x%08X (%d lines)", kNameTagSite, g_probeLines);

    // The marker is its own hook: without it the probe and the toggle still
    // work, the tag is just drawn the way the client always drew it.
    if (VerifySig(kMarkerSite, kMarkerSig, sizeof(kMarkerSig), "name tag line count") &&
        WriteJump(kMarkerSite, (void*)&MarkerHook, kMarkerStolen)) {
        g_markerHooked = 1;
        Log("marker installed at 0x%08X (test marker %d, auras %u/%u/%u)", kMarkerSite,
            g_testMarker, g_auraFor[MARK_WORLD], g_auraFor[MARK_TOURNAMENT], g_auraFor[MARK_BOT]);
    }
}
