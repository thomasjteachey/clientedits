# Violet Hold class boons — client-side waivers (Point Blank, Untamed, Field Medic, Second Coming)

Wow.exe 3.3.5a 12340. Code: `src/rulerelax.cpp` (rules `pointblank`,
`untamed`, `fieldmedic` and `secondcoming`, next to the existing `weapon` rule). Companion to
`RULE_RELAX_RE.md`, whose reasoning (DBC is global, auras are per-player, so the
waiver has to happen at runtime) applies unchanged.

All four refusals are client-local: the client never sends `CMSG_CAST_SPELL`,
so the server halves in `VioletHoldClassBoons` (`IgnoresMinRange` in
`Spell::GetMinMaxRange`, `ShiftsThroughControl` in `Spell::CheckCasterAuras`,
`IsFieldMedicHeal` in `SpellInfo::CheckExplicitTarget`, and the Second Coming
combat / cast-time waivers) are never asked. Both halves are needed. The server never trusts the client:
deleting the DLL grants nothing, and the DLL alone grants nothing either.

All addresses below were found by **byte scan** (call-target scan for
`E8 rel32`, raw displacement/immediate search, decoding backwards from each
hit), not by a linear sweep — see the tooling note in `RULE_RELAX_RE.md`.
Every signature in the code was re-checked against `clients\centurion\Wow.exe`
after the build (all six match; with Field Medic, twelve; with Second Coming, fifteen).

## How the errors were found

Cast errors are reported through one function:

| What | Address | Notes |
|---|---|---|
| Cast-error reporter | `0x00808200` | `__cdecl(sink, SpellRec*, errorId, p1, p2, 0)`, 98 call sites. The weapon check emits error 30 through it at `0x00809492`. |

Scanning its 98 call sites for the error id pushed as arg 3 gives exactly:

| error | id | site | owner |
|---|---|---|---|
| `SPELL_FAILED_STUNNED` | 108 `0x6C` | `0x00739B98` | `0x00739650` |
| `SPELL_FAILED_FLEEING` | 34 `0x22` | `0x00739CF6` | `0x00739650` |
| `SPELL_FAILED_CONFUSED` | 26 `0x1A` | `0x00739D73` | `0x00739650` |
| `SPELL_FAILED_TOO_CLOSE` | 128 `0x80` | `0x008099D4` | `0x00809610` |
| `SPELL_FAILED_TOO_CLOSE` / `OUT_OF_RANGE` | 128 / 97 | `0x0080C2DE` | range-tint path |
| `SPELL_FAILED_BAD_TARGETS` / `BAD_IMPLICIT_TARGETS` | 12 / 11 | `0x0080D755` (12 pushed at `0x0080D7E1`, 11 at `0x0080D74B`) | `0x0080CCE0`, after the acceptor `0x0080BC80` refused the unit (Field Medic) |
| `SPELL_FAILED_AFFECTING_COMBAT` | 1 `0x01` | `0x0073A19B` | `0x00739650` (Second Coming) |
| `SPELL_FAILED_MOVING` | 51 `0x33` | `0x0073A0DD`, `0x0080B0A6` | `0x00739650`, send `0x0080AC90` — both from cast time > 0 |

## In-memory SpellRec is not the raw DBC row past field 135

`RULE_RELAX_RE.md` says the record is "raw DBC order, field × 4". That holds
only up to field 135. Each localized string (name, rank, description, tooltip)
is **one pointer** in memory instead of 17 DBC fields, so every field ≥ 204 sits
64 fields (0x100 bytes) earlier. Record size `0x2A8` = (234 − 64) × 4, the
copy buffer size in `0x00802C30`.

