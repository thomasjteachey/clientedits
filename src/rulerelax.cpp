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
//                 minimum range of 0. Aimed Shot and Multi-Shot also pass the
//                 two start-while-moving checks without becoming instant.
//                 Range is hooked at the single calculator 0x007FF480, which
//                 every range consumer reaches; moving reuses the cast-time
//                 detour at 0x007FF180 only for its two refusal callers.
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
//
// "fieldmedic" (2026-11-14) aura 94383. A hunter's damaging shots (family 9,
// DmgClass 3, nothing but damage effects - the same predicate as the server's
// VioletHoldClassBoons::ShootsAllies: Auto Shot, Arcane Shot, Multi-Shot,
// Aimed Shot, Kill Shot...) may be cast at a friendly unit. The refusal is in
// the unit-target acceptor 0x0080BC80: implicit target 6 (TARGET_UNIT_TARGET_
// ENEMY) becomes TARGET_FLAG_UNIT_ENEMY (0x80) in the pending-cast flags
// 0x00D3F4E0, and that branch accepts a unit only if CanAttack 0x00729740 says
// so. A friend falls through, the cast function reports error 12
// (SPELL_FAILED_BAD_TARGETS, "Invalid target") at 0x0080D7E1 and nothing is
// sent. The waiver swaps 0x80 for the client's own UNIT_ALLY flag (0x100) for
// the one call when the unit is not attackable, so the client's own ally branch
// (CanAssist 0x007293D0, then the normal range check) decides. Plus two
// cosmetic halves: 0x00809610 (IsActionInRange / IsSpellInRange: range tint on
// a friend) and the usability predicate's target-aura-state block (Kill Shot).
//
// "secondcoming" (2026-11-14) aura 94386. Priest Resurrection (family 6 with a
// RESURRECT_NEW 113 effect) is castable in combat and instant. In combat: clear
// SPELL_ATTR0_CANT_USED_IN_COMBAT on the record copy for the caster-state check
// 0x00739650 (error 1 AFFECTING_COMBAT at 0x0073A19B) and the usability
// predicate 0x0080B5D0 (button dims). Instant: the active player's cast-time
// calculator 0x007FF180 (move check, send, local cast prediction, tooltip,
// GetSpellInfo) and CGPlayer's cast-time virtual 0x006D68D0 (cast-start
// visual) report 0 for a holder.

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <intrin.h>
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
static const int kRec_SpellFamilyFlags1 = 0x244;

// --- Field Medic -------------------------------------------------------------
//
// THE refusal: the unit-target acceptor. __cdecl(CGObject* obj, CastTargets* ct)
// -> bool in al, caller cleans 8. ct == NULL means the pending cast at
// 0x00D3F4E4. It refetches the record itself (id at ct+0x20, caster guid at
// ct+0x10) and walks the still-needed target flags in the global 0x00D3F4E0:
//   0x8 party / 0x4 raid / 0x100 ally  -> CanAssist 0x007293D0   (0x0080BE83..)
//   0x80 enemy                         -> CanAttack 0x00729740   (0x0080BF54)
//   0x2 any unit, 0x8000/0x200 corpses, 0x10000 minipet ...
// The branch that accepts the unit clears ITS bit, stores the guid, runs the
// range check (0x00803850 -> errors 97 / 128 at 0x0080C2DE, restoring the
// flags from entry on failure) and, when no bits are left, sends the cast
// (0x0080AC90). A friend under 0x80 alone matches nothing: returns 0 silently,
// 0x0080C790 returns 0, and 0x0080CCE0 reports 12 BAD_TARGETS (11 with no
// target) through 0x00808200 at 0x0080D755. Nine callers: the cast path
// (0x0080CBC1 / 0x0080CC0C / 0x0080CC2C in 0x0080C790), clicking a unit while
// the spell cursor is up (0x00524CE9 in the target handler 0x00524BF0),
// 0x0080DBF4, 0x0080DD9C, 0x005167C8, 0x00521504 (pet), 0x007118D9.
//
// Steals push ebp / mov ebp,esp / sub esp,0x2B0 = 9 bytes, no rel ops.
static const DWORD kTryTarget       = 0x0080BC80;
static const BYTE  kTryTargetSig[]  = { 0x55, 0x8B, 0xEC, 0x81, 0xEC, 0xB0, 0x02, 0x00, 0x00, 0x56, 0x8B, 0x75, 0x0C };
static const size_t kTryTargetStolen = 9;

// Explicit-target + range pre-check. __cdecl(caster, SpellRec*, guidLo, guidHi,
// bool* inRange, bool showError, errSink) -> bool, caller cleans 0x1C. Same
// flag walk, but local: Targets (rec+0x40, a WORD) OR the implicit-target map.
// Returns 0 = "not a target this spell takes" (no error), 1 + *inRange. Callers
// all pass a stack copy of the record: the cast path 0x0080D6AD (0 just skips
// to the acceptor above, so this half is NOT the refusal), the action bar's
// IsActionInRange 0x005A9596, Lua IsSpellInRange 0x00541D6E, 0x0051CAB2,
// 0x005E55B6 (0 = nil = no range tint at all on a friend).
// Steals push ebp / mov ebp,esp / push ecx / mov eax,[ebp+0x10] = 7 bytes.
static const DWORD kTargetCheck       = 0x00809610;
static const BYTE  kTargetCheckSig[]  = { 0x55, 0x8B, 0xEC, 0x51, 0x8B, 0x45, 0x10, 0x8B, 0x4D, 0x14, 0x8B, 0xD0 };
static const size_t kTargetCheckStolen = 7;

