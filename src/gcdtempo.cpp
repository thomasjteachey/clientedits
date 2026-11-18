// gcdtempo.cpp - Violet Hold "Boon of Tempo": 0.125 sec off every global
// cooldown per stack, to no less than 0.5 sec.
//
// PROBLEM
// -------
// The 3.3.5a client starts its own GCD the moment you cast and refuses the
// next cast until it runs out ("Spell is not ready yet" before a packet is
// sent). So a server-only GCD cut does nothing a player can feel: the client
// is the gate. The usual way to tell the client about a GCD change - a
// SPELLMOD_GLOBAL_COOLDOWN spellmod - cannot be used here:
//   * a FLAT mod also reaches every off-GCD spell in GCD category 133 with a
//     StartRecoveryTime of 0 (Blink, Overpower, Stealth, Holy Shock, Death
//     Coil ... 300+ across the classes on Centurion), where 0 - 100 underflows
//     into a vast GCD and bricks the class - exactly what happened to warriors
//     on 2026-08-24 (memory: spellmod-empty-mask);
//   * either kind is applied BEFORE the client's haste clamp, which floors a
//     caster's GCD at 1.0 sec - the owner's "the client caps at 1.0 second".
//
// So the cut is made here, at the END of the client's GCD function, after the
// haste clamp, only for a GCD that is already above the floor - the same sum
// the server makes in Spell::TriggerGlobalCooldown.
//
// REVERSING (Wow.exe 3.3.5a 12340, verified statically 2026-11-13)
// ----------------------------------------------------------------
//   0x00805D70  the client's GCD function, __cdecl(SpellRec* rec, CGUnit* caster).
//               4 callers (0x53E209, 0x54048F, 0x5D4427, 0x80B583). It is the
//               client twin of TC's Spell::TriggerGlobalCooldown:
//                 ret early if rec+0x234 (StartRecoveryCategory) == 0 and
//                   rec+0x238 (StartRecoveryTime) == 0
//                 ebx = StartRecoveryTime, then spellmod op 0x15 (GLOBAL_COOLDOWN)
//                   via Spell_C_GetSpellModifiers 0x7FD970: (ebx + flat) * pct / 100
//                 if category == 133 && time == 1500 && DmgClass (rec+0x254) not
//                   2/3 && !(Attributes & 0x12): ebx = ebx * UNIT_MOD_CAST_SPEED
//                   (caster+0xD0 -> +0x128), clamped to [1000, 1500]
//                   at 0x805E23..0x805E3E   <- the "1.0 sec cap"
//                 0x805E40  test ebx, ebx / je 0x805EDF   <- THE HOOK
//                 otherwise start a GCD of ebx ms (0x805230 on the record bank
//                   at 0xD3F5AC + 24*[caster is the active player])
//   SpellRec in memory: Spell.dbc field order, field*4, EXCEPT each of the four
//   localized strings (Name/Rank/Description/Tooltip, DBC fields 136-203) is a
//   single pointer, so fields after them sit 64 dwords lower: DBC 205 -> 0x234.
//
//   Aura records: 0x18 bytes each, at caster+0xC50 (inline) or [caster+0xC58]
//   (heap) when [caster+0xDD0] == -1. The count is GetAuraCount 0x4F8850, the
//   record is 0x556E10 __thiscall(unit, index) -> rec* (0 when out of range),
//   rec+0x08 = spell id, rec+0x0E = stack count (byte). +0x0E is what
//   Lua UnitAura (0x6147C0, at 0x614910) pushes as `count`.
//
// THE HOOK
// --------
// The 8 bytes at 0x805E40 (`test ebx, ebx` / `je rel32`) become `jmp stub`
// + 3 nops. The stub saves everything, calls Adjust(caster, gcd) and writes
// the result back into ebx, re-runs the stolen test, and jumps to 0x805EDF
// (zero) or 0x805E48 (non-zero) exactly as the original did.
//
// Adjust only ever SHORTENS a GCD that is above the floor. It never touches 0
// (off-GCD), never raises anything, and reads the aura off the CASTER unit, so
// a pet's GCD (pets do not carry the boon) is untouched.
//
// The server makes the same cut whether or not the player runs this DLL. A
// client without it just keeps its own longer GCD and gets no benefit.

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>
#include <stdarg.h>
#include "gcdtempo.h"