| field | DBC index | in-memory offset | verified by |
|---|---|---|---|
| `AttributesEx5` | 9 | `+0x024` | `0x00739B45` tests `0x8`, `0x0080B708` tests `0x20000` |
| `RangeIndex` | 46 | `+0x0B8` | `0x007FF4C0` indexes SpellRange with it |
| `EffectApplyAuraName1..3` | 95..97 | `+0x17C..+0x184` | raw layout (< 136) |
| `SpellFamilyName` | 208 | **`+0x240`** | spellmod family match `0x007FDEC0` compares two records' `+0x240`, then their three family-flag words at `+0x244` |
| `PreventionType` | 214 | **`+0x258`** | `0x00739BAB` `cmp [esi+0x258], 1` → silence |
| `DmgClass` | 213 | **`+0x254`** | `0x00805DE7` reads `[esi+0x254]` and skips GCD haste for 2 (melee) / 3 (ranged); the only `+0x254` read in the spell code |
| `Targets` | 16 | `+0x040` | read as a WORD at `0x00809653`, as a dword at `0x0080C7A5` |
| `TargetAuraState` / `…Not` | 21 / 23 | `+0x054` / `+0x05C` | `0x0080B987`, `0x00803534` |
| `Effect1..3` | 71..73 | `+0x11C..+0x124` | `0x0080368D` compares them with 38 / 108 / 126 (dispel effects) |
| `Attributes` | 4 | `+0x010` | `0x0073A169`, `0x0080B89A` test `0x10000000` |
| `CastingTimeIndex` | 28 | `+0x070` | `0x007FF195`, `0x006D68EB`, `0x0071AD38` |
| `EffectImplicitTargetA1` | 86 | `+0x158` | `0x0080C79F` (switch), `0x0080BAC4` `cmp …, 6` |

## Point Blank — minimum range and moving shots (aura 94346)

The minimum range is computed in exactly one place that a gate reaches:

```
0x00803850  in-range test, register ABI: eax=target, edi=caster,
            edx=spellId, ebx=&tooFar.  dist² < min² -> false, tooFar=0
            (= TOO_CLOSE); dist² > max² -> false, tooFar=1 (= OUT_OF_RANGE)
  -> 0x00802C30  __cdecl(caster, spellId, float* min, float* max, target)
                 fetches the record copy (0xAD49D0 / 0x004CFD20)
    -> 0x00801650  __cdecl(caster, rec, min*, max*, target): friendly/hostile
      -> 0x007FF480  __cdecl(caster, rec, min*, max*, friendly, target)
                     THE calculator: SpellRange row (rec+0xB8), melee and
                     combat-reach cases. Caller cleans 0x18.
```

`0x00803850` has two callers: `0x00809610` (target/range check on the cast
path, called at `0x0080D6AD` with showError = 1) and `0x0080C2B1` (the range
tint / `IsSpellInRange` path). `0x007FF480` has one more pair of callers,
`0x00623F8E` / `0x00623FA7`, in the tooltip builder `0x006238A0`.

**Hook:** detour `0x007FF480`; after the original returns, if
`*min > 0` and `rec+0x240 == 9` (hunter) and the caster holds 94346, write
`*min = 0`. Every consumer then agrees — the cast goes out, the button is not
red at melee range, and the tooltip says "35 yd range" rather than "5-35 yd"
for the holder only.

Not hooked, on purpose: `0x007FF400` reads the SpellRange minimum directly but
is only called from `0x00540CFB` (a Lua getter) and the tooltip builder —
display, not a gate.

Prologue stolen: `55 / 8B EC / 8B 55 1C` (6 bytes). Signature checked:
`55 8B EC 8B 55 1C 83 EC 08 85 D2`.

## Point Blank — the client's cancel on movement start (2026-11-18)

Owner report: a Point Blank Aimed Shot begun on the run was cancelled by
switching direction. The server allows the whole cast (Spell::update waives
the movement interrupt via VioletHoldClassBoons::ShootsWhileMoving); the cancel
is the client's own:

```
0x005FAA40  movement handler for the active player (GUID == [0xCA1238/C])
  0x005FAA83  call 0x00806620
0x00806620  __cdecl void(): walk the pending-spell list [0x00AF5254]
            (skips [0x00D3F4E4]), next = [spell+4], id = [spell+0x20];
            copy the record (rows [0x00AD49F0], or 0x004CFBB0 when
            [0x00C5DEA0] is set) to [ebp-0x2AC]
  0x008066C5  test byte ptr [ebp-0x230], 1   ; InterruptFlags (+0x7C) MOVEMENT
  0x008066CC  je 0x008066DD                  ; next spell
  0x008066CE  push 0x28 / push 1 / push 0 / push ebx
  0x008066D5  call 0x00806200                ; cancel (reason 0x28) + CMSG_CANCEL_CAST
0x00806200  the cancel routine itself (11 callers; 0x00806620 is the only
            movement one). It also sends 0x12F itself when the spell's cast
            time (vtable +0x148) is > 0.
```

A cast started while already moving survives until the next movement START
(direction change, strafe, jump...) because only a start event reaches
0x00806620.

