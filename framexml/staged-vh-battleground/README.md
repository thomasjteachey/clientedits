# Violet Hold is a battleground on the client again (patch-enUS-A 1.00193, 2026-10-16)

Owner: *"can't use rebirth or potions in violet hold"* - then *"it has to do with the
arena change we made for the client so that it showed 'Entering Spectator Mode'"*.

Since 1.00167 (`../staged-vh-spectator`) the client's Map.dbc row 1608 was InstanceType 4,
an arena, so dying in the Hold looked like dying in an arena. It also put the Hold under
the client's own arena rules, which the client applies before anything is sent: every
consumable without the arena item flag (0x200000 - no potion has it) and Rebirth (its
long cooldown; ranks 6-7 also carry SPELL_ATTR4 "not in arena") were refused locally. The
server's Map.dbc always kept 1608 at 3 and would have allowed both.

The owner picked: map back to a battleground, the arena look rebuilt without it.

| file | what |
|---|---|
| `DBFilesClient/Map.dbc` | row 1608 InstanceType 4 -> 3, one dword (`tools/revert_map.py`, refuses anything else) |
| `DBFilesClient/Spell.dbc` | not kept here (58 MB): the live client file plus spell **92335** "Spectator Mode", appended by `tools/add_spectator_spell.py` |
| `Interface/FrameXML/WorldStateFrame.lua` | finds the Hold by `GetInstanceInfo()` (type "pvp", name "The Violet Hold Gauntlet"), and teaches the stock DEATH popup the Hold |

## How the arena look comes back

- **Popup.** `StaticPopupDialogs["DEATH"]` is wrapped at load (StaticPopup.xml is line 36 of
  FrameXML.toc, WorldStateFrame.xml line 123): in the Hold, OnShow sets
  `DEATH_RELEASE_SPECTATOR` and OnAccept prints `CENTURION_LAST_STAND_SPECTATOR` - the stock
  `ARENA_SPECTATOR` line, but naming the battleground icon and "Leave Battleground", which
  is what the minimap offers there now. Never in addition to the stock arena line. The Hold
  has no release timer (server `UsesArenaDeathRules`), so StaticPopup_OnUpdate never
  rewrites the text.
- **Ghost screen.** Spell 92335: hidden (AttributesEx 0x12000000), can't cancel, castable
  while dead, can target dead, death persistent, infinite, one effect APPLY_AURA 260
  SCREEN_EFFECT misc 0. The client's effect picker (Wow.exe 0x4F8979) takes a 260 aura ahead
  of the ghost check, and 0 is no effect. The server (`Player::BuildPlayerRepop`, commit
  5fae69817e) puts it on whoever releases in a battleground that dies like an arena but is
  not one; `ResurrectPlayer` takes it off. Same row appended to both Centurion servers'
  Spell.dbc (`.bak-spectator-20261016`); loaded at their next restart.
- The scoreboard and arena-frame rules from 1.00167 stay, now keyed on the same helper; with
  the battleground row they change nothing, and they still hold if the row ever goes back.

What returns to battleground behaviour in the Hold: the minimap menu reads "Leave
Battleground", the battlefield minimap opens, report-AFK is back.

Potions at 60 also needed the server: every level-60 Hold run is a tournament-rules match,
which bans potions. `Centurion.Tournament.BgConsumablesFreeIn` (default "105") lifts that ban
for the Hold only (same commit).

## Verifying

```
python3 lua_run.py spectator_popup_test.lua WSF_PATH=<path>/WorldStateFrame.lua   # pass=28 fail=0
python3 lua_run.py ../staged-vh-spectator/tests/laststand_test.lua WSF_PATH=...    # pass=19 fail=0
```
`lua_run.py` is in `../staged-ctf-flags/tests/`.

Published with `../staged-centurion-ui/publish_files.py spec_vhbg_A.json` from
`/tmp/centmode/stage-vhbg` on the game server: 1756 -> 1756 names, 3 files read back, 5
samples unchanged. Backups `~/patch-backups/patch-enUS-A.{zip,version}.bak-vh-battleground-1.00192`.