// ------------------------------------------------------------------ client ABI
static const DWORD kHookAt   = 0x00805E40;   // test ebx, ebx ; je 0x805EDF
static const DWORD kResumeNZ = 0x00805E48;   // past the je: start the GCD
static const DWORD kResumeZ  = 0x00805EDF;   // the je target: no GCD

// 0x805E23..0x805E47: the haste clamp to [1000, 1500] followed by the hooked
// test/je. Verifying the clamp too pins this to the right function and build.
static const BYTE kHookSig[] = {
    0x05, 0x18, 0xFC, 0xFF, 0xFF,             // add eax, -1000
    0x3D, 0xF4, 0x01, 0x00, 0x00,             // cmp eax, 500
    0x76, 0x11,                               // jbe 0x805E40
    0xC1, 0xF8, 0x1F,                         // sar eax, 31
    0xF7, 0xD0,                               // not eax
    0x25, 0xF4, 0x01, 0x00, 0x00,             // and eax, 500
    0x05, 0xE8, 0x03, 0x00, 0x00,             // add eax, 1000
    0x8B, 0xD8,                               // mov ebx, eax
    0x85, 0xDB,                               // test ebx, ebx      <- kHookAt
    0x0F, 0x84, 0x97, 0x00, 0x00, 0x00        // je 0x805EDF
};
static const DWORD kHookSigAt = 0x00805E23;
static const size_t kStolen   = 8;            // test (2) + je rel32 (6)

static const DWORD kGetAuraCount = 0x004F8850;  // thiscall (unit) -> int
static const BYTE  kAuraCountSig[] = { 0x8B, 0x81, 0xD0, 0x0D, 0x00, 0x00, 0x83, 0xF8, 0xFF };

static const DWORD kGetAuraRec = 0x00556E10;    // thiscall (unit, index) -> rec* or 0
static const BYTE  kAuraRecSig[] = { 0x55, 0x8B, 0xEC, 0x8B, 0x81, 0xD0, 0x0D, 0x00, 0x00, 0x83, 0xF8, 0xFF };

static const int kAuraRec_SpellId = 0x08;
static const int kAuraRec_Stacks  = 0x0E;
static const int kUnit_AuraTail   = 0xDD4;      // highest unit offset read here (+0xDD0, 4 bytes)

typedef int   (__thiscall *GetAuraCount_t)(void* unit);
typedef BYTE* (__thiscall *GetAuraRec_t)(void* unit, int index);

static GetAuraCount_t pGetAuraCount;
static GetAuraRec_t   pGetAuraRec;

// --------------------------------------------------------------- settings
// Defaults are compiled in so the shipped zip needs no ini. They MUST match the
// server: VioletHoldBoons::ApplyGlobalCooldownReduction, TEMPO_MAX_MS and
// TEMPO_FLOOR_MS (src/server/game/Miscellaneous/VioletHoldBoons.h).
static int  g_enabled    = 1;
static int  g_debug      = 0;
static int  g_auraId     = 94318;   // Boon of Tempo
static int  g_msPerStack = 125;
static int  g_maxMs      = 500;
static int  g_floorMs    = 500;
static char g_dir[MAX_PATH];

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
                          "[gcdtempo pid%lu] %s\r\n", GetCurrentProcessId(), msg);
    if (len < 0) return;

    HANDLE h = CreateFileA(path, FILE_APPEND_DATA,
                           FILE_SHARE_READ | FILE_SHARE_WRITE, NULL,
                           OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (h == INVALID_HANDLE_VALUE) return;
    DWORD w = 0;
    WriteFile(h, line, (DWORD)len, &w, NULL);
    CloseHandle(h);
}

