// rulerelax.cpp - aura-gated relaxation of client-side spell usability rules.
//
// PROBLEM
// -------
// A set bonus like "your abilities work while unarmed" cannot be done on the
// server alone: the client refuses the cast before a packet is ever sent, from
// its own Spell.dbc. The obvious data fix - clear EquippedItemClass on the
// spell - works, but Spell.dbc is global. It would drop the "Requires Melee
// Weapon" tooltip line and the greyed button for EVERY player, not just the
// ones wearing the set. Auras are per-player; DBC is not. So the relaxation has
// to happen at runtime, in the client, gated on an aura.
//
// This module leaves the DBC completely alone. Non-wearers see exactly what
// they see today: red button, tooltip line intact. A wearer's button lights up.
//
// The server still enforces the real rule (Spell::CheckItems), so this DLL only
// removes a client-side refusal - it never grants anything. Deleting the DLL
// makes your own buttons stop working; it cannot be used to gain an ability.
//
// REVERSING (Wow.exe 3.3.5a 12340, all verified statically - see below)
// --------------------------------------------------------------------
//   Lua IsUsableSpell   registration table entry at .data 0x00ACCD78
//                       -> Script_IsUsableSpell = 0x00541680
//   Script_IsUsableSpell copies the Spell.dbc row into a 0x2B4-byte stack local
//                       (SpellDB fetch: mov ecx, 0xAD49D0 / call 0x004CFD20),
//                       resolves the unit via GetActivePlayerGuid 0x004D3790 +
//                       ObjectPtr 0x004D4DB0, then calls:
//   0x0080B5D0          THE USABILITY PREDICATE - __cdecl, 5 args, caller
//                       cleans 0x14. Returns bool in al. 8 callers.
//                         arg1 unit, arg2 SpellRec copy, arg3 int* out, arg4,5 = 0
//   0x008017E0          the power/mana check (drives IsUsableSpell's 2nd return)
//
// SpellRec layout is the raw Spell.dbc field order, field_index*4. Verified
// against tools\WoW Spell Editor LPLUS\Bindings_335_wotlk\Spell.txt and against
// live code in 0x80B5D0 (which reads [rec+0x10] = field 4 = Attributes, and
// [rec+0x11C] = field 71 = Effect1, comparing it to effect ids 47 and 78):
//       +0x110  EquippedItemClass              (field 68, -1 = no requirement)
//       +0x114  EquippedItemSubClassMask       (field 69)
//       +0x118  EquippedItemInventoryTypeMask  (field 70)
//
// WHY WE DO NOT PATCH A NAMED "WEAPON CHECK"
// ------------------------------------------
// The equipped-item test could not be pinned to its own function by static
// scanning - no [rec+0x110] access exists anywhere in the spell code region.
// (Two candidates elsewhere were checked and rejected: 0x008D5590 tests
// [esi+0x110] == -1 but returns SPELL_FAILED_ITEM_ALREADY_ENCHANTED = 42, so
// it is enchanting code where +0x110 means something else.) Rather than guess
// at an address, this hook uses the client's OWN answer differentially:
//
//   ask the predicate normally                     -> unusable?
//   ask it again with EquippedItemClass set to -1  -> usable?
//   if that flipped the answer, the weapon requirement WAS the deciding
//   reason, and only then do we report usable.
//
// That is exact. It cannot mistake a cooldown, a stun or an out-of-range for a
// weapon problem, because anything else keeps the second answer unusable too.
// It also survives being wrong about which internal function does the check.
//
// OPEN MEASUREMENT (this is the one thing static analysis could not settle)
// ------------------------------------------------------------------------
// Whether this predicate is the ONLY gate. If the client also re-checks on the
// send path, the button will light up but the cast still will not go out. Run
// with Debug=1, unequip your weapon, and press the ability:
//   * log shows "relaxed" AND the spell casts -> this hook is the whole gate.
//   * log shows "relaxed" but nothing happens -> there is a second gate on the
//     send path; hook that too. Start from the CastSpell path rather than the
//     usability path (assertion string ".\\SpellCast.cpp" is at 0x00AA9818,
//     pushed at 0x009AB847).
// Measure it before shipping - do not assume.

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>
#include <stdarg.h>
#include "rulerelax.h"