**Hook:** `jmp` over the 9 bytes at `0x008066C5` (signature
`F6 85 D0 FD FF FF 01 74 0F 6A 28`) to a naked stub that redoes the bit test and
asks `PointBlankKeepsMovingCast(&copy)`; a holder's Aimed Shot / Multi-Shot
goes to `0x008066DD` (kept), everything else to `0x008066CE` (cancelled as
before). eax/ecx/edx are dead on both exits. Log line: `pointblank rule
installed at 0x007FF480 + 0x007FF180 + 0x008066C5`.

## Untamed — shapeshift through stun / fear / confuse (aura 94361)

### The cast path: `0x00739650`

`__thiscall(unit, errSink, SpellRec*, a3) -> bool in al`, `ret 0xC`. **One
caller**, `0x0080D6D1` in the cast function `0x0080CCE0`, which passes its own
stack copy of the record (`[ebp-0x2B8]`). It is the client's CheckCasterAuras:

| block | condition | exemption | error |
|---|---|---|---|
| `0x00739B33` | UNIT_FLAGS `0x40000` stunned | Ex5 `0x8`, Ex7 `0x100000`, or mechanic test `0x00804590` | 108 (or 147 by mechanic) |
| `0x00739BAB` | PreventionType 1 + `0x2000` silenced | mechanic test | 104 — **not waived** |
| `0x00739C1D` | PreventionType 2 + `0x20000` pacified | mechanic test | 98 — **not waived** |
| `0x00739C90` | UNIT_FLAGS `0x800000` fleeing | Ex5 `0x20000`, Ex7 `0x100000`, mechanic | 34 |
| `0x00739D09` | UNIT_FLAGS `0x400000` confused | Ex5 `0x40000`, Ex7 `0x100000`, mechanic | 26 |

(UNIT_FLAGS = unit descriptor `[unit+0xD0]` `+0xD4`.)

### The button: the usability predicate `0x0080B5D0`

`0x0080B6D6` tests the global **`0x00BCFB8C` = "player has control"**. It is
written only by `0x00520FE0` (`SetControl(int)`, which also fires the control
gained/lost events) and set to 1 on world entry. Fear and confuse clear it; stun
does not. While it is 0, `0x0080B6E4..0x0080B7A7` reports the spell unusable
unless Ex5 says usable-while-feared (`0x20000`) / -confused (`0x40000`) for the
state you are in, or one of two charm/taxi cases. So while feared or
polymorphed the form buttons dim (`IsUsableAction`, `0x005A9E20`, asks this
predicate at `0x005AA1F7`).

The pet-bar path `0x005D4210` has its own copy of these tests (counts
stunned/fleeing/confused pets); it is not involved in a player's own cast.

### Hook

Detour both. In each wrapper, when the record is druid family
(`rec+0x240 == 7`), has `36` (MOD_SHAPESHIFT) in any of
`EffectApplyAuraName1..3`, and the unit holds 94361: OR
`0x8 | 0x20000 | 0x40000` (the client's own SPELL_ATTR5_USABLE_WHILE_STUNNED /
_FEARED / _CONFUSED) into the record's `AttributesEx5`, call the original,
restore. This is the same predicate the server's `ShiftsThroughControl` uses.

