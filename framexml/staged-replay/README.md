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
- **Spectate** (`CenturionReplaySpectate.lua`): right-click a player in the
  replay (in the world, or their arena frame), or press Spectate. The camera
  follows them without turning with them (client-tweaks DLL `CenturionSpectate`,
  clientedits `src/spectate.cpp`; only the camera is taken over, every other
  control works); the arena frames hide and the normal UI shows; the server puts
  THEIR buttons and cooldowns on YOUR default action bars (ASSUN `SPECTATE
  <guid>`, the way an All-Star life swaps bars) and lights the button they press.
  Right-click someone else to switch; a movement key (W/A/S/D, strafe, autorun)
  ends it and you are back in your own body with your own bars (`SPECTATE OFF`).

Test: `test/run_replay.py` + `test/mock_replay.lua` (on the bounty board's
harness, `bountyboard/harness`) - 39 checks, all passing, 2026-10-29.

Publish: copy `CENTURION_Replay/` up next to `addons_replace_replay.sh` on the
game box and run it there (backs up, replaces three files, bumps
addons.version last, verifies over HTTP). Without the server build the new
lines simply never arrive and the addon behaves as v63.
