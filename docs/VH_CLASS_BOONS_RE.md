# Violet Hold class boons — client-side waivers (Point Blank, Untamed)

Wow.exe 3.3.5a 12340. Code: `src/rulerelax.cpp` (rules `pointblank` and
`untamed`, next to the existing `weapon` rule). Companion to
`RULE_RELAX_RE.md`, whose reasoning (DBC is global, auras are per-player, so the
waiver has to happen at runtime) applies unchanged.

Both refusals are client-local: the client never sends `CMSG_CAST_SPELL`, so the
server halves in `VioletHoldClassBoons` (`IgnoresMinRange` in
`Spell::GetMinMaxRange`, `ShiftsThroughControl` in `Spell::CheckCasterAuras`)
are never asked. Both halves are needed. The server never trusts the client:
deleting the DLL grants nothing, and the DLL alone grants nothing either.

All addresses below were found by **byte scan** (call-target scan for
`E8 rel32`, raw displacement/immediate search, decoding backwards from each
hit), not by a linear sweep — see the tooling note in `RULE_RELAX_RE.md`.
Every signature in the code was re-checked against `clients\centurion\Wow.exe`
after the build (all six match).

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

## Point Blank — minimum range (aura 94346)

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

## Settings (`[RuleRelax]` in AnimSpeedFix.ini)

| key | default (compiled in) | meaning |
|---|---|---|
| `Enabled` | 1 | whole module |
| `PointBlankAura` | `94346` | aura ids; empty = rule off |
| `UntamedAura` | `94361` | aura ids; empty = rule off |
| `Debug` | 0 (repo ini: 1) | log waivers to `AnimSpeedFix.log` |

Each rule installs on its own; a signature mismatch logs and disables only that
rule. Both auras are visible (Attributes `0x80000000`, not PASSIVE/HIDDEN), the
same as Bare Knuckle 90217, so the client has them in its aura array.

Debug lines: `relaxed min range: spell=… min=… -> 0` (throttled, it runs per
button per update) and `untamed cast: spell=… unitFlags=… -> allowed` (only
when you are actually stunned/feared/confused). Install lines:
`pointblank rule installed at 0x007FF480`, `untamed rule installed at
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
   Serpent Sting cast. Log shows `relaxed min range`. Max range unchanged
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

If step 3 or 6 lights the button but nothing casts and the server sends no
error, there is another client gate on the send path; if the **server** answers
with "Target too close" / "Can't do that while stunned", the server build is
missing.
