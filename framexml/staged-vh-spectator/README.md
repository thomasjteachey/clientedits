# Violet Hold death = arena death (patch-enUS-A 1.00167, 2026-10-15)

Owner: *"when i die in arena it says 'You are in Spectator Mode' and i don't see a ghost
overlay for my screen. I want this to occur in VH as well"*

Both are the client's own behaviour, and both key off one value: the InstanceType of
the battlefield's map in the **client's** Map.dbc, which the battlefield status parser
stores at `0xBEA570` (4 = arena). The spectator text is stock StaticPopup.lua
(`IsActiveBattlefieldArena()` reads that global); the ghost screen is skipped in
`Wow.exe` at `0x4F8979`. Full site list: `docs/ARENA_UNITS_RE.md`.

The owner chose this over a narrower route (a server-applied aura with effect 260
SCREEN_EFFECT, misc 0, to blank the ghost screen, plus a StaticPopup tweak). That
route touches nothing else but needs Spell.dbc on both Centurion servers and a C++ build.

| file | what |
|---|---|
| `DBFilesClient/Map.dbc` | row 1608 InstanceType 3 -> 4, one dword (`make_map.py`, refuses to change anything else). **Client copy only**: the server's Map.dbc keeps 1608 at 3, since its battleground code needs that |
| `Interface/FrameXML/WorldStateFrame.lua` | switches back off the two arena behaviours that do not fit the Hold (below) |

## What the arena map type turns on in the Hold

Wanted: the DEATH popup says "Release spirit to enter Spectator Mode" and prints
`ARENA_SPECTATOR` on release, and there is no ghost screen.

Undone by WorldStateFrame.lua, which tells the Hold from an arena by its battlefield
status (arena map, but team size 0; every arena, custom arena games included, has
2, 3 or 5):

- **scoreboard**: `ToggleWorldStateScoreFrame`, `WorldStateScoreFrame_Update` and
  `_Resize` go through `CenturionScoreboard_IsArena()`, so it still opens mid-run and keeps
  the battleground columns.
- **arena enemy frames**: the clones are on the other side, so the client slots them into
  arena1-5. A run sends far more than five and the slots are never freed mid-match.
  `ArenaEnemyFrames_UpdateVisible` is wrapped when Blizzard_ArenaUI loads and re-run on
  `UPDATE_BATTLEFIELD_STATUS` (the status arrives after the loading screen), so they stay hidden.

Left as arena behaviour (minor, accepted):

- the minimap battlefield menu says "Leave Arena" (which is also what `ARENA_SPECTATOR` tells
  you to click) and the end-of-run message is the arena one;
- the battlefield minimap does not open (`IsInInstance()` now says "arena"), and the world
  map's report-AFK right-click is off;
- no corpse "Resurrect" prompt. The server refuses corpse reclaim in the Hold anyway;
  casualties come back when the wave they died to is cleared.

The DEATH popup text is set when the popup opens. Fine here: the status that sets `0xBEA570`
arrives right after the loading screen, long before anyone dies.

## Verifying

```
python3 lua_run.py laststand_test.lua WSF_PATH=<path>/WorldStateFrame.lua   # -> pass=19 fail=0
```
`lua_run.py` is in `../staged-ctf-flags/tests/` (needs the game server's `liblua5.3.so.0`).
The live 1.00166 copy fails 11 of them: enemy frames shown and scoreboard locked in the Hold.

Published with `../staged-centurion-ui/publish_files.py spec_vhspectator_A.json` from
`/tmp/centmode/vh-spectator` on the game server. 1715 -> 1715 names, both files read back,
five samples unchanged. Backups `~/patch-backups/patch-enUS-A.{zip,version}.bak-vh-spectator-1.00166`.
Rechecked over HTTP: version `1.00167`, served Map.dbc row 1608 InstanceType 4.

To undo, republish the 1.00166 copies of both files (Map.dbc `becc1469d5ad715673acf904475b17fc`,
WorldStateFrame.lua `2dd8590ff298bd62f6755620d33d214e`, which is
`../staged-ctf-flags/shipped/A/Interface/FrameXML/WorldStateFrame.lua`) as a NEW version. Do not
put the backup zip back under 1.00166: clients already on 1.00167 would never re-download it.