// Reaction tests, called, never patched.
//   CanAttack  __thiscall(unit, target) -> bool, ret 4
//   CanAssist  __thiscall(unit, target, int flag) -> bool, ret 8
static const DWORD kCanAttack     = 0x00729740;
static const BYTE  kCanAttackSig[] = { 0x55, 0x8B, 0xEC, 0x53, 0x8B, 0xD9, 0x8B, 0x43, 0x08 };
static const DWORD kCanAssist     = 0x007293D0;
static const BYTE  kCanAssistSig[] = { 0x55, 0x8B, 0xEC, 0x53, 0x8B, 0x5D, 0x08, 0x8B, 0x83, 0xD0, 0x00, 0x00, 0x00 };

// Spell DB record copy: __thiscall(db = 0x00AD49D0, id, buf) -> nonzero on
// success, ret 8. Copies 0x2A8 bytes (rep movsd 0xAA dwords, or the
// decompressing path 0x004CFBB0 with the same size). The client brackets it
// with 0x008B7DA0 / 0x005EEB70, which are `mov eax, ecx; ret` and `ret`.
static const DWORD kSpellDb       = 0x00AD49D0;
static const DWORD kSpellDbGet    = 0x004CFD20;
static const BYTE  kSpellDbGetSig[] = { 0x55, 0x8B, 0xEC, 0x8B, 0x51, 0x10, 0x8B, 0x45, 0x08, 0x3B, 0xC2 };
static const size_t kSpellRecSize = 0x2A8;

// ObjectPtr: __cdecl(guidLo, guidHi, typeMask, file, line) -> obj or NULL. It
// tests typeMask against the object's OBJECT_FIELD_TYPE itself; file/line are
// never read (same call playercollide.cpp makes with three args).
static const DWORD kObjectPtr     = 0x004D4DB0;
static const BYTE  kObjectPtrSig[] = { 0x55, 0x8B, 0xEC, 0x64, 0x8B, 0x0D, 0x2C };

static const DWORD kCastTargetFlags = 0x00D3F4E0;  // still-needed TARGET_FLAG_* of the pending cast
static const DWORD kPendingCast     = 0x00D3F4E4;  // CastTargets* of the pending cast
static const DWORD kCurrentTarget   = 0x00BD07B0;  // selected target guid (lo, hi)

static const DWORD kTargetFlagUnit      = 0x0002;
static const DWORD kTargetFlagRaid      = 0x0004;
static const DWORD kTargetFlagParty     = 0x0008;
static const DWORD kTargetFlagUnitEnemy = 0x0080;
static const DWORD kTargetFlagUnitAlly  = 0x0100;

static const DWORD kTypeMaskUnit = 0x8;

static const int kCast_CasterGuid = 0x10;
static const int kCast_SpellId    = 0x20;

static const int kRec_Targets             = 0x040;  // field 16, read as a WORD
static const int kRec_TargetAuraState     = 0x054;  // field 21
static const int kRec_TargetAuraStateNot  = 0x05C;  // field 23
static const int kRec_TargetAuraSpell     = 0x064;  // field 25
static const int kRec_ExcludeTargetAura   = 0x06C;  // field 27
static const int kRec_Effect1             = 0x11C;  // fields 71..73
static const int kRec_ImplicitTargetA1    = 0x158;  // field 86
// DBC 213 -> in-memory 149. Verified: 0x00805DE7 reads [rec+0x254] and skips
// GCD haste for 2 (melee) and 3 (ranged) - DmgClass semantics.
static const int kRec_DmgClass            = 0x254;

static const int kDmgClassRanged          = 3;
static const int kTargetUnitTargetEnemy   = 6;
static const int kTargetUnitTargetAlly    = 21;

