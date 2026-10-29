CENTURION_Replay v62 Hide On Map Change
=======================================

Install
-------
Copy this folder to:
  World of Warcraft/Interface/AddOns/CENTURION_Replay/

Then /reload.

Test requirement
----------------
No new arena needed.
Server rebuild is not required.

Fix
---
This removes the overcomplicated replay-exit detection.

Behavior:
  PLAYER_ENTERING_WORLD fires while replay addon is active -> CREP_Disable()

That means any map change while the addon is showing hides all replay addon UI:
- Exit Replay button
- manual Leave Battleground
- GM port
- hearth/teleport
- reconnect/map reload

Preserved
---------
- v61/v60 addon fixes.
- v60 name color scope fix.
- v59 castbar/scanner guards.
- v58 Blizzard UI hide while replay is active.
- v56 spell history tooltips.
- v55 coordinate aura tooltip scanner.
- v50 removal of the old spect reset command.
