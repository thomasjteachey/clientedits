# Arena opponent units (arena1..5) — Wow.exe 3.3.5a 12340

Verified statically 2026-09-24 with `reversing/peexplore.py` while looking at a
report that Gladius showed no enemies. There is **no arena-opponent packet in
3.3.5**: the client fills the arena slots itself, from the battlefield status
packet plus each player's own descriptor.

## Globals

| VA | what | written by |
|---|---|---|
| `0xBEA570` | InstanceType of the battlefield map (4 = arena) | SMSG_BATTLEFIELD_STATUS parser, `mov [0xBEA570], eax` @ `0x54AFB1` (eax = Map.dbc row +8) |
| `0xBEA54A` | the LOCAL player's arena faction (byte) | same parser, `CDataStore::GetUInt8(0xBEA54A)` @ `0x54B003` — the last byte of a STATUS_IN_PROGRESS status |
| `0xBE9EF8` | 5 slots × 0x10: opponent GUID per arenaN | `0x54D100` |
| `0xBE9F48` | 5 × 8: GUID last announced per slot | `0x54B840` |

## Functions

- **`0x54D100 (WGUID*)`** — assign an arena slot: returns early if the GUID
  already holds one (scan `0xBE9F00`, 5 × 0x10), else takes the first empty slot
  and calls `0x54B840`.
- **`0x54B840 (WGUID*, slot)`** — stores the GUID, builds `"arena%d"` (`0xA0C3A4`)
  and fires `ARENA_OPPONENT_UPDATE(unit, "seen")` (event 0x26A via `0x81B530`),
  or `"unseen"` (`0xA0C39C`) when the object is not in the object manager.
- **`0x54B920`** — the pet counterpart (`arenapet%d`).

## When a player becomes an opponent

Two call sites in the CGPlayer create/re-init paths, identical logic:
`0x6E1D91` (→ call `0x6E1DBD`) and `0x6E8300` (→ call `0x6E832C`):

```
cmp dword [0xBEA570], 4          ; only on an arena map
jne skip
mov eax, [esi+0x1008]            ; the player's PLAYER_* descriptor block
mov cl, [eax+0x1F]               ; PLAYER_BYTES_3 byte 3 = arena faction
cmp cl, [0xBEA54A]               ; vs my arena faction from the status packet
je  skip                         ; same side -> not an opponent
call 0x54D100                    ; different side -> arenaN
```

`+0x1F` = field 7 of the player block (PLAYER_BYTES_3) byte 3, i.e.
`PLAYER_BYTES_3_OFFSET_ARENA_FACTION`, which the server sets in
`Player::SetBGTeam` (ALLIANCE → 1, HORDE → 0).

So, server-side, arena frames need: (1) a STATUS_IN_PROGRESS status whose last
byte is the player's real side, received BEFORE other players' objects are
created; (2) every other player's arena-faction byte already correct in the
CREATE packet. The check runs at object creation, so a byte set afterwards may
not re-slot the player (the second call site may be a re-init path — not yet
confirmed which).

## Everything that reads `[0xBEA570]` (2026-10-15)

Verified statically while making Violet Hold die like an arena. `0xBEA570` is set
only by the STATUS_IN_PROGRESS branch of the SMSG_BATTLEFIELD_STATUS parser: it
reads the packet's map id into `0xBEA564` @ `0x54AF75`, looks it up in the client's
Map.dbc (min id `0xAD4170`, max `0xAD416C`, rows `0xAD4180`) and stores row `+8`
(InstanceType) @ `0x54AFB2`. It is zeroed with the map id when the parser finds no
active slot (`0x54B17E`) and by the battlefield state reset (`0x54E37D`).
So "arena" to the client means **the battlefield's map is InstanceType 4 in the
client's own Map.dbc** - the packet's team-size byte plays no part. The other 16
references are all `cmp dword [0xBEA570], 4`:

| site | in | arena (==4) does |
|---|---|---|
| `0x549AD3` | `IsActiveBattlefieldArena` `0x549AD0` (Lua table `0xACD19C`) | returns true. FrameXML keys the DEATH popup on it (StaticPopup.lua: `DEATH_RELEASE_SPECTATOR` on show, prints `ARENA_SPECTATOR` on release), the scoreboard (arena columns, ToggleWorldStateScoreFrame refuses mid-match), the minimap menu's LEAVE_ARENA and the end-of-match ARENA_COMPLETE_MESSAGE |
| `0x4F8979` | full-screen effect picker (`0x4F7020(id)`) | **skips the ghost screen.** Order: an aura with effect 260 SCREEN_EFFECT wins (its MiscValue is the effect id); else `PLAYER_FLAGS & 0x10` (ghost, player block `+0x1008`, field 2) and not arena -> effect 1; else descriptor byte `+0x10E7 & 0x40` -> effect 0x51; else 0 (none) |
| `0x515A03` | `ResurrectHasTimer` `0x515A00` | returns nil |
| `0x512C4E`, `0x524A5D`, `0x5267A6` | corpse-range state `0xBD0820` | skips firing events 0x184-0x186 (by position, the corpse in-range / out-of-range family - no "Resurrect" reclaim prompt; event names not confirmed) |
| `0x524AAA` | same function as `0x524A5D`, after a ghost-flag test | skips firing event 0x216 |
| `0x6E1D91`, `0x6E8300` | CGPlayer create / re-init | assigns arenaN slots (above) |
| `0x740218`, `0x7403D7` | pet create paths | assigns arenapetN slots via `0x54D1C0` |
| `0x4D72B3` | object update path | calls `0x54ABF0` with the unit's GUID |
| `0x54B5F2` | packet handler (reads a GUID) | routes to `0x54AD10` |
| `0x54AA85` | loop over battlefield members `0xBEA368` | skips the per-member work |
| `0x54CE4D` | timed battlefield check (`0xBEA584`/`0xBEA588`) | skipped |
| `0x521EC2` | unit-pair test on descriptor byte `+0x1D1` | forces the flag false |

`IsInInstance` (`0x5156A0`) is separate: it reads the CURRENT map (`0xBD088C`)
InstanceType and returns "arena" for 4, which loads Blizzard_ArenaUI from
UIParent and gates Blizzard_BattlefieldMinimap (no toggle in "arena"),
WorldStateFrame_CanShowBattlefieldMinimap and the world map's report-AFK click
(both "pvp" only).

**Violet Hold uses this.** The client's Map.dbc row 1608 is InstanceType 4 (the
server's stays 3) since patch-enUS-A 1.00167; see
`framexml/staged-vh-spectator/README.md` for what that switches on and what
WorldStateFrame.lua switches back off.