// --- Second Coming -----------------------------------------------------------
//
// In-combat refusal: SPELL_ATTR0_CANT_USED_IN_COMBAT (Attributes 0x10000000)
// is tested on a spell record in exactly two places (byte scan of every
// `test …, 0x10000000` and every `mov r32, 0x10000000` in the spell code):
//   0x0073A169  caster-state check 0x00739650 (cast path): Attributes & edi
//               (edi = 0x10000000 from 0x0073A132), UNIT_FLAGS & 0x80000 (in
//               combat), unless aura type 262 affects the spell (0x0072AAF0)
//               -> error 1 SPELL_FAILED_AFFECTING_COMBAT at 0x0073A19B
//   0x0080B89A  usability predicate 0x0080B5D0: same test -> button dims
// Both receive the caller's record copy, so the bit is cleared for the call
// (the Untamed pattern; both functions are already detoured for it).
//
// Cast time: SpellCastTimes (DB object 0x00AD4748, rows read inline through
// 0x00AD4754 / 0x00AD4758 / 0x00AD4768) is read from rec+0x70 in exactly three
// functions:
//   0x007FF180  __cdecl(rec, a2, a3, a4) -> ms. The active player's cast time:
//               base, level scaling, CASTING_TIME spellmods (0x007FDB50 type
//               10), haste. Callers: caster-state move check 0x0073A0BB (error
//               51 MOVING), send function 0x0080B081 (MOVING again) and its
//               local cast prediction 0x007FA450 (0x0080B406), 0x00809FD5,
//               Lua GetSpellInfo 0x00540C6A, tooltip builder 0x00624471.
//   0x006D68D0  CGPlayer virtual (vtable 0x00A326C8 + 0x148), __thiscall(unit,
//               rec) -> ms; asked by the cast-start visual 0x0073B140 at
//               0x0073B275 ("instant?" -> which precast animation).
//   0x0071AD20  CGUnit version of the same virtual (vtable 0x00A34D90 + 0x148)
//               - creatures; a priest holding the aura is always a CGPlayer.
// Hooked: the first two, returning 0 for a holder.
//
// Steals push ebp / mov ebp,esp / sub esp,0xC = 6 bytes in both, no rel ops.
static const DWORD kCastTime        = 0x007FF180;
static const BYTE  kCastTimeSig[]   = { 0x55, 0x8B, 0xEC, 0x83, 0xEC, 0x0C, 0x57, 0x8B, 0x7D, 0x08, 0x85, 0xFF };
static const size_t kCastTimeStolen = 6;

static const DWORD kPlayerCastTime       = 0x006D68D0;
static const BYTE  kPlayerCastTimeSig[]  = { 0x55, 0x8B, 0xEC, 0x83, 0xEC, 0x0C, 0x53, 0x8B, 0x5D, 0x08, 0x85, 0xDB };
static const size_t kPlayerCastTimeStolen = 6;

// GetActivePlayerGuid: __cdecl () -> u64 in edx:eax (same as playercollide).
static const DWORD kGetActiveGuid   = 0x004D3790;
static const BYTE  kGetActiveSig[]  = { 0x64, 0x8B, 0x0D, 0x2C, 0x00, 0x00, 0x00, 0xA1 };

static const int   kRec_Attributes          = 0x010;  // field 4
static const DWORD kAttr0CantUsedInCombat   = 0x10000000;
static const DWORD kUnitFlagInCombat        = 0x00080000;
static const int   kFamilyPriest            = 6;
static const int   kEffectResurrectNew      = 113;

static const int kFamilyDruid    = 7;
static const int kFamilyHunter   = 9;
static const int kAuraModShapeshift = 36;
static const DWORD kHunterFamilyFlagMultiShot = 0x00001000;
static const DWORD kHunterFamilyFlagAimedShot = 0x00020000;

// The two callers of the active-player cast-time calculator which reject a
// cast that starts while moving. Returning zero only to these callers waives
// their MOVING result without making the shot instant for prediction, the
// cast bar or the server.
static const DWORD kCasterStateMoveCheckReturn = 0x0073A0C0;
static const DWORD kSendMoveCheckReturn        = 0x0080B086;

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
typedef bool (__cdecl   *TryTarget_t)(void* obj, void* castTargets);
typedef bool (__cdecl   *TargetCheck_t)(void* caster, void* rec, DWORD guidLo, DWORD guidHi,
                                        void* inRange, int showError, void* errSink);
typedef bool (__thiscall *CanAttack_t)(void* unit, void* target);
typedef bool (__thiscall *CanAssist_t)(void* unit, void* target, int flag);
typedef int  (__thiscall *SpellDbGet_t)(void* db, int spellId, void* buf);
typedef void* (__cdecl  *ObjectPtr_t)(DWORD guidLo, DWORD guidHi, DWORD typeMask,
                                      const char* file, int line);
typedef int  (__cdecl   *CastTime_t)(void* rec, int a2, int a3, int a4);
typedef int  (__thiscall *PlayerCastTime_t)(void* unit, void* rec);
typedef unsigned __int64 (__cdecl *GetActiveGuid_t)();

static WeaponCheck_t  pOrigWeaponCheck;
static GetAuraCount_t pGetAuraCount;
static GetAuraInfo_t  pGetAuraInfo;
static RangeCalc_t    pOrigRangeCalc;
static CasterState_t  pOrigCasterState;
static Usable_t       pOrigUsable;
static TryTarget_t    pOrigTryTarget;
static TargetCheck_t  pOrigTargetCheck;
static CanAttack_t    pCanAttack;
static CanAssist_t    pCanAssist;
static SpellDbGet_t   pSpellDbGet;
static ObjectPtr_t    pObjectPtr;
static CastTime_t       pOrigCastTime;
static PlayerCastTime_t pOrigPlayerCastTime;
static GetActiveGuid_t  pGetActiveGuid;

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

// Rule: "fieldmedic" (Violet Hold Boon of the Field Medic). Damaging hunter
// shots accept a friendly unit target. 0 entries = rule off.
static int  g_fieldMedicAuras[kMaxAuras];
static int  g_fieldMedicAuraCount = 0;