Why set bits rather than ask twice (the weapon rule's differential approach):
`0x00739650` reports its error the moment a check fails, so a first "real" ask
would already have printed "Can't do that while stunned". Setting the bits
waives exactly those three blocks and nothing else — silence and pacify still
refuse. The record is always a private copy (the Spell DB only hands out copies
via `0x004CFD20`); the wrapper still confirms the page is writable first.

Prologues stolen: `0x00739650` `55 / 8B EC / 81 EC BC 02 00 00` (9 bytes);
`0x0080B5D0` `55 / 8B EC / 83 EC 10` (6 bytes). Both signatures are verified
before either is patched.

Note the server is broader than the client here: `ShiftsThroughControl` returns
`SPELL_CAST_OK` from `CheckCasterAuras` outright, so the server would also let a
form through silence. Druid forms have PreventionType 0, so silence never
blocked them anyway, and the client keeps its own silence/pacify checks.

## Field Medic — hunter shots at a friendly unit (aura 94383)

Server half: `VioletHoldClassBoons::ShootsAllies` / `IsFieldMedicHeal`. Without
the client half the server is never asked: the client prints "Invalid target"
and sends no `CMSG_CAST_SPELL`.

### Which spells

The client mirrors the server's `ShootsAllies` exactly, not just family +
damage class: `rec+0x240 == 9`, `rec+0x254 == 3`, and every non-zero
`Effect1..3` is a plain damage effect (2, 17, 31, 58, 121), at least one.
From `centurion\dbc\Spell.dbc` that is Auto Shot 75 (58), Arcane Shot (2),
Multi-Shot 2643… (121), Kill Shot 53351… (17 + 31), Aimed Shot rank 1 19434
(121), plus NPC Shoot/Throw variants. Concussive / Scatter / Tranquilizing
Shot, stings, Chimera, Steady Shot (dummy effect) and the higher Aimed Shot
ranks (an aura effect) are left alone, because the server refuses them on a
friend and a lit button followed by a server error is worse than a dim one.
Every player shot in the set has `ImplicitTargetA1 = 6`, `Targets = 0`.

### Where the refusal is

The cast function `0x0080CCE0` reaches it like this:

```
0x0080D6AD  0x00809610  explicit-target + range pre-check      (not the refusal)
0x0080D6D1  0x00739650  caster state
0x0080D727  0x0080C790  target selection:
              flags = rec.Targets | map(ImplicitTargetA)  -> global 0x00D3F4E0
              implicit 6 -> TARGET_FLAG_UNIT_ENEMY 0x80, 21 -> UNIT_ALLY 0x100,
              25 -> UNIT 0x2   (jump table 0x0080CC44 / byte map 0x0080CC78)
              guid 0 -> current target 0x00BD07B0
            0x0080BC80  THE unit-target acceptor (obj, CastTargets*)
0x0080D7A6  if 0x0080C790 failed and flags & 0x80:
              target selected -> error 12 SPELL_FAILED_BAD_TARGETS (0x0080D7E1)
              none            -> error 11                            (0x0080D74B)
            both reported through 0x00808200 at 0x0080D755
```

`0x0080BC80` (`__cdecl(obj, ct)`, `ct == NULL` = pending cast `0x00D3F4E4`)
copies the record itself (`ct+0x20` = spell id, `ct+0x10` = caster guid) and
walks the still-needed flags in `0x00D3F4E0`:

| flag | test | site |
|---|---|---|
| `0x8` party | `0x00718CA0` in party + CanAssist | `0x0080BE83` |
| `0x4` raid | `0x00718D70` in raid + CanAssist | `0x0080BEDD` |
| `0x100` ally | CanAssist `0x007293D0(caster, unit, Ex6>>3 & 1)` | `0x0080BF15` |
| `0x80` enemy | **CanAttack `0x00729740(caster, unit)`** | `0x0080BF54` |
| `0x2` any unit | not `UNIT_FLAGS 0x100` (two attribute exemptions) | `0x0080BF94` |

The accepting branch clears its bit, stores the guid, runs the range check
(`0x00803850`, errors 97 / 128 at `0x0080C2DE`, flags restored from entry on
failure), and when no bits remain sends the cast (`0x0080AC90`, opcode
`0x12E` at `0x0080B2F5`). A friend under `0x80` alone matches nothing and the
function returns 0 silently. The dead-target test (`0x0080BE2F`, health ≤ 0
needs flag `0x8600` or Ex2 `0x1`) runs before the branches and does not look
at `0x80` / `0x100`.

Nine callers, all covered by one hook: the cast path (`0x0080CBC1`,
`0x0080CC0C`, `0x0080CC2C` in `0x0080C790`), clicking a unit with the spell
cursor up (`0x00524CE9` in the target handler `0x00524BF0`), `0x0080DBF4`,
`0x0080DD9C`, `0x005167C8`, `0x00521504` (pet), `0x007118D9`.

The send function's own pre-check `0x00803500` (its errors 111 / 110 are
reported at `0x0080AE53`) only tests target aura states and dispel effects
(38 / 108 / 126) — no reaction test for these spells.

### Hook 1 (the waiver): detour `0x0080BC80`

When the pending flags have `0x80` and none of `0x2 | 0x4 | 0x8 | 0x100`, the
object is a unit, the record is a Field Medic shot, the caster (resolved by
`ObjectPtr 0x004D4DB0` from `ct+0x10`, type mask 8) can **not** attack it
(CanAttack false) and holds 94383: replace `0x80` with `0x100` for the call.
The client's own ally branch then decides (CanAssist, then the normal range
check, then the send). Afterwards, if `0x100` is still set (not assistable,
dead, out of range — the range failure restores the entry flags), put `0x80`
back so the pending cast is exactly what it was. An attackable unit (every
enemy, a duel partner) never takes the swap and is shot as before.

