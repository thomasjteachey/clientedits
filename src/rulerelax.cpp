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
//
// VIOLET HOLD CLASS BOONS (2026-11-13) - two more rules on the same pattern
// -------------------------------------------------------------------------
// Both refusals below are client-local: the 3.3.5 client never sends the cast,
// so the server-side waivers in VioletHoldClassBoons (IgnoresMinRange in
// Spell::GetMinMaxRange, ShiftsThroughControl in Spell::CheckCasterAuras) are
// never consulted without these. Full RE notes: docs/VH_CLASS_BOONS_RE.md.
//
//   "pointblank"  aura 94346. Hunter-family (SpellFamilyName 9) spells get a
//                 minimum range of 0. Hooked at the single min/max range
//                 calculator 0x007FF480, which every range consumer reaches
//                 (cast path "Target too close", IsSpellInRange / action
//                 button range tint, Auto Shot), so all of them agree.
//   "untamed"     aura 94361. Druid-family (7) spells with an
//                 APPLY_AURA MOD_SHAPESHIFT (36) effect ignore stun, fear and
//                 confuse. Done by setting the client's OWN
//                 USABLE_WHILE_STUNNED / _FEARED / _CONFUSED bits
//                 (AttributesEx5 0x8 / 0x20000 / 0x40000) on the caller's
//                 private record copy for the duration of two calls:
//                   0x00739650  caster-state check on the cast path
//                               (errors 108 STUNNED / 34 FLEEING / 26 CONFUSED)
//                   0x0080B5D0  usability predicate (button dims while control
//                               is lost - fear/confuse clear "has control")

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

// --- Point Blank: the min/max range calculator -------------------------------
//
// __cdecl(caster, SpellRec*, float* minRange, float* maxRange, int friendly,
//         target) - caller cleans 0x18. Writes both ranges from SpellRange.dbc
// (rec+0xB8 = RangeIndex) plus the melee/combat-reach special cases. It is the
// ONLY reader of the SpellRange min fields on a gate path:
//   0x00803850 (dist-vs-min/max test, register args) -> 0x00802C30 (by id)
//     -> 0x00801650 (picks friendly/hostile) -> here
// 0x00803850 is what both 0x00809610 (cast path, errors 97 OUT_OF_RANGE /
// 128 TOO_CLOSE at 0x008098FA / 0x008099CD) and the range-tint path at
// 0x0080C2B1 use, so zeroing min here makes the cast go out AND the button
// stop showing red. The other two direct callers (0x00623F8E/A7) build the
// tooltip; they then read "35 yd" instead of "5-35 yd" for a boon holder,
// which is the truth for that player.
//
// Steals push ebp / mov ebp,esp / mov edx,[ebp+0x1c] = 6 bytes, no rel ops.
static const DWORD kRangeCalc       = 0x007FF480;
static const BYTE  kRangeCalcSig[]  = { 0x55, 0x8B, 0xEC, 0x8B, 0x55, 0x1C, 0x83, 0xEC, 0x08, 0x85, 0xD2 };
static const size_t kRangeCalcStolen = 6;

// --- Untamed: caster-state check on the cast path ----------------------------
//
// __thiscall(unit, errSink, SpellRec*, a3) -> bool in al, ret 0xC. ONE caller:
// 0x0080D6D1 in the cast function 0x0080CCE0, which passes its own stack copy
// of the record ([ebp-0x2B8]). The stun / fear / confuse blocks are:
//   0x00739B33  UNIT_FLAGS & 0x40000  unless Ex5 & 0x8      -> error 108 STUNNED
//   0x00739C90  UNIT_FLAGS & 0x800000 unless Ex5 & 0x20000  -> error 34 FLEEING
//   0x00739D09  UNIT_FLAGS & 0x400000 unless Ex5 & 0x40000  -> error 26 CONFUSED
// (each also accepts Ex7 & 0x100000 and a per-mechanic aura test, and reports
// 147 PREVENTED_BY_MECHANIC instead when that test names a mechanic). Silence
// (PreventionType 1) and pacify (2) sit between them and are NOT touched.
//
// Steals push ebp / mov ebp,esp / sub esp,0x2BC = 9 bytes, no rel ops.
static const DWORD kCasterState       = 0x00739650;
static const BYTE  kCasterStateSig[]  = { 0x55, 0x8B, 0xEC, 0x81, 0xEC, 0xBC, 0x02, 0x00, 0x00, 0x56 };
static const size_t kCasterStateStolen = 9;