static bool Readable(const void* p, size_t n)
{
    MEMORY_BASIC_INFORMATION mbi;
    if (!VirtualQuery(p, &mbi, sizeof(mbi))) return false;
    if (mbi.State != MEM_COMMIT) return false;
    if (mbi.Protect & (PAGE_NOACCESS | PAGE_GUARD)) return false;
    return (SIZE_T)((const BYTE*)p - (const BYTE*)mbi.BaseAddress) + n <= mbi.RegionSize;
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

// ---------------------------------------------------------------- the cut
static int TempoStacks(void* unit)
{
    if (!unit || !Readable(unit, kUnit_AuraTail))
        return 0;
    int n = pGetAuraCount(unit);
    if (n < 0 || n > 256) n = 64;               // transient garbage; same clamp as playercollide
    for (int i = 0; i < n; ++i) {
        BYTE* rec = pGetAuraRec(unit, i);
        if (!rec || !Readable(rec, 0x18))
            continue;
        if (*(int*)(rec + kAuraRec_SpellId) == g_auraId)
            return rec[kAuraRec_Stacks];
    }
    return 0;
}

// Same sum as VioletHoldBoons::ApplyGlobalCooldownReduction.
static int __cdecl Adjust(void* caster, int gcd)
{
    if (gcd <= g_floorMs)                       // 0 (off-GCD) and floor stay put
        return gcd;
    int stacks = TempoStacks(caster);
    if (stacks <= 0)
        return gcd;
    int cut = stacks * g_msPerStack;
    if (cut > g_maxMs) cut = g_maxMs;
    int out = gcd - cut;
    if (out < g_floorMs) out = g_floorMs;
    if (g_debug)
        Log("gcd %d -> %d (%d stack(s))", gcd, out, stacks);
    return out;
}

// pushad layout after the three args are popped: [esp+16] is the saved ebx.
static __declspec(naked) void Stub()
{
    __asm {
        pushad
        push ebx                    // gcd
        push dword ptr [ebp + 0Ch]  // caster (the function's 2nd argument)
        call Adjust
        add esp, 8
        mov [esp + 16], eax         // popad restores this into ebx
        popad
        test ebx, ebx
        jz  zero
        push 00805E48h              // kResumeNZ
        ret
    zero:
        push 00805EDFh              // kResumeZ
        ret
    }
}

// ------------------------------------------------------------------ settings
void GcdTempo_LoadSettings(const char* dir)
{
    strcpy_s(g_dir, sizeof(g_dir), dir ? dir : "");

    char ini[MAX_PATH];
    _snprintf_s(ini, sizeof(ini), _TRUNCATE, "%sAnimSpeedFix.ini", g_dir);

    g_enabled    = GetPrivateProfileIntA("GcdTempo", "Enabled",    1,     ini);
    g_debug      = GetPrivateProfileIntA("GcdTempo", "Debug",      0,     ini);
    g_auraId     = GetPrivateProfileIntA("GcdTempo", "Aura",       94318, ini);
    g_msPerStack = GetPrivateProfileIntA("GcdTempo", "MsPerStack", 125,   ini);
    g_maxMs      = GetPrivateProfileIntA("GcdTempo", "MaxMs",      500,   ini);
    g_floorMs    = GetPrivateProfileIntA("GcdTempo", "FloorMs",    500,   ini);
    if (g_floorMs < 0) g_floorMs = 0;
}

// ------------------------------------------------------------------- install
void GcdTempo_Install()
{
    if (!g_enabled || g_auraId <= 0 || g_msPerStack <= 0)
        return;

    if (!VerifySig(kHookSigAt,    kHookSig,      sizeof(kHookSig),      "gcd-clamp") ||
        !VerifySig(kGetAuraCount, kAuraCountSig, sizeof(kAuraCountSig), "GetAuraCount") ||
        !VerifySig(kGetAuraRec,   kAuraRecSig,   sizeof(kAuraRecSig),   "GetAuraRec")) {
        g_enabled = 0;
        return;
    }

    pGetAuraCount = (GetAuraCount_t)kGetAuraCount;
    pGetAuraRec   = (GetAuraRec_t)kGetAuraRec;

    BYTE* p = (BYTE*)kHookAt;
    DWORD old = 0;
    if (!VirtualProtect(p, kStolen, PAGE_EXECUTE_READWRITE, &old)) {
        Log("VirtualProtect failed at 0x%08X - disabled", kHookAt);
        g_enabled = 0;
        return;
    }
    memset(p, 0x90, kStolen);                   // jmp rel32 (5) + 3 nops
    p[0] = 0xE9;
    *(DWORD*)(p + 1) = (DWORD)&Stub - (kHookAt + 5);
    VirtualProtect(p, kStolen, old, &old);
    FlushInstructionCache(GetCurrentProcess(), p, kStolen);

    Log("installed at 0x%08X (aura %d, %d ms/stack, max %d, floor %d, debug=%d)",
        kHookAt, g_auraId, g_msPerStack, g_maxMs, g_floorMs, g_debug);
}
