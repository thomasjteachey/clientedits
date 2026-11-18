# Client global cooldown: RE notes

Wow.exe 3.3.5a 12340, verified statically 2026-11-13. Companion to `src/gcdtempo.cpp`
(Violet Hold Boon of Tempo).

## The GCD function

`0x00805D70` `__cdecl(SpellRec* rec, CGUnit* caster)`. Callers: `0x53E209`, `0x54048F`,
`0x5D4427`, `0x80B583`. It is the client twin of TC `Spell::TriggerGlobalCooldown`.

| step | address | what |
|---|---|---|
| early out | `0x805D7A` | `rec+0x234` StartRecoveryCategory == 0 and `rec+0x238` StartRecoveryTime == 0 -> no GCD |
| spellmod | `0x805DA0` | op `0x15` (GLOBAL_COOLDOWN) via `Spell_C_GetSpellModifiers 0x7FD970`: `(t + flat) * pct / 100` |
| haste gate | `0x805DD3` | category == 133, time == 1500, DmgClass (`rec+0x254`) not 2/3, `!(Attributes & 0x12)` |
| haste | `0x805E00` | `t * UNIT_MOD_CAST_SPEED` (`[caster+0xD0]+0x128`), minus 0.5 (`[0xAF523C]`), fistp |
| **clamp** | `0x805E23`-`0x805E3E` | to **[1000, 1500]**, the "1.0 sec cap". Only on the haste branch. The server's `MIN_GCD` is 500 since commit 59f4998eef, so a caster with more than 33% haste gets a shorter GCD from the server than from the client, and the client is the gate |
| zero test | `0x805E40` | `test ebx,ebx / je 0x805EDF`. **gcdtempo hooks here** |
| start | `0x805EBE` | `0x805230` on the record bank `0xD3F5AC + 24*isActivePlayer` |

A flat GCD spellmod is applied before the zero test with no floor, so a spell with
category 133 and StartRecoveryTime 0 (300+ off-GCD class spells on Centurion: Blink, Overpower,
Stealth, Holy Shock ...) gets a negative GCD, which reads as huge and locks the class. Never use
a flat GCD spellmod with a wide mask.

## SpellRec in memory

Spell.dbc field order, `field*4`, except each localized string (Name/Rank/Description/Tooltip,
DBC fields 136-203) is ONE pointer. Every field after them is 64 dwords lower than its DBC index:
DBC 205 StartRecoveryCategory -> `0x234`, 206 StartRecoveryTime -> `0x238`, 208 SpellFamilyName
-> `0x240`, 213 DmgClass -> `0x254`.

## Aura records

0x18 bytes each, inline at `unit+0xC50`, or at `[unit+0xC58]` when `[unit+0xDD0] == -1`.
`GetAuraCount 0x4F8850`, `GetAuraInfo 0x4F8870` (-> spell id), `0x556E10`
`__thiscall(unit, index)` -> record pointer, 0 when out of range.
`+0x08` spell id, `+0x0E` stack count (byte; Lua `UnitAura` pushes it as `count` at `0x614910`).

## The GCD check (2026-11-18, for Boon of Swift Light)

The refusal side is the cooldown query `0x00807980` (wrapped by `0x00809000`, `__cdecl(spellId,
isActivePlayer, ...)`, on the record bank `0xD3F5AC + 24*isActivePlayer`). It fetches its own record
copy through `0x004CFD20` into `[ebp-0x2C4]`, so the copy's StartRecoveryCategory `+0x234` is
`[ebp-0x90]`, loaded at `0x8079E8` into `[ebp-0x1C]`. The GCD records are then matched at `0x807BBD`
(`cmp [rec+0x28], ecx`): a bank record's `+0x28` is the category the GCD start stored, its `+0x2C`
the duration, `+0x10` the start time. So start and check both take the category from a record copy
made by `0x004CFD20`; within the spell code region (`0x7F0000-0x812000`) no other instruction reads
`+0x234`. `rulerelax.cpp` rule `swiftlight` rewrites that field in the copy for one spell.