// ------------------------------------------------------------------ client ABI
//
// THE equipped-item check: __cdecl(unit, SpellRec*, int, bool showError, int)
// -> bool in al, caller cleans 0x14. Two callers, and that is the whole reason
// this is the right hook:
//   0x0080B7DB - inside the usability predicate 0x0080B5D0 (showError = 0)
//   0x0080D46F - the CAST path (showError = 1)
// Hooking the usability predicate alone un-greys the button and then the cast
// path calls this directly and refuses anyway - measured in game 2026-08-17,
// "Must have a Melee Weapon equipped in the Main Hand" with the button lit.
// Hooking here covers both, because the predicate delegates its item check to
// this function rather than duplicating it.
//
// Internals worth knowing: it early-outs to PASS when EquippedItemClass < 0
// (0x008093FB) or EquippedItemSubClassMask == 0, picks a slot mask from
// AttributesEx3 (+0x1C) bit 0x400 = MAIN_HAND / 0x1000000 = REQ_OFFHAND, and on
// failure emits cast error 30 (SPELL_FAILED_EQUIPPED_ITEM_CLASS_MAINHAND) at
// 0x00809468. The server never returns error 30, which is how we knew the
// refusal was client-local and the packet had never been sent.
//
// Detour steals the first 6 bytes = three whole instructions
// (push ebp / mov ebp,esp / sub esp,0x10), so the trampoline resumes at +6.
static const DWORD kWeaponCheck       = 0x008093D0;
static const BYTE  kWeaponCheckSig[]  = { 0x55, 0x8B, 0xEC, 0x83, 0xEC, 0x10 };
static const DWORD kWeaponCheckResume = 0x008093D6;

// Aura array accessors - same pair playercollide.cpp already uses and ships.
static const DWORD kGetAuraCount = 0x004F8850;  // thiscall (unit) -> int
static const BYTE  kAuraCountSig[] = { 0x8B, 0x81, 0xD0, 0x0D, 0x00, 0x00, 0x83, 0xF8, 0xFF };

static const DWORD kGetAuraInfo  = 0x004F8870;  // thiscall (unit, index) -> spellId
static const BYTE  kAuraInfoSig[] = { 0x55, 0x8B, 0xEC, 0x83, 0xB9, 0xD0, 0x0D, 0x00, 0x00, 0xFF };

// SpellRec field offsets (see header block).
static const int kRec_Id                = 0x000;
static const int kRec_EquippedItemClass = 0x110;

// Item class 2 = weapon. -1 in EquippedItemClass means "no requirement at all".
static const int kItemClassWeapon = 2;
static const int kNoRequirement   = -1;

typedef bool (__cdecl   *WeaponCheck_t)(void* unit, void* rec, int a3, int showError, int a5);
typedef int  (__thiscall *GetAuraCount_t)(void* unit);
typedef int  (__thiscall *GetAuraInfo_t)(void* unit, int index);

static WeaponCheck_t  pOrigWeaponCheck;
static GetAuraCount_t pGetAuraCount;
static GetAuraInfo_t  pGetAuraInfo;

// --------------------------------------------------------------- settings
static int  g_enabled    = 0;
static int  g_debug      = 0;
static char g_dir[MAX_PATH];

// Rule: "weapon". Waives the equipped-weapon requirement.
//
// A LIST, not a single id, because a rank chain is one spell to a player and N
// ids to us: Power Word: Fortitude alone is 8 ranks plus item variants, and a
// configured id that does not match the rank actually on the player looks
// exactly like a broken feature. List every id that should count.
static const int kMaxAuras = 16;
static int  g_weaponAuras[kMaxAuras];
static int  g_weaponAuraCount = 0;              // 0 = rule off