// --- Untamed: the usability predicate ----------------------------------------
//
// Same function the header block describes. When the global "player has
// control" flag 0x00BCFB8C is 0 (written only by 0x00520FE0, which also fires
// the control gained/lost events - fear and confuse clear it, stun does not),
// 0x0080B6E4..0x0080B7A7 marks the spell unusable unless Ex5 says
// usable-while-feared (0x20000) / -confused (0x40000) for the state you are
// in. So without this the form buttons dim while feared or polymorphed even
// though the cast path would now allow them.
//
// Steals push ebp / mov ebp,esp / sub esp,0x10 = 6 bytes, no rel ops.
static const DWORD kUsable       = 0x0080B5D0;
static const BYTE  kUsableSig[]  = { 0x55, 0x8B, 0xEC, 0x83, 0xEC, 0x10, 0x57, 0x8B, 0x7D, 0x08 };
static const size_t kUsableStolen = 6;

// In-memory SpellRec is NOT the raw DBC row past field 135: each localized
// string (name, rank, description, tooltip) is ONE pointer in memory instead of
// 17 DBC fields, so every field >= 204 sits 64 fields (0x100 bytes) earlier.
// Record size 0x2A8 = (234 - 64) * 4 matches the copy buffer in 0x00802C30.
// Cross-checked: PreventionType (DBC 214) is read at +0x258 in 0x00739BAB, and
// SpellFamilyName (DBC 208) at +0x240 by the spellmod family match 0x007FDEC0
// (followed by the three family-flag words at +0x244).
static const int kRec_AttributesEx5   = 0x024;   // field 9
static const int kRec_ApplyAuraName1  = 0x17C;   // fields 95..97, raw layout
static const int kRec_SpellFamilyName = 0x240;   // DBC field 208, in-memory 144

static const int kFamilyDruid    = 7;
static const int kFamilyHunter   = 9;
static const int kAuraModShapeshift = 36;

// SPELL_ATTR5_USABLE_WHILE_STUNNED | _FEARED | _CONFUSED - the client's own
// exemptions, read at 0x00739B45 / 0x00739CA4 / 0x00739D1D and 0x0080B708 /
// 0x0080B73F.
static const DWORD kEx5UsableWhileControlled = 0x00000008 | 0x00020000 | 0x00040000;

// UNIT_FIELD_FLAGS (descriptor +0xD4) bits the waiver is about. Only used to
// keep the debug log quiet when you are not actually controlled.
static const int   kUnit_Descriptor = 0xD0;
static const int   kDesc_UnitFlags  = 0xD4;
static const DWORD kUnitFlagsControlled = 0x00040000 | 0x00400000 | 0x00800000;

typedef bool (__cdecl   *WeaponCheck_t)(void* unit, void* rec, int a3, int showError, int a5);
typedef int  (__thiscall *GetAuraCount_t)(void* unit);
typedef int  (__thiscall *GetAuraInfo_t)(void* unit, int index);
typedef int  (__cdecl   *RangeCalc_t)(void* caster, void* rec, float* minRange, float* maxRange,
                                      int friendly, void* target);
typedef bool (__thiscall *CasterState_t)(void* unit, void* errSink, void* rec, void* a3);
typedef bool (__cdecl   *Usable_t)(void* unit, void* rec, void* a3, void* a4, void* a5);

static WeaponCheck_t  pOrigWeaponCheck;
static GetAuraCount_t pGetAuraCount;
static GetAuraInfo_t  pGetAuraInfo;
static RangeCalc_t    pOrigRangeCalc;
static CasterState_t  pOrigCasterState;
static Usable_t       pOrigUsable;

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

// Rule: "pointblank" (Violet Hold Boon of Point Blank). Hunter-family spells
// lose their minimum range. 0 entries = rule off.
static int  g_pointBlankAuras[kMaxAuras];
static int  g_pointBlankAuraCount = 0;

