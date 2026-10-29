# staged-replay: CENTURION_Replay v64

Base: the live addons.zip copy (addons 1.00136; SunwellAS.lua md5 25c3476b...).

What changed, with the server half in `src/server/scripts/Custom/BGReplay.cpp`
(replay blob V3):

- **Buffle Shuffle**: the frames move sides when a round re-deals the teams
  (server roster events -> `TEM`). Replays saved before V3 follow the
  arena-team byte of the replayed unit fields instead.
- **All-Star**: name, class icon/colour and power type follow each life
  (roster events -> `NME`/`CLA`, and `UNIT_FIELD_BYTES_0` -> `CLA`/`PWT`).
- **Four Corners**: new `SQD` (squad 0..3, 255 none); a squad match lays the
  frames out by squad in the screen corner of its map corner, in the squad's
  colour, scaled to fit 5v5v5v5.
- Rage / runic power show /10 like the bars.
- **Spectate** (`CenturionReplaySpectate.lua`): right-click a frame or press
  Spectate. The camera follows that player without turning with them (needs
  the client-tweaks DLL with `CenturionSpectate`, clientedits
  `src/spectate.cpp`); their action bars (`BRS`/`BAR`/`BMC`), cooldowns
  (`BCD`), stance/form page (`FRM`) and button presses (`SPE`) show; the
  arena frames hide; a strip at the top switches player; X stops.

Test: `test/run_replay.py` + `test/mock_replay.lua` (on the bounty board's
harness, `bountyboard/harness`) - 30 checks, all passing, 2026-10-29.

Publish: copy `CENTURION_Replay/` up next to `addons_replace_replay.sh` on the
game box and run it there (backs up, replaces three files, bumps
addons.version last, verifies over HTTP). Without the server build the new
lines simply never arrive and the addon behaves as v63.