// Set per rule only once its detour is actually in place.
static bool g_pointBlankLive = false;
static bool g_pointBlankMovingLive = false;
static bool g_untamedCastLive = false;
static bool g_untamedUsableLive = false;
static bool g_fieldMedicLive = false;        // the acceptor 0x0080BC80 is hooked

// Rule: "secondcoming" (Violet Hold Second Coming charge). Priest Resurrection
// is castable in combat and instant. 0 entries = rule off.
static int  g_secondComingAuras[kMaxAuras];
static int  g_secondComingAuraCount = 0;

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

static bool IsPointBlankMovingShot(const void* rec)
{
    if (!rec || !Readable((void*)rec, kRec_SpellFamilyFlags1 + 4))
        return false;
    const BYTE* r = (const BYTE*)rec;
    if (*(const int*)(r + kRec_SpellFamilyName) != kFamilyHunter)
        return false;
    DWORD const familyFlags = *(const DWORD*)(r + kRec_SpellFamilyFlags1);
    return (familyFlags & (kHunterFamilyFlagAimedShot | kHunterFamilyFlagMultiShot)) != 0;
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
// ------------------------------------- Second Coming (in-combat, instant res)
//
// Priest family with a RESURRECT_NEW (113) effect in any slot: Resurrection
// 2006 / 2010 / 10880 / 10881 / 20770 / 25435 / 48171 (and, by the same
// predicate, the unused 21074 / 32841 / 51035 variants).
static bool IsSecondComingSpell(const void* rec)
{
    if (!rec || !Readable((void*)rec, kRec_SpellFamilyName + 4))
        return false;
    const BYTE* r = (const BYTE*)rec;
    if (*(const int*)(r + kRec_SpellFamilyName) != kFamilyPriest)
        return false;
    const int* effects = (const int*)(r + kRec_Effect1);
    return effects[0] == kEffectResurrectNew || effects[1] == kEffectResurrectNew ||
           effects[2] == kEffectResurrectNew;
}

static int SecondComingHolder(void* unit, const void* rec)
{
    if (!g_enabled || !g_secondComingAuraCount || !unit || !IsSecondComingSpell(rec))
        return 0;
    return UnitHasAnyAura(unit, g_secondComingAuras, g_secondComingAuraCount);
}

// Calls `fn` with SPELL_ATTR0_CANT_USED_IN_COMBAT cleared on the record copy.
// Nothing else reads that bit in either function (see the constants block).
#define WITH_COMBAT_BIT_CLEARED(rec, expr)                                    \
    do {                                                                     \
        DWORD* pAttr_ = (DWORD*)((BYTE*)(rec) + kRec_Attributes);            \
        DWORD  savedAttr_ = *pAttr_;                                         \
        *pAttr_ = savedAttr_ & ~kAttr0CantUsedInCombat;                      \
        expr;                                                                \
        *pAttr_ = savedAttr_;                                                \
    } while (0)

// The cast-time calculator for the active player. Runs the real calculation,
// then reports instant for a holder. a2 != 0 means the cast is for the
// player's charm/pet (0x007FF224 then measures haste on that unit); a pet
// never casts Resurrection, but leave that case alone anyway.
static int __cdecl CastTimeWrapper(void* rec, int a2, int a3, int a4)
{
    int ms = pOrigCastTime(rec, a2, a3, a4);
    if (ms <= 0 || a2 || !g_enabled)
        return ms;

    unsigned __int64 guid = pGetActiveGuid();
    if (!guid)
        return ms;
    void* player = pObjectPtr((DWORD)guid, (DWORD)(guid >> 32), kTypeMaskUnit, "rulerelax", __LINE__);

    DWORD returnAddress = (DWORD)_ReturnAddress();
    if (g_pointBlankMovingLive && player && IsPointBlankMovingShot(rec) &&
        (returnAddress == kCasterStateMoveCheckReturn || returnAddress == kSendMoveCheckReturn)) {
        int matchedAura = UnitHasAnyAura(player, g_pointBlankAuras, g_pointBlankAuraCount);
        if (matchedAura) {
            if (g_debug)
                Log("point blank moving cast: spell=%d cast time check %d ms -> allowed aura=%d",
                    *(int*)((BYTE*)rec + kRec_Id), ms, matchedAura);
            return 0;
        }
    }

    if (!g_secondComingAuraCount || !IsSecondComingSpell(rec))
        return ms;

    int matchedAura = player ? UnitHasAnyAura(player, g_secondComingAuras, g_secondComingAuraCount) : 0;
    if (!matchedAura)
        return ms;

    if (g_debug) {
        // Throttled: the tooltip asks every frame while it is shown.
        static int   s_lastSpell;
        static DWORD s_lastTick;
        int   spellId = *(int*)((BYTE*)rec + kRec_Id);
        DWORD now     = GetTickCount();
        if (spellId != s_lastSpell || now - s_lastTick > 5000) {
            Log("second coming: spell=%d cast time %d ms -> instant aura=%d", spellId, ms, matchedAura);
            s_lastSpell = spellId;
            s_lastTick  = now;
        }
    }
    return 0;
}

// CGPlayer::GetSpellCastTime (virtual). Only the cast-start visual asks it.
static int __fastcall PlayerCastTimeWrapper(void* unit, void* /*edx*/, void* rec)
{
    int ms = pOrigPlayerCastTime(unit, rec);
    if (ms > 0 && SecondComingHolder(unit, rec))
        return 0;
    return ms;
}

static bool __fastcall CasterStateWrapper(void* unit, void* /*edx*/, void* errSink, void* rec, void* a3)
{
    int matchedAura;
    if (!UntamedApplies(unit, rec, &matchedAura)) {
        int scAura = SecondComingHolder(unit, rec);
        if (!scAura || !Writable((BYTE*)rec + kRec_Attributes, 4))
            return pOrigCasterState(unit, errSink, rec, a3);

        bool ok;
        WITH_COMBAT_BIT_CLEARED(rec, ok = pOrigCasterState(unit, errSink, rec, a3));
        if (g_debug && (UnitFlagsOf(unit) & kUnitFlagInCombat))
            Log("second coming cast: spell=%d in combat aura=%d -> %s",
                *(int*)((BYTE*)rec + kRec_Id), scAura, ok ? "allowed" : "still refused");
        return ok;
    }

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

// ------------------------------------------------- Field Medic (shoot allies)
//
// The server's VioletHoldClassBoons::ShootsAllies, mirrored on the record:
// hunter family, ranged damage class, and every effect is a plain damage
// effect (SCHOOL_DAMAGE 2, WEAPON_DAMAGE_NOSCHOOL 17, WEAPON_PERCENT_DAMAGE 31,
// WEAPON_DAMAGE 58, NORMALIZED_WEAPON_DMG 121), at least one of them. So Auto
// Shot (58), Arcane Shot (2), Multi-Shot (121), Kill Shot (17+31), Aimed Shot
// rank 1 (121) qualify; stings, Concussive / Scatter / Tranquilizing Shot and
// anything with an aura or dummy effect do not - the server would refuse them
// on a friend, so the client must not light them up either.
static bool IsFieldMedicShot(const void* rec)
{
    if (!rec || !Readable((void*)rec, kRec_DmgClass + 4))
        return false;
    const BYTE* r = (const BYTE*)rec;
    if (*(const int*)(r + kRec_SpellFamilyName) != kFamilyHunter ||
        *(const int*)(r + kRec_DmgClass) != kDmgClassRanged)
        return false;

    bool hits = false;
    const int* effects = (const int*)(r + kRec_Effect1);
    for (int i = 0; i < 3; ++i) {
        switch (effects[i]) {
            case 0:
                break;
            case 2: case 17: case 31: case 58: case 121:
                hits = true;
                break;
            default:
                return false;
        }
    }
    return hits;
}

// OBJECT_FIELD_TYPE & TYPEMASK_UNIT - the same test the acceptor makes at
// 0x0080BD3A ([obj+8] = descriptor, +8 = type mask).
static bool IsUnitObject(void* obj)
{
    if (!obj || !Readable((BYTE*)obj + 8, 4)) return false;
    BYTE* desc = *(BYTE**)((BYTE*)obj + 8);
    if (!desc || !Readable(desc + 8, 4)) return false;
    return (*(DWORD*)(desc + 8) & kTypeMaskUnit) != 0;
}

// Throttled debug line: the acceptor is also asked by the spell cursor, so a
// held cursor over a friend would otherwise log every frame.
static void LogFieldMedic(int spellId, bool ok, int aura)
{
    static int   s_lastSpell;
    static bool  s_lastOk;
    static DWORD s_lastTick;
    DWORD now = GetTickCount();
    if (spellId == s_lastSpell && ok == s_lastOk && now - s_lastTick < 2000)
        return;
    s_lastSpell = spellId;
    s_lastOk    = ok;
    s_lastTick  = now;
    Log("field medic: spell=%d friendly target -> %s aura=%d",
        spellId, ok ? "accepted" : "still refused (range / dead / not assistable)", aura);
}

// THE waiver. Only when the pending cast still wants exactly an enemy unit
// (0x80, and none of unit/raid/party/ally which would already take a friend),
// the object is a unit the caster can NOT attack, the spell is a Field Medic
// shot and the caster holds the boon: swap 0x80 for 0x100 for this one call.
// An attackable unit (any enemy, a duel partner) never gets here, so it is shot
// exactly as before.
//
// After the call: if 0x100 is still set, the ally branch did not take the unit
// (not assistable, dead, or the range check failed and restored the flags from
// entry) - put 0x80 back so the pending cast is exactly what it was. If 0x100
// is gone the ally branch accepted it, which also cleared what 0x80 would have,
// and - when nothing else was needed - already sent the cast.
static bool __cdecl TryTargetWrapper(void* obj, void* castTargets)
{
    if (!g_enabled || !g_fieldMedicAuraCount)
        return pOrigTryTarget(obj, castTargets);

    DWORD* pFlags = (DWORD*)kCastTargetFlags;
    DWORD  flags  = *pFlags;
    const DWORD takesFriend = kTargetFlagUnit | kTargetFlagRaid | kTargetFlagParty | kTargetFlagUnitAlly;
    if (!(flags & kTargetFlagUnitEnemy) || (flags & takesFriend))
        return pOrigTryTarget(obj, castTargets);

    BYTE* ct = (BYTE*)(castTargets ? castTargets : *(void**)kPendingCast);
    if (!ct || !Readable(ct, kCast_SpellId + 4) || !IsUnitObject(obj))
        return pOrigTryTarget(obj, castTargets);

    int spellId = *(int*)(ct + kCast_SpellId);
    BYTE rec[kSpellRecSize + 8];
    if (!pSpellDbGet((void*)kSpellDb, spellId, rec) || !IsFieldMedicShot(rec))
        return pOrigTryTarget(obj, castTargets);

    void* caster = pObjectPtr(*(DWORD*)(ct + kCast_CasterGuid), *(DWORD*)(ct + kCast_CasterGuid + 4),
                              kTypeMaskUnit, "rulerelax", __LINE__);
    if (!caster)                                // self-target is fine: not attackable, assistable
        return pOrigTryTarget(obj, castTargets);
    if (pCanAttack(caster, obj))                // an enemy: the stock branch shoots it
        return pOrigTryTarget(obj, castTargets);

    int matchedAura = UnitHasAnyAura(caster, g_fieldMedicAuras, g_fieldMedicAuraCount);
    if (!matchedAura)
        return pOrigTryTarget(obj, castTargets);

    *pFlags = (flags & ~kTargetFlagUnitEnemy) | kTargetFlagUnitAlly;
    bool ok = pOrigTryTarget(obj, castTargets);
    DWORD after = *pFlags;
    if (after & kTargetFlagUnitAlly)
        *pFlags = (after & ~kTargetFlagUnitAlly) | kTargetFlagUnitEnemy;

    if (g_debug)
        LogFieldMedic(spellId, ok, matchedAura);
    return ok;
}

// Range tint on a friend. Adding UNIT_ALLY to the record's Targets is enough
// here: the ally branch is tested BEFORE the enemy branch, so an enemy still
// fails the ally test and reaches CanAttack exactly as before, and a unit that
// is both (duel partner) is accepted either way by the same range check. Only
// a friend changes outcome: "no answer" becomes in range / out of range.
static bool __cdecl TargetCheckWrapper(void* caster, void* rec, DWORD guidLo, DWORD guidHi,
                                       void* inRange, int showError, void* errSink)
{
    if (!g_enabled || !g_fieldMedicAuraCount || !caster || !IsFieldMedicShot(rec) ||
        !Writable((BYTE*)rec + kRec_Targets, 4) ||
        !UnitHasAnyAura(caster, g_fieldMedicAuras, g_fieldMedicAuraCount))
        return pOrigTargetCheck(caster, rec, guidLo, guidHi, inRange, showError, errSink);

    DWORD* pTargets = (DWORD*)((BYTE*)rec + kRec_Targets);
    DWORD  saved    = *pTargets;
    *pTargets = saved | kTargetFlagUnitAlly;
    bool ok = pOrigTargetCheck(caster, rec, guidLo, guidHi, inRange, showError, errSink);
    *pTargets = saved;
    return ok;
}

// Usability predicate, Field Medic half. 0x0080B987 only looks at the target
// when the spell has a target aura requirement (rec+0x54/0x5C/0x64/0x6C) -
// among Field Medic shots that is Kill Shot (TargetAuraState 2, health < 20%).
// Then 0x0080BAC4 demands CanAttack for implicit target 6, so Kill Shot dims on
// a friend even below 20%. Swapping the implicit target to 21 (TARGET_UNIT_
// TARGET_ALLY) for the call makes 0x0080B9E2 keep the friend as the unit whose
// aura state is read and 0x0080BADD ask CanAssist instead. Only done when the
// selected unit is assistable and not attackable, so for an enemy (and for a
// neutral that is neither) the predicate runs untouched.
static bool FieldMedicUsableSwap(void* unit, void* rec, void* guidPtr)
{
    if (!g_fieldMedicLive || !g_fieldMedicAuraCount || !unit || !IsFieldMedicShot(rec))
        return false;
    const BYTE* r = (const BYTE*)rec;
    if (*(const int*)(r + kRec_ImplicitTargetA1) != kTargetUnitTargetEnemy)
        return false;
    if (!*(const int*)(r + kRec_TargetAuraState) && !*(const int*)(r + kRec_TargetAuraStateNot) &&
        !*(const int*)(r + kRec_TargetAuraSpell) && !*(const int*)(r + kRec_ExcludeTargetAura))
        return false;                            // the predicate never looks at the target
    if (!Writable((BYTE*)rec + kRec_ImplicitTargetA1, 4))
        return false;

    DWORD lo = 0, hi = 0;
    if (guidPtr && Readable(guidPtr, 8)) {
        lo = ((DWORD*)guidPtr)[0];
        hi = ((DWORD*)guidPtr)[1];
    }
    if (!lo && !hi) {                           // same fallback as 0x0080B9B0
        lo = ((DWORD*)kCurrentTarget)[0];
        hi = ((DWORD*)kCurrentTarget)[1];
    }
    if (!lo && !hi)
        return false;
    void* target = pObjectPtr(lo, hi, kTypeMaskUnit, "rulerelax", __LINE__);
    if (!target || pCanAttack(unit, target) || !pCanAssist(unit, target, 0))
        return false;
    return UnitHasAnyAura(unit, g_fieldMedicAuras, g_fieldMedicAuraCount) != 0;
}

// The predicate prints nothing, but it is asked for every button on every
// usability update, so it logs nothing either. Shared by "untamed",
// "secondcoming" and "fieldmedic" (druid / priest / hunter family, so at most
// one applies per call).
static bool __cdecl UsableWrapper(void* unit, void* rec, void* a3, void* a4, void* a5)
{
    int matchedAura;
    if (UntamedApplies(unit, rec, &matchedAura)) {
        DWORD* pEx5  = (DWORD*)((BYTE*)rec + kRec_AttributesEx5);
        DWORD  saved = *pEx5;
        *pEx5 = saved | kEx5UsableWhileControlled;
        bool ok = pOrigUsable(unit, rec, a3, a4, a5);
        *pEx5 = saved;
        return ok;
    }

    if (SecondComingHolder(unit, rec) && Writable((BYTE*)rec + kRec_Attributes, 4)) {
        bool ok;
        WITH_COMBAT_BIT_CLEARED(rec, ok = pOrigUsable(unit, rec, a3, a4, a5));
        return ok;
    }

    if (FieldMedicUsableSwap(unit, rec, a3)) {
        int* pImplicit = (int*)((BYTE*)rec + kRec_ImplicitTargetA1);
        int  saved     = *pImplicit;
        *pImplicit = kTargetUnitTargetAlly;
        bool ok = pOrigUsable(unit, rec, a3, a4, a5);
        *pImplicit = saved;
        return ok;
    }

    return pOrigUsable(unit, rec, a3, a4, a5);
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

    // Must match SPELL_FIELD_MEDIC in VioletHoldClassBoons.h.
    GetPrivateProfileStringA("RuleRelax", "FieldMedicAura", "94383", buf, sizeof(buf), ini);
    ParseSpellList(buf, g_fieldMedicAuras, &g_fieldMedicAuraCount, kMaxAuras);

    // Second Coming charge aura (server: the Violet Hold priest boon).
    GetPrivateProfileStringA("RuleRelax", "SecondComingAura", "94386", buf, sizeof(buf), ini);
    ParseSpellList(buf, g_secondComingAuras, &g_secondComingAuraCount, kMaxAuras);
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
    if (!g_enabled || (!g_weaponAuraCount && !g_pointBlankAuraCount && !g_untamedAuraCount &&
                       !g_fieldMedicAuraCount && !g_secondComingAuraCount))
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
        if (!g_pointBlankLive)
            g_pointBlankAuraCount = 0;
        else {
            bool movingSigsOk =
                VerifySig(kCastTime,      kCastTimeSig,  sizeof(kCastTimeSig),  "cast-time") &&
                VerifySig(kGetActiveGuid, kGetActiveSig, sizeof(kGetActiveSig), "GetActivePlayerGuid") &&
                VerifySig(kObjectPtr,     kObjectPtrSig, sizeof(kObjectPtrSig), "ObjectPtr");
            if (movingSigsOk) {
                pGetActiveGuid = (GetActiveGuid_t)kGetActiveGuid;
                pObjectPtr     = (ObjectPtr_t)kObjectPtr;
                pOrigCastTime = (CastTime_t)InstallDetour(kCastTime, kCastTimeSig,
                    sizeof(kCastTimeSig), kCastTimeStolen, (void*)&CastTimeWrapper, "cast-time");
                g_pointBlankMovingLive = pOrigCastTime != NULL;
            }
            Log("pointblank rule installed at 0x%08X%s (%d aura(s), first %d)",
                kRangeCalc, g_pointBlankMovingLive ? " + 0x007FF180" : " [moving cast FAILED]",
                g_pointBlankAuraCount, g_pointBlankAuras[0]);
        }
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

    if (g_fieldMedicAuraCount) {
        // The acceptor is the refusal; everything it calls must match before
        // it is patched. The range pre-check and the usability predicate are
        // cosmetic (range tint, Kill Shot button) - each logs on its own if it
        // fails and the rule stays on.
        bool sigsOk =
            VerifySig(kTryTarget,  kTryTargetSig,  sizeof(kTryTargetSig),  "target-acceptor") &&
            VerifySig(kCanAttack,  kCanAttackSig,  sizeof(kCanAttackSig),  "CanAttack") &&
            VerifySig(kCanAssist,  kCanAssistSig,  sizeof(kCanAssistSig),  "CanAssist") &&
            VerifySig(kSpellDbGet, kSpellDbGetSig, sizeof(kSpellDbGetSig), "SpellDbGet") &&
            VerifySig(kObjectPtr,  kObjectPtrSig,  sizeof(kObjectPtrSig),  "ObjectPtr");
        if (sigsOk) {
            pCanAttack  = (CanAttack_t)kCanAttack;
            pCanAssist  = (CanAssist_t)kCanAssist;
            pSpellDbGet = (SpellDbGet_t)kSpellDbGet;
            pObjectPtr  = (ObjectPtr_t)kObjectPtr;
            pOrigTryTarget = (TryTarget_t)InstallDetour(kTryTarget, kTryTargetSig,
                sizeof(kTryTargetSig), kTryTargetStolen, (void*)&TryTargetWrapper, "target-acceptor");
            g_fieldMedicLive = pOrigTryTarget != NULL;
        }

        bool rangeLive = false, usableLive = false;
        if (g_fieldMedicLive) {
            pOrigTargetCheck = (TargetCheck_t)InstallDetour(kTargetCheck, kTargetCheckSig,
                sizeof(kTargetCheckSig), kTargetCheckStolen, (void*)&TargetCheckWrapper,
                "target-range-check");
            rangeLive = pOrigTargetCheck != NULL;

            // The usability predicate may already be detoured by "untamed";
            // UsableWrapper serves both rules, so patch it only once.
            if (!pOrigUsable)
                pOrigUsable = (Usable_t)InstallDetour(kUsable, kUsableSig, sizeof(kUsableSig),
                    kUsableStolen, (void*)&UsableWrapper, "usable-predicate");
            usableLive = pOrigUsable != NULL;
        }

        if (g_fieldMedicLive)
            Log("fieldmedic rule installed at 0x%08X%s%s (%d aura(s), first %d)",
                kTryTarget,
                rangeLive  ? " + 0x00809610" : " [range check FAILED]",
                usableLive ? " + 0x0080B5D0" : " [predicate FAILED]",
                g_fieldMedicAuraCount, g_fieldMedicAuras[0]);
        else
            g_fieldMedicAuraCount = 0;
    }

    if (g_secondComingAuraCount) {
        // Two halves that both matter: the combat waiver (caster-state check,
        // already detoured if "untamed" installed) and instant cast (the
        // cast-time calculator). Everything is verified before anything is
        // patched; the button (usability predicate) and the cast-start visual
        // (player virtual) are cosmetic and log on their own if they fail.
        bool sigsOk =
            (pOrigCastTime || VerifySig(kCastTime, kCastTimeSig, sizeof(kCastTimeSig), "cast-time")) &&
            VerifySig(kGetActiveGuid, kGetActiveSig,     sizeof(kGetActiveSig),     "GetActivePlayerGuid") &&
            VerifySig(kObjectPtr,     kObjectPtrSig,     sizeof(kObjectPtrSig),     "ObjectPtr") &&
            (pOrigCasterState ||
             VerifySig(kCasterState,  kCasterStateSig,   sizeof(kCasterStateSig),   "caster-state"));
        bool combatLive = false, castTimeLive = false, usableLive = false, visualLive = false;
        if (sigsOk) {
            pGetActiveGuid = (GetActiveGuid_t)kGetActiveGuid;
            pObjectPtr     = (ObjectPtr_t)kObjectPtr;

            if (!pOrigCasterState)
                pOrigCasterState = (CasterState_t)InstallDetour(kCasterState, kCasterStateSig,
                    sizeof(kCasterStateSig), kCasterStateStolen, (void*)&CasterStateWrapper,
                    "caster-state");
            combatLive = pOrigCasterState != NULL;

            if (combatLive) {
                if (!pOrigCastTime)
                    pOrigCastTime = (CastTime_t)InstallDetour(kCastTime, kCastTimeSig,
                        sizeof(kCastTimeSig), kCastTimeStolen, (void*)&CastTimeWrapper, "cast-time");
                castTimeLive = pOrigCastTime != NULL;
            }
        }
        if (combatLive && castTimeLive) {
            if (!pOrigUsable)
                pOrigUsable = (Usable_t)InstallDetour(kUsable, kUsableSig, sizeof(kUsableSig),
                    kUsableStolen, (void*)&UsableWrapper, "usable-predicate");
            usableLive = pOrigUsable != NULL;

            pOrigPlayerCastTime = (PlayerCastTime_t)InstallDetour(kPlayerCastTime,
                kPlayerCastTimeSig, sizeof(kPlayerCastTimeSig), kPlayerCastTimeStolen,
                (void*)&PlayerCastTimeWrapper, "player-cast-time");
            visualLive = pOrigPlayerCastTime != NULL;

            Log("secondcoming rule installed at 0x%08X + 0x%08X%s%s (%d aura(s), first %d)",
                kCasterState, kCastTime,
                usableLive ? " + 0x0080B5D0" : " [predicate FAILED]",
                visualLive ? " + 0x006D68D0" : " [player virtual FAILED]",
                g_secondComingAuraCount, g_secondComingAuras[0]);
        } else {
            // A half-installed rule would be worse than none (in combat but a
            // 10 s cast, or instant but refused): the caster-state detour may be
            // shared with untamed, so only the aura list is switched off -
            // every wrapper checks it first.
            if (combatLive && !castTimeLive)
                Log("secondcoming: cast-time hook FAILED - rule off");
            g_secondComingAuraCount = 0;
        }
    }
}
