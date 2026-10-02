# Client-side spell rule relaxation — RE notes

Wow.exe 3.3.5a 12340. Companion to `src/rulerelax.cpp`.

## The problem this exists to solve

Some cast rules are decided entirely by the client, from its own `Spell.dbc`.
A server-side set bonus that waives such a rule is invisible to the client: the
button stays greyed and, for at least the equipped-weapon rule, the cast is
never sent at all.

The obvious data fix — clear the field in `Spell.dbc` and re-enforce the rule
server-side, the way this fork already does for Starfire's movement-interrupt
flag — does not work when the bonus must be **per-player**. DBC is global. For
the equipped-weapon rule, clearing `EquippedItemClass` would also delete the
"Requires Melee Weapon" tooltip line and the grey-out for every player on the
server, not just the ones wearing the set.

Auras are per-player; DBC is not. So the waiver has to happen at runtime in the
client, gated on an aura. That is what this module does.

## Verified addresses

| What | Address | Notes |
|---|---|---|
| Lua `IsUsableSpell` name string | `0x00A0AD28` | no code xref — it is a data table entry |
| Lua registration table entry | `0x00ACCD78` | `{const char* name, fn}` pairs; handler is the next dword |
| `Script_IsUsableSpell` | `0x00541680` | from that table |
| **Usability predicate** | **`0x0080B5D0`** | `__cdecl(unit, SpellRec*, int* out, 0, 0) -> bool` in `al`, caller cleans `0x14`, 8 callers |
| Power/mana check | `0x008017E0` | drives `IsUsableSpell`'s second return value |
| `GetActivePlayerGuid` | `0x004D3790` | already used by playercollide |
| `ObjectPtr` | `0x004D4DB0` | already used by playercollide |
| `GetAuraCount` | `0x004F8850` | `__thiscall(unit) -> int` |
| `GetAuraInfo` | `0x004F8870` | `__thiscall(unit, index) -> spellId` |
| Cast-error name switch stubs | `0x007FCD40`+ | `mov eax, <string>; ret` per error id |
| `.\SpellCast.cpp` assert string | `0x00AA9818` | pushed at `0x009AB847` — start here for the send path |

`Script_IsUsableSpell` copies the whole Spell.dbc row into a `0x2B4`-byte stack
local (`mov ecx, 0xAD49D0` / `call 0x004CFD20`) and passes a pointer to that
copy as arg2. So the record the hook sees is the caller's private copy.

## SpellRec layout

In-memory record is the raw `Spell.dbc` field order, `field_index * 4`.
Cross-checked two ways: against
`tools\WoW Spell Editor LPLUS\Bindings_335_wotlk\Spell.txt`, and against live
code in `0x80B5D0` which reads `[rec+0x10]` (field 4 = `Attributes`, tested
against bits `0x2` and `0x800000`) and `[rec+0x11C]` (field 71 = `Effect1`,
compared to effect ids 47 and 78).

```
+0x000  Id
+0x010  Attributes                     (field 4)
+0x080  AuraInterruptFlags             (field 32)
+0x110  EquippedItemClass              (field 68)   -1 = no requirement, 2 = weapon
+0x114  EquippedItemSubClassMask       (field 69)
+0x118  EquippedItemInventoryTypeMask  (field 70)
+0x11C  Effect1                        (field 71)
```

## Why there is no "weapon check" address here

The equipped-item test could not be pinned to its own function statically.
There is **no** `[reg+0x110]` access anywhere in the spell code region — a
byte-level scan of `.text` (not a linear disassembly; see below) found 654
occurrences of the displacement across the whole image and none of them is a
spell record.

Two candidates elsewhere were checked and **rejected**:

- `0x008D5590` — tests `[esi+0x110] == -1` and looks perfect, but the `je`
  target returns `42 = SPELL_FAILED_ITEM_ALREADY_ENCHANTED`. It is enchanting
  code where `+0x110` means something else. Its three callers all sit in an
  unrelated `0x8D7xxx–0x8E4xxx` cluster.
- `0x007FC262` / `0x007FC4A3` — read `+0x110/+0x114/+0x118` as a tight triple,
  but the surrounding code is `fadd`/`fmul`/`fstp` on `+0x120`/`+0x124`. It is
  a 3-float vector copy in a different struct.

So the module does not patch a named check. It uses the client's own answer
**differentially** instead:

```
ask the predicate normally                    -> unusable?
ask it again with EquippedItemClass set to -1 -> usable?
if that flipped the answer, the weapon requirement was the deciding reason
```

This is exact. A cooldown, stun, range or cost failure keeps the second answer
false too, so it cannot be mistaken for a weapon problem — and it stays correct
regardless of which internal function actually performs the test.

## Tooling note: linear disassembly lies here

`peexplore.py` sweeps `.text` linearly, which desynchronises on the jump tables
the client embeds in code. Searching for `[reg+0x110]` that way found **3**
instructions; a raw byte search for the displacement followed by decoding
backwards from each hit found **654**. When hunting a rare instruction pattern,
scan bytes and decode locally — do not trust a sweep from the section start.
(`scratchpad/bytescan.py` in the session that produced this doc; worth folding
into `reversing/` if this comes up again.)

## Open measurement — do this before shipping

Whether `0x0080B5D0` is the **only** gate is not settled. It certainly drives
the button tint. If the send path re-checks independently, the button will
light up but the cast still will not go out.

Run with `[RuleRelax] Debug = 1`, unequip your weapon, press the ability:

- log shows `relaxed` **and the spell casts** → this hook is the whole gate, done.
- log shows `relaxed` **but nothing happens** → there is a second gate on the
  send path. Start from `.\SpellCast.cpp` (`0x00AA9818`, pushed at
  `0x009AB847`) rather than from the usability path.

Measure it. Do not assume.

## Server side is still required

The DLL only removes a client-side refusal. `Spell::CheckItems` still rejects
the cast at
`src/server/game/Spells/Spell.cpp:7428` (`HasItemFitToSpellRequirements`) and
`:7863` (the `SPELL_ATTR3_MAIN_HAND` / `REQ_OFFHAND` weapon-presence check).
Both halves are needed, and that split is what keeps this non-exploitable:
deleting the DLL grants nothing, and the server never trusts the client's
opinion about who holds the aura.