// Optional spell whitelist. Empty = every spell that has a weapon requirement,
// which is what the unarmed set wants (all your abilities, not a curated few -
// Heroic Strike alone is ten ranks). The list exists because other rules added
// later will want to name a single spell.
static const int kMaxSpells = 64;
static int  g_weaponSpells[kMaxSpells];
static int  g_weaponSpellCount = 0;

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
    // Tag with the pid: two test clients share one log file and untagged lines
    // from the second one are impossible to attribute.
    int len = _snprintf_s(line, sizeof(line), _TRUNCATE,
                          "[rulerelax pid%lu] %s\r\n", GetCurrentProcessId(), msg);
    if (len < 0) return;

    // FILE_SHARE_WRITE matters: with read-only sharing the second client's
    // lines silently vanish.
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

// ------------------------------------------------------------------ aura gate
//
// Lifted from playercollide.cpp, including the clamp: the aura count reads as a
// sentinel/garbage value transiently, and bailing out on that would drop the
// relaxation for a frame - which here means a button that flickers red.
// Returns the matching spell id, or 0 if the unit carries none of them. One
// pass over the aura array; the id list is tiny.
static int UnitHasAnyAura(void* unit, const int* ids, int count)
{
    if (!unit || count <= 0) return 0;
    int n = pGetAuraCount(unit);
    if (n < 0 || n > 256) n = 64;               // client's visible aura cap
    for (int i = 0; i < n; ++i) {
        int id = pGetAuraInfo(unit, i);
        if (id == 0) continue;
        for (int k = 0; k < count; ++k)
            if (ids[k] == id)
                return id;
    }
    return 0;
}

static bool SpellListed(const int* list, int count, int spellId)
{
    if (count == 0) return true;                // empty list = all spells
    for (int i = 0; i < count; ++i)
        if (list[i] == spellId) return true;
    return false;
}

// ---------------------------------------------------------------- the hook
static bool __cdecl WeaponCheckWrapper(void* unit, void* rec, int a3, int showError, int a5)
{
    bool usable = pOrigWeaponCheck(unit, rec, a3, showError, a5);
    if (usable || !g_enabled || !g_weaponAuraCount || !unit || !rec)
        return usable;

    if (!Readable(rec, kRec_EquippedItemClass + 4))
        return usable;

    int* pClass = (int*)((BYTE*)rec + kRec_EquippedItemClass);
    if (*pClass != kItemClassWeapon)             // not a weapon-gated spell
        return usable;

    int spellId = *(int*)((BYTE*)rec + kRec_Id);
    if (!SpellListed(g_weaponSpells, g_weaponSpellCount, spellId))
        return usable;

    int matchedAura = UnitHasAnyAura(unit, g_weaponAuras, g_weaponAuraCount);
    if (!matchedAura)
        return usable;

    // Differential re-ask: was the weapon requirement the DECIDING reason? Any
    // other cause keeps the second answer false, so we cannot mistake it for a
    // weapon problem. The record is the caller's own stack copy of the DBC row
    // (0x0080D46F passes [ebp-0x2b8]) and the client runs this single-threaded,
    // so patch-call-restore is safe; restore happens on every path.
    //
    // showError is forced to 0 on the re-ask, or a failing second call would
    // print a duplicate error to the user's chat frame.
    int saved = *pClass;
    *pClass = kNoRequirement;
    bool withoutWeaponRule = pOrigWeaponCheck(unit, rec, a3, 0, a5);
    *pClass = saved;

    if (!withoutWeaponRule)
        return usable;                          // something else is wrong; leave it red

    if (g_debug)
        Log("relaxed weapon rule: spell=%d aura=%d", spellId, matchedAura);
    return true;
}

// ------------------------------------------------------------------ settings
static void ParseSpellList(const char* s, int* out, int* count, int maxCount)
{
    *count = 0;
    while (*s && *count < maxCount) {
        while (*s == ' ' || *s == ',' || *s == '\t') ++s;
        if (!*s) break;
        int v = 0; bool any = false;
        while (*s >= '0' && *s <= '9') { v = v * 10 + (*s - '0'); ++s; any = true; }
        if (any) out[(*count)++] = v;
        while (*s && *s != ',') ++s;            // skip anything unparsed
    }
}

