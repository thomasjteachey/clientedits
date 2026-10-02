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