// Rule: "untamed" (Violet Hold Boon of the Untamed). Druid shapeshifts ignore
// stun / fear / confuse. 0 entries = rule off.
static int  g_untamedAuras[kMaxAuras];
static int  g_untamedAuraCount = 0;

// Set per rule only once its detour is actually in place.
static bool g_pointBlankLive = false;
static bool g_untamedCastLive = false;
static bool g_untamedUsableLive = false;

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

// The Untamed rule writes into the caller's record for the length of one call.
// Every caller seen passes a stack/heap COPY (the Spell DB hands out copies via
// 0x004CFD20), but never write without asking the OS first.
static bool Writable(void* p, size_t n)
{
    MEMORY_BASIC_INFORMATION mbi;
    if (!VirtualQuery(p, &mbi, sizeof(mbi))) return false;
    if (mbi.State != MEM_COMMIT) return false;
    if (mbi.Protect & (PAGE_NOACCESS | PAGE_GUARD)) return false;
    const DWORD rw = PAGE_READWRITE | PAGE_EXECUTE_READWRITE;
    if (!(mbi.Protect & rw)) return false;
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

// ------------------------------------------------- Point Blank (min range)
//
// Runs the real calculation, then zeroes the minimum only when everything
// lines up. Cheapest tests first: this sits under the action-bar range tint
// and runs for every ranged button every update, so the aura walk happens only
// for a hunter spell that actually has a minimum.
static int __cdecl RangeCalcWrapper(void* caster, void* rec, float* minRange, float* maxRange,
                                    int friendly, void* target)
{
    int ret = pOrigRangeCalc(caster, rec, minRange, maxRange, friendly, target);
    if (!g_enabled || !caster || !rec || !minRange)
        return ret;
    if (!(*minRange > 0.0f))                    // the original just wrote it
        return ret;
    if (!Readable(rec, kRec_SpellFamilyName + 4))
        return ret;
    if (*(int*)((BYTE*)rec + kRec_SpellFamilyName) != kFamilyHunter)
        return ret;

    int matchedAura = UnitHasAnyAura(caster, g_pointBlankAuras, g_pointBlankAuraCount);
    if (!matchedAura)
        return ret;

    if (g_debug) {
        // Throttled: this fires every frame per lit button.
        static int   s_lastSpell;
        static DWORD s_lastTick;
        int   spellId = *(int*)((BYTE*)rec + kRec_Id);
        DWORD now     = GetTickCount();
        if (spellId != s_lastSpell || now - s_lastTick > 5000) {
            Log("relaxed min range: spell=%d min=%.1f -> 0 aura=%d",
                spellId, (double)*minRange, matchedAura);
            s_lastSpell = spellId;
            s_lastTick  = now;
        }
    }
    *minRange = 0.0f;
    return ret;
}

// ------------------------------------------- Untamed (stun / fear / confuse)
//
// True if this cast is a druid shapeshift and the unit holds the boon. The
// server's ShiftsThroughControl asks exactly this:
//   SpellFamilyName == DRUID && HasAura(SPELL_AURA_MOD_SHAPESHIFT) && Holds(...)
static bool UntamedApplies(void* unit, void* rec, int* matchedAura)
{
    *matchedAura = 0;
    if (!g_enabled || !g_untamedAuraCount || !unit || !rec)
        return false;
    if (!Readable(rec, kRec_SpellFamilyName + 4))
        return false;
    if (*(int*)((BYTE*)rec + kRec_SpellFamilyName) != kFamilyDruid)
        return false;

    const int* auraNames = (const int*)((BYTE*)rec + kRec_ApplyAuraName1);
    if (auraNames[0] != kAuraModShapeshift && auraNames[1] != kAuraModShapeshift &&
        auraNames[2] != kAuraModShapeshift)
        return false;

    if (!Writable((BYTE*)rec + kRec_AttributesEx5, 4))
        return false;

    *matchedAura = UnitHasAnyAura(unit, g_untamedAuras, g_untamedAuraCount);
    return *matchedAura != 0;
}

static DWORD UnitFlagsOf(void* unit)
{
    void** pDesc = (void**)((BYTE*)unit + kUnit_Descriptor);
    if (!Readable(pDesc, 4)) return 0;
    BYTE* desc = (BYTE*)*pDesc;
    if (!desc || !Readable(desc + kDesc_UnitFlags, 4)) return 0;
    return *(DWORD*)(desc + kDesc_UnitFlags);
}

// The record is the caller's private copy (cast path: [ebp-0x2B8] of
// 0x0080CCE0), the client is single-threaded here, and the bits are restored
// on the only exit. Ex5 bits are set rather than asked differentially because
// 0x00739650 reports its error the moment it fails - a first "real" ask would
// already have printed "Can't do that while stunned".
static bool __fastcall CasterStateWrapper(void* unit, void* /*edx*/, void* errSink, void* rec, void* a3)
{
    int matchedAura;
    if (!UntamedApplies(unit, rec, &matchedAura))
        return pOrigCasterState(unit, errSink, rec, a3);

    DWORD* pEx5  = (DWORD*)((BYTE*)rec + kRec_AttributesEx5);
    DWORD  saved = *pEx5;
    *pEx5 = saved | kEx5UsableWhileControlled;
    bool ok = pOrigCasterState(unit, errSink, rec, a3);
    *pEx5 = saved;

    if (g_debug) {
        DWORD flags = UnitFlagsOf(unit);
        if (flags & kUnitFlagsControlled)
            Log("untamed cast: spell=%d unitFlags=0x%08X aura=%d -> %s",
                *(int*)((BYTE*)rec + kRec_Id), flags, matchedAura, ok ? "allowed" : "still refused");
    }
    return ok;
}

// The predicate prints nothing, but it is asked for every form button on every
// usability update, so it logs nothing either.
static bool __cdecl UsableWrapper(void* unit, void* rec, void* a3, void* a4, void* a5)
{
    int matchedAura;
    if (!UntamedApplies(unit, rec, &matchedAura))
        return pOrigUsable(unit, rec, a3, a4, a5);

    DWORD* pEx5  = (DWORD*)((BYTE*)rec + kRec_AttributesEx5);
    DWORD  saved = *pEx5;
    *pEx5 = saved | kEx5UsableWhileControlled;
    bool ok = pOrigUsable(unit, rec, a3, a4, a5);
    *pEx5 = saved;
    return ok;
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

    // Violet Hold class boons. Must match SPELL_POINT_BLANK / SPELL_UNTAMED in
    // src/server/game/Miscellaneous/VioletHoldClassBoons.h - nothing enforces
    // that. An empty value turns the rule off.
    GetPrivateProfileStringA("RuleRelax", "PointBlankAura", "94346", buf, sizeof(buf), ini);
    ParseSpellList(buf, g_pointBlankAuras, &g_pointBlankAuraCount, kMaxAuras);

    GetPrivateProfileStringA("RuleRelax", "UntamedAura", "94361", buf, sizeof(buf), ini);
    ParseSpellList(buf, g_untamedAuras, &g_untamedAuraCount, kMaxAuras);
}

// ------------------------------------------------------------------- install
//
// Generic entry detour: verify the signature, copy `stolen` bytes (whole,
// position-independent instructions - checked per site above) into a
// trampoline that jumps back to addr+stolen, then point addr at the hook.
// Returns the trampoline (= "call the original") or NULL with the reason
// logged. Each rule installs on its own: one bad signature switches off that
// rule only.
static void* InstallDetour(DWORD addr, const BYTE* sig, size_t sigLen, size_t stolen,
                           void* hook, const char* name)
{
    if (stolen < 5 || stolen > sigLen || !VerifySig(addr, sig, sigLen, name))
        return NULL;

    BYTE* tramp = (BYTE*)VirtualAlloc(NULL, 32, MEM_COMMIT | MEM_RESERVE,
                                      PAGE_EXECUTE_READWRITE);
    if (!tramp) {
        Log("%s: trampoline VirtualAlloc failed - disabled", name);
        return NULL;
    }
    memcpy(tramp, (void*)addr, stolen);                     // stolen prologue
    tramp[stolen] = 0xE9;                                   // jmp rel32 back
    *(DWORD*)(tramp + stolen + 1) = (addr + (DWORD)stolen) - ((DWORD)tramp + (DWORD)stolen + 5);
    FlushInstructionCache(GetCurrentProcess(), tramp, 32);

    BYTE* p = (BYTE*)addr;
    DWORD old = 0;
    if (!VirtualProtect(p, stolen, PAGE_EXECUTE_READWRITE, &old)) {
        Log("%s: VirtualProtect failed at 0x%08X - disabled", name, addr);
        VirtualFree(tramp, 0, MEM_RELEASE);
        return NULL;
    }
    memset(p, 0x90, stolen);                                // jmp rel32 (5) + nop pad
    p[0] = 0xE9;
    *(DWORD*)(p + 1) = (DWORD)hook - (addr + 5);
    VirtualProtect(p, stolen, old, &old);
    FlushInstructionCache(GetCurrentProcess(), p, stolen);
    return tramp;
}

void RuleRelax_Install()
{
    // Inert until an aura id is configured, so shipping it enabled changes
    // nothing for anyone until the boon / set exists.
    if (!g_enabled || (!g_weaponAuraCount && !g_pointBlankAuraCount && !g_untamedAuraCount))
        return;

    // Every rule reads auras; without these nothing is safe to install.
    if (!VerifySig(kGetAuraCount, kAuraCountSig, sizeof(kAuraCountSig), "GetAuraCount") ||
        !VerifySig(kGetAuraInfo,  kAuraInfoSig,  sizeof(kAuraInfoSig),  "GetAuraInfo")) {
        g_enabled = 0;
        return;
    }
    pGetAuraCount = (GetAuraCount_t)kGetAuraCount;
    pGetAuraInfo  = (GetAuraInfo_t)kGetAuraInfo;

    if (g_weaponAuraCount) {
        pOrigWeaponCheck = (WeaponCheck_t)InstallDetour(kWeaponCheck, kWeaponCheckSig,
            sizeof(kWeaponCheckSig), sizeof(kWeaponCheckSig), (void*)&WeaponCheckWrapper,
            "weapon-check");
        if (pOrigWeaponCheck)
            Log("weapon rule installed at 0x%08X (%d aura(s), %d spell(s) listed, debug=%d)",
                kWeaponCheck, g_weaponAuraCount, g_weaponSpellCount, g_debug);
        else
            g_weaponAuraCount = 0;
    }

    if (g_pointBlankAuraCount) {
        pOrigRangeCalc = (RangeCalc_t)InstallDetour(kRangeCalc, kRangeCalcSig,
            sizeof(kRangeCalcSig), kRangeCalcStolen, (void*)&RangeCalcWrapper, "range-calc");
        g_pointBlankLive = pOrigRangeCalc != NULL;
        if (g_pointBlankLive)
            Log("pointblank rule installed at 0x%08X (%d aura(s), first %d)",
                kRangeCalc, g_pointBlankAuraCount, g_pointBlankAuras[0]);
        else
            g_pointBlankAuraCount = 0;
    }

    if (g_untamedAuraCount) {
        // Both signatures are checked before either site is patched, so a
        // client build that matches only one half gets neither. The cast-path
        // half is the one that matters (it is the refusal); the predicate only
        // un-dims the button while feared/confused, so if patching it alone
        // fails at runtime the rule stays on and the log says so.
        bool sigsOk =
            VerifySig(kCasterState, kCasterStateSig, sizeof(kCasterStateSig), "caster-state") &&
            VerifySig(kUsable,      kUsableSig,      sizeof(kUsableSig),      "usable-predicate");
        if (sigsOk) {
            pOrigCasterState = (CasterState_t)InstallDetour(kCasterState, kCasterStateSig,
                sizeof(kCasterStateSig), kCasterStateStolen, (void*)&CasterStateWrapper,
                "caster-state");
            g_untamedCastLive = pOrigCasterState != NULL;
        }
        if (g_untamedCastLive) {
            pOrigUsable = (Usable_t)InstallDetour(kUsable, kUsableSig, sizeof(kUsableSig),
                kUsableStolen, (void*)&UsableWrapper, "usable-predicate");
            g_untamedUsableLive = pOrigUsable != NULL;
        }
        if (g_untamedCastLive)
            Log("untamed rule installed at 0x%08X + 0x%08X%s (%d aura(s), first %d)",
                kCasterState, kUsable, g_untamedUsableLive ? "" : " [predicate FAILED]",
                g_untamedAuraCount, g_untamedAuras[0]);
        else
            g_untamedAuraCount = 0;
    }
}