void RuleRelax_LoadSettings(const char* dir)
{
    strcpy_s(g_dir, sizeof(g_dir), dir ? dir : "");

    char ini[MAX_PATH];
    _snprintf_s(ini, sizeof(ini), _TRUNCATE, "%sAnimSpeedFix.ini", g_dir);

    // Defaults are compiled in so the shipped zip needs no ini at all, and an
    // ini missing a key behaves identically to no ini.
    g_enabled = GetPrivateProfileIntA("RuleRelax", "Enabled", 1, ini);
    g_debug   = GetPrivateProfileIntA("RuleRelax", "Debug",   0, ini);

    char buf[512];
    GetPrivateProfileStringA("RuleRelax", "WeaponAura", "90217", buf, sizeof(buf), ini);
    ParseSpellList(buf, g_weaponAuras, &g_weaponAuraCount, kMaxAuras);

    GetPrivateProfileStringA("RuleRelax", "WeaponSpells", "", buf, sizeof(buf), ini);
    ParseSpellList(buf, g_weaponSpells, &g_weaponSpellCount, kMaxSpells);
}

// ------------------------------------------------------------------- install
static BYTE* g_tramp;

void RuleRelax_Install()
{
    // Inert until an aura id is configured, so shipping it enabled changes
    // nothing for anyone until the set exists.
    if (!g_enabled || !g_weaponAuraCount)
        return;

    if (!VerifySig(kWeaponCheck,  kWeaponCheckSig, sizeof(kWeaponCheckSig), "weapon-check") ||
        !VerifySig(kGetAuraCount, kAuraCountSig,   sizeof(kAuraCountSig),   "GetAuraCount") ||
        !VerifySig(kGetAuraInfo,  kAuraInfoSig,    sizeof(kAuraInfoSig),    "GetAuraInfo")) {
        g_enabled = 0;
        return;
    }

    pGetAuraCount = (GetAuraCount_t)kGetAuraCount;
    pGetAuraInfo  = (GetAuraInfo_t)kGetAuraInfo;

    const size_t stolen = sizeof(kWeaponCheckSig);
    BYTE* tramp = (BYTE*)VirtualAlloc(NULL, 32, MEM_COMMIT | MEM_RESERVE,
                                      PAGE_EXECUTE_READWRITE);
    if (!tramp) {
        Log("trampoline VirtualAlloc failed - disabled");
        g_enabled = 0;
        return;
    }
    memcpy(tramp, (void*)kWeaponCheck, stolen);             // stolen prologue
    tramp[stolen] = 0xE9;                                   // jmp rel32 back
    *(DWORD*)(tramp + stolen + 1) = kWeaponCheckResume - ((DWORD)tramp + stolen + 5);
    FlushInstructionCache(GetCurrentProcess(), tramp, 32);
    g_tramp = tramp;
    pOrigWeaponCheck = (WeaponCheck_t)tramp;

    BYTE* p = (BYTE*)kWeaponCheck;
    DWORD old = 0;
    if (!VirtualProtect(p, stolen, PAGE_EXECUTE_READWRITE, &old)) {
        Log("VirtualProtect failed at 0x%08X - disabled", kWeaponCheck);
        g_enabled = 0;
        return;
    }
    memset(p, 0x90, stolen);                                // jmp rel32 (5) + nop pad
    p[0] = 0xE9;
    *(DWORD*)(p + 1) = (DWORD)&WeaponCheckWrapper - (kWeaponCheck + 5);
    VirtualProtect(p, stolen, old, &old);
    FlushInstructionCache(GetCurrentProcess(), p, stolen);

    Log("installed at 0x%08X (%d weapon aura(s), %d spell(s) listed, debug=%d)",
        kWeaponCheck, g_weaponAuraCount, g_weaponSpellCount, g_debug);
}