Prologue stolen: `55 / 8B EC / 81 EC B0 02 00 00` (9 bytes). Signature
`55 8B EC 81 EC B0 02 00 00 56 8B 75 0C`.

### Hook 2 (range tint): detour `0x00809610`

`__cdecl(caster, rec, guidLo, guidHi, bool* inRange, showError, errSink)`,
caller cleans `0x1C`. Same flag walk, locally from `rec+0x40` (WORD): party
`0x008097EA`, raid `0x0080980D`, ally `0x00809830` (CanAssist at
`0x0080984B`), enemy `0x00809854` (CanAttack at `0x0080985B`), any unit
`0x00809864`. Returns 0 = "not a target this spell takes", without an error.
On the cast path (`0x0080D6AD`) a 0 only skips to the acceptor, so this is
**not** the refusal; but the action bar's `IsActionInRange` (`0x005A9596`) and
Lua `IsSpellInRange` (`0x00541D6E`) return nil on 0, so a friend gets no range
tint at all. All five callers pass a stack copy of the record (also
`0x0051CAB2`, `0x005E55B6`).

Hook: for a Field Medic shot from a holder, OR `0x100` into `rec+0x40` for the
call. The ally branch runs before the enemy branch, so an enemy still fails it
and reaches CanAttack as before; only a friend changes, from "no answer" to in
or out of range. Prologue stolen: `55 / 8B EC / 51 / 8B 45 10` (7 bytes).
Signature `55 8B EC 51 8B 45 10 8B 4D 14 8B D0`.

No branch anywhere in `.text` targets the inside of either stolen prologue
(raw scan of every `E8/E9/EB/7x/0F 8x`).

### Hook 3 (Kill Shot button): the usability predicate `0x0080B5D0`

Shared with Untamed (one detour, one wrapper; druid vs hunter family, never
both in one call). `0x0080B987` only looks at the target when the spell has a
target aura requirement (`+0x54 / +0x5C / +0x64 / +0x6C`); among Field Medic
shots that is only Kill Shot (`TargetAuraState` 2, below 20 %). Then
`0x0080BAC4` demands CanAttack for implicit target 6, so Kill Shot stays dim
on a friend under 20 %. Wrapper: when the selected unit (`*a3`, else
`0x00BD07B0`) is assistable and not attackable and the caster holds 94383, set
`rec+0x158` to 21 (TARGET_UNIT_TARGET_ALLY) for the call — `0x0080B9E2` keeps
the friend as the unit whose aura state is read and `0x0080BADD` asks
CanAssist. Every other shot never enters that block. Cosmetic only: the cast
path does not call the predicate.

### Helpers called (not patched), all signature-checked before hook 1 installs

| what | address | signature |
|---|---|---|
| CanAttack `__thiscall(unit, target)`, `ret 4` | `0x00729740` | `55 8B EC 53 8B D9 8B 43 08` |
| CanAssist `__thiscall(unit, target, int)`, `ret 8` | `0x007293D0` | `55 8B EC 53 8B 5D 08 8B 83 D0 00 00 00` |
| Spell DB copy `__thiscall(0x00AD49D0, id, buf)`, `ret 8`, 0x2A8 bytes | `0x004CFD20` | `55 8B EC 8B 51 10 8B 45 08 3B C2` |
| ObjectPtr `__cdecl(lo, hi, mask, file, line)`; file/line never read | `0x004D4DB0` | `55 8B EC 64 8B 0D 2C` |

The client brackets the record copy with `0x008B7DA0` / `0x005EEB70`, which
are `mov eax, ecx; ret` and `ret` — nothing to replicate.

### Multi-Shot

Nothing Multi-Shot-specific on the client. Its primary target goes through the
same three paths as Arcane Shot (`ImplicitTargetA1 = 6`, `Targets = 0`; its Ex5
`0x8000` is not tested on these paths, which test Ex5 `0x800`). The client
never reads `EffectChainTarget` (`+0x1A0..+0x1A8`): the only `+0x1A0/4/8`
accesses in `0x007F0000–0x00812000` are float stores into another struct at
`0x007F0F1E`. The jumps are picked by the server.

### Auto Shot (auto-repeat)

Auto Shot is cast through the same `0x0080CCE0` path (Ex2 `0x20` only skips
the spell-queue block at `0x0080CE0E`), so hook 1 is all it needs to start on a
friend. The repeating is server-driven. The client stops auto-repeat
(`0x00807560`, sends `CMSG_CANCEL_AUTO_REPEAT_SPELL 0x26D` at `0x00807581`)
only on: target change with the stop-on-target-change cvar (`0x00524F67`), the
current target going away (`0x0071EECB`), certain cast-failure packets
(`0x00809D69`), Lua stop (`0x00809F4D`), melee swing start (`0x006E285A`) and a
few UI paths — none of them a hostility test. If Auto Shot starts on a friend
and stops after one shot, the stop comes from the server.

