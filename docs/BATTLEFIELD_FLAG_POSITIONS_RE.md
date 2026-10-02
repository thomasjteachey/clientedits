# Battleground flag carrier positions (Wow.exe 3.3.5a 12340)

How the client stores, requests and colours WSG / Twin Peaks flag carrier markers. Found 2026-09-16 while fixing the cross-faction flag icons (`clientedits/framexml/staged-ctf-flags`). Addresses are VAs, image base 0x400000; tools: `clientedits/reversing/peexplore.py`, `dscan.py`.

## Storage

| address | meaning |
|---|---|
| `0xBEA170` / `0xBEA178` | flag carrier GUID slots 0 and 1 (uint64 each) |
| `0xBEA710 + slot*8` | packet X / Y (float, float) per slot |
| `0xBEA5B0` | count of "player positions" (the other half of the packet, 0 from TrinityCore) |
| `0xBEA180 + i*8`, `0xBEA5D0 + i*8` | those player position GUIDs and X/Y |
| `0xBEA5B4` | positions request deadline (ms) |
| `0xACD310` | token table: `[0] "AllianceFlag"`, `[1] "HordeFlag"` |

## MSG_BATTLEGROUND_PLAYER_POSITIONS handler `0x54B3F0`

Reads the player positions, then `uint32 flagCount` and the carriers:

- `count <= 0`: both slots and positions cleared.
- `count == 1`: slot 1 cleared, the carrier goes to slot 0.
- `count == 2`: **the first carrier in the packet goes to slot 1, the second to slot 0** (loop `eax = count-1 ... 0`).

It does not check that a request was pending, so a server may push the packet unasked.

## Lua API

- `GetNumBattlefieldFlagPositions` `0x54A0E0`: 2 if slot 1 is set (even with slot 0 empty), else 1 if slot 0 is set, else 0.
- `GetBattlefieldFlagPosition(i)` `0x54DCC0`: slot `i-1`. Position = the carrier unit's live position if the object is known (`0x5444F0`), else the packet position mapped through `0x544140` for the current map area `[0xBEA564]`. Third return = `0x54D010(slot)`.
- `RequestBattlefieldPositions` `0x54DCB0` -> `0x54CF60`: returns without sending unless 5000 ms have passed since the last request (`[0xBEA5B4] = now + 0x1388`). Direct callers: the Lua function and the G15 LCD screen `0x848D50`. Nothing in the engine polls it: FrameXML asks from `WorldMapFrame_OnUpdate` (every frame while the world map is open) and patch-enUS-7's battlefield minimap asks on show / zone change.

## Token `0x54D010(slot)`: race based

- Carrier object known: `unit->0x6D6E90()`, which reads the race byte (`[unitData(+0xD0)] + 0x44`, UNIT_FIELD_BYTES_0), looks up ChrRaces -> FactionTemplate, and returns 0 if the group mask has Horde (4), 1 if Alliance (2), else -1.
- Carrier unknown: the local player's own `0x6D6E90()`, kept if `0x549670(carrier)` finds the carrier in the raid list (`0xBEB568`, 40 entries), otherwise flipped.
- Token = `table[team]` (0 -> "AllianceFlag", 1 -> "HordeFlag"); on map 566 (Eye of the Storm) the index is inverted.

So in a cross-faction battleground the stock icon follows race, not team: on a realm where every race is Alliance, every visible carrier is "HordeFlag".

## Server side (LEGIONNAIRE_PLUS)

`WorldSession::HandleBattlegroundPlayerPositionsOpcode` writes `GetFlagPickerGUID(TEAM_ALLIANCE)` (the physical Alliance flag's carrier; picker slots are indexed by flag identity in both BattlegroundWS and BattlegroundTP) first, then the Horde flag's. With the client's reversal, Lua marker 1 is the Horde-flag carrier whenever both flags are carried.

## Not the minimap

The only engine code that draws these tokens itself is the G15 LCD battleground screen (`0x848D50`, strings `WarsongMapScreen`, `PlayerIcon`, `LCD.CPP`). The world map and battlefield minimap flags are FrameXML textures (`WorldMapFlag1..2`, `BattlefieldMinimapFlag1..2`), so Lua can fully control them.