### Not hooked, on purpose

- `0x00729740` CanAttack itself: ~40 callers (tooltips, nameplates, interaction,
  the usability predicate); faking it would make friends look attackable
  everywhere.
- `0x0080C790` target selection: its friendly fallback (`0x0080CBF9`, refuse
  rather than auto-assist onto a friend's target) is never reached once the
  acceptor takes the friend.
- The range calculator: friendly vs hostile range is picked by `0x00801650`
  from the real reaction, and hunter ranges are the same both ways.

### Debug

`field medic: spell=… friendly target -> accepted | still refused (…) aura=…`
(at most one line per spell/result per 2 s; the acceptor is also asked by the
spell cursor). Install line: `fieldmedic rule installed at 0x0080BC80 +
0x00809610 + 0x0080B5D0 (…)`, with `[range check FAILED]` / `[predicate
FAILED]` if a cosmetic half did not patch.

## Second Coming — Resurrection in combat, instant (aura 94386)

Priest charge aura (one per wave). Server half: waives the combat check, makes
the cast instant, full-health resurrect data, consumes the charge. Without the
client half the cast is refused locally with "You can't do that while in
combat" and never sent.

### Which spells

`rec+0x240 == 6` (priest) and an effect 113 (`SPELL_EFFECT_RESURRECT_NEW`) in
any of `Effect1..3` (`+0x11C..+0x124`). In `centurion\dbc\Spell.dbc` that is
Resurrection 2006 / 2010 / 10880 / 10881 / 20770 / 25435 / 48171 (Attributes
`0x10010000`, CastingTimeIndex 7 = 10 s, InterruptFlags `0xF`), and by the same
predicate three spells no player has: 21074 "Test NPC Resurrection", 32841
"Mass Resurrection", 51035 "Gan'jo's Resurrection". No spellmod: family flag
`0x40000000` is shared with Shackle Undead, Lightwell, Dispel Magic, and the
stock percentage cast-time mod skips casts ≥ 10 s.

### In combat: error 1, not 3

`SPELL_FAILED_AFFECTING_COMBAT` is **1** in 3.3.5 (`SharedDefines.h`). A byte
scan for every `test …, 0x10000000` and every `mov r32, 0x10000000` in the
spell code finds `SPELL_ATTR0_CANT_USED_IN_COMBAT` read from a spell record in
exactly two places:

| site | function | condition | result |
|---|---|---|---|
| `0x0073A169` | caster-state check `0x00739650` (cast path, called at `0x0080D6D1`) | `Attributes & edi` (edi = `0x10000000` from `0x0073A132`), UNIT_FLAGS `0x80000` in combat, and not exempted by an aura of type 262 (`0x0072AAF0`) | error 1 at `0x0073A19B` |
| `0x0080B89A` | usability predicate `0x0080B5D0` | same test | button dims |

Both get the caller's private record copy. The other `0x10000000` tests in
that range are on `[unit+0xA30]` (`0x0072EC3A`, `0x0080AF0A`…), on other
record fields (Ex3 at `0x00803CA1` / `0x0080BFAE`, Ex4 at `0x00807280`, and
`+0x14` / `+0x24` in `0x0053D580`), or in other structs. The cast function and
the send function do not test the bit.

One more reader exists and is **not hooked**: `0x0071210E` in `0x00712080`
(a virtual, vtable slot `0x00A340A4`, `ret 0xC`). It fetches its own record
copy for a spell id taken from a per-class table (`[unit+0x1A4]`, index from
`0x00746190(class byte, 10)`), and refuses silently (returns 0, no error) if
that spell is CANT_USED_IN_COMBAT and the player is in combat. It is not on the
`CastSpell` → `0x0080CCE0` path and its purpose was not identified; if a
Resurrection triggered some other way (e.g. a UI interaction rather than the
spell button) still refuses in combat, look here first.

**Hook:** in the existing caster-state and usability wrappers (shared with
Untamed and Field Medic; druid / priest / hunter, never two in one call),
clear `0x10000000` in the record copy's `Attributes` (`+0x10`) for the call and
restore it. Nothing else in either function reads that bit.

### Instant: who computes cast time

`SpellCastTimes` (DB object `0x00AD4748`; vtable `0x00A289B0` holds its loader
`0x0064E1C0`, which names `DBFilesClient\SpellCastTimes.dbc` via `0x008B72A0`)
is read inline through `0x00AD4754 / 58 / 68` from `rec+0x70` in exactly three
functions:

| function | what | callers | covered |
|---|---|---|---|
| `0x007FF180` `__cdecl(rec, a2, a3, a4)` → ms | the active player's cast time: base + per-level, CASTING_TIME spellmods (`0x007FDB50` type 10), haste (`desc+0x128`), `a2 != 0` = for the charm/pet | caster-state move check `0x0073A0BB` (error 51 MOVING at `0x0073A0DD`), send function `0x0080B081` (MOVING again at `0x0080B0A6`), local cast prediction `0x007FA450` (from `0x0080B406`, writes `0x00D39798..A8`), `0x00809FD5`, Lua `GetSpellInfo` `0x00540C6A`, tooltip builder `0x00624471` ("Instant" vs "10 sec cast") | **yes** |
| `0x006D68D0` CGPlayer virtual, vtable `0x00A326C8 + 0x148`, `__thiscall(unit, rec)` → ms | same, per unit | the cast-start visual `0x0073B140` at `0x0073B275` (instant or not → precast animation) | **yes** |
| `0x0071AD20` CGUnit virtual, vtable `0x00A34D90 + 0x148` | creature version | same visual for NPCs | no — a holder is always a player |

**Hooks:** detour `0x007FF180` (6 bytes `55 / 8B EC / 83 EC 0C`, signature
`55 8B EC 83 EC 0C 57 8B 7D 08 85 FF`): run the original, and when it is > 0,
`a2 == 0`, the spell is in scope and the active player (`0x004D3790` +
ObjectPtr) holds 94386, return 0. Detour `0x006D68D0` (6 bytes, signature
`55 8B EC 83 EC 0C 53 8B 5D 08 85 DB`): same, for the unit passed in.

The cast-time hooks are used instead of setting CastingTimeIndex to 1 on the
cast-path copy because the send function `0x0080AC90` refetches its own copy
(`0x0080AD96`) and runs its own moving check on it; patching the cast
function's copy would not reach it. Returning 0 also means no table row has to
be trusted to be 0 ms.

The cast bar itself is drawn from `SMSG_SPELL_START` (server cast time). With
the server half instant there is no bar; without it the client sends at once
and the server still runs a 10 s cast. **Correction (2026-11-18):** moving does
not only matter through the two error-51 checks above. The client also cancels
its own in-flight casts on every new movement start - see "Point Blank — the
client's cancel on movement start" below.

### Install

Verified before anything is patched: `0x007FF180`, GetActivePlayerGuid
`0x004D3790` (`64 8B 0D 2C 00 00 00 A1`), ObjectPtr, and `0x00739650` unless
Untamed already detoured it. The combat waiver and the cast-time hook must both
install or the rule switches itself off (a refused cast and a 10 s in-combat
cast are both worse than stock). The button and the cast-start visual are
cosmetic and log `[predicate FAILED]` / `[player virtual FAILED]` on their
own. No branch in `.text` targets the inside of either new stolen prologue.

### Debug

`second coming: spell=… cast time 10000 ms -> instant aura=…` (throttled,
tooltips ask every frame) and `second coming cast: spell=… in combat aura=… ->
allowed|still refused` (only when you are actually in combat). Install line:
`secondcoming rule installed at 0x00739650 + 0x007FF180 + 0x0080B5D0 +
0x006D68D0 (…)`.

## Settings (`[RuleRelax]` in AnimSpeedFix.ini)

| key | default (compiled in) | meaning |
|---|---|---|
| `Enabled` | 1 | whole module |
| `PointBlankAura` | `94346` | aura ids; empty = rule off |
| `UntamedAura` | `94361` | aura ids; empty = rule off |
| `FieldMedicAura` | `94383` | aura ids; empty = rule off |
| `SecondComingAura` | `94386` | aura ids; empty = rule off |
| `Debug` | 0 (repo ini: 1) | log waivers to `AnimSpeedFix.log` |

Each rule installs on its own; a signature mismatch logs and disables only that
rule. Both auras are visible (Attributes `0x80000000`, not PASSIVE/HIDDEN), the
same as Bare Knuckle 90217, so the client has them in its aura array.

Debug lines: `relaxed min range: spell=… min=… -> 0` (throttled, it runs per
button per update) and `untamed cast: spell=… unitFlags=… -> allowed` (only
when you are actually stunned/feared/confused). Install lines:
Point Blank also shares the active-player cast-time detour used by Second
Coming. For Aimed Shot (hunter family flag `0x20000`) and Multi-Shot
(`0x1000`), it reports zero only to the two movement-refusal callers returning
to `0x0073A0C0` and `0x0080B086`. Every other caller receives the real cast
time, so the shots remain non-instant. The server owns movement interruption
after the cast begins and applies the matching aura-gated waiver.

`pointblank rule installed at 0x007FF480 + 0x007FF180`, `untamed rule installed at
0x00739650 + 0x0080B5D0`.

## In-game test plan (owner)

Needs the rebuilt `dinput8.dll` in the client folder **and** the server build
with the VioletHoldClassBoons changes — the client waiver alone just moves the
refusal to the server. Set `Debug = 1`.

1. **Install check.** Log in, open `AnimSpeedFix.log`: both `pointblank rule
   installed` and `untamed rule installed … + 0x0080B5D0` with no
   `[predicate FAILED]` and no `signature mismatch`.
2. **Point Blank, control group.** Hunter *without* 94346, target dummy at
   ~3 yd: Arcane Shot says "Target too close", Auto Shot button red.
3. **Point Blank.** Take the boon (or `.aura 94346`): same spot — Auto Shot
   button not red, Auto Shot fires and keeps firing, Arcane Shot / Concussive /
   Serpent Sting cast. Start Aimed Shot and Multi-Shot while already moving,
   and begin moving during either cast; both finish without becoming instant.
   Log shows `relaxed min range` / `point blank moving cast`. Max range unchanged
   (step back past 35 yd → "Out of range").
4. **Point Blank, scope.** Non-hunter spell with a minimum range on a hunter
   (none in stock; skip if none) and a non-hunter with the aura: nothing
   changes. Tooltip of Arcane Shot reads "35 yd range" with the aura.
5. **Untamed, control group.** Druid without 94361, stunned (Hammer of
   Justice / Cheap Shot / Bash): Cat Form says "Can't do that while stunned".
   Feared / polymorphed: form buttons dim.
6. **Untamed.** With 94361: stunned → Bear/Cat/Travel/Moonkin cast, the server
   breaks the stun (`OnShapeshift`). Feared → buttons stay lit, shift works,
   fear breaks. Polymorphed → same. Log shows `untamed cast: … -> allowed`.
7. **Untamed, scope.** With the aura while stunned: a non-form druid spell
   (Moonfire, Wrath) still refuses with the stock error.
8. **Off switch.** `PointBlankAura =` and `UntamedAura =` (empty) → log shows
   neither rule installed, stock behaviour back.
9. **Field Medic, control group.** Hunter without 94383, party member
   targeted: Arcane Shot → "Invalid target", Auto Shot button shows no range
   tint.
10. **Field Medic.** With 94383 (and the server build): party member, another
    friendly player and yourself → Arcane Shot, Multi-Shot and Auto Shot cast
    (log: `field medic: … accepted`) and the ally is healed; Auto Shot keeps
    repeating on them. Step past 35 yd → "Out of range" and the button tints
    red. Dead ally → still refused. Kill Shot lights on an ally under 20 %.
11. **Field Medic, scope.** Enemy target: unchanged. Concussive Shot / Serpent
    Sting on an ally: still "Invalid target". Non-hunter with the aura: nothing
    changes. `FieldMedicAura =` (empty) → log shows no fieldmedic line, stock.
12. **Second Coming, control group.** Priest without 94386, in combat, dead
    party member targeted: Resurrection → "You can't do that while in combat",
    button dim. Out of combat: 10 s cast bar, moving refuses.
13. **Second Coming.** With 94386 (and the server build), in combat: button
    lit, tooltip "Instant cast", Resurrection goes out at once with no cast bar,
    also while moving (log: `second coming cast: … allowed` and
    `… -> instant`); the charge is consumed by the server, after which the
    stock behaviour is back.
14. **Second Coming, scope.** Other priest spells with the aura (Shackle
    Undead, Mind Control): cast time and combat rules unchanged.
    `SecondComingAura =` (empty) → stock.

If step 3 or 6 lights the button but nothing casts and the server sends no
error, there is another client gate on the send path; if the **server** answers
with "Target too close" / "Can't do that while stunned", the server build is
missing.
