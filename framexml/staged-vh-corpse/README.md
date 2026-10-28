# No corpse prompt in Violet Hold (patch-enUS-A 1.00254, 2026-10-28)

Owner: *"i still have a death timer popup in violet hold even though in arenas i don't have it"*.

The popup was the stock corpse prompt, `StaticPopupDialogs["RECOVER_CORPSE"]`: "You can
resurrect in 30 sec", then "Resurrect now?". The Hold has no graveyard
(`BattlegroundVHR::GetClosestGraveyard` returns nullptr), so a released ghost stays on its own
corpse and the client fires `CORPSE_IN_RANGE` at once. The countdown is the reclaim delay the
server sends from `Player::BuildPlayerRepop`. The button does nothing there: `HandleReclaimCorpse`
refuses it (`AllowsCorpseReclaim` is false for the Hold), and the dead come back when the wave
falls. The client skips this prompt only in an arena (Map.dbc InstanceType 4), and since 1.00193
the Hold is a battleground on the client again.

| file | what |
|---|---|
| `Interface/FrameXML/WorldStateFrame.lua` | built from the live 1.00253 copy (`4dd3602d...`). Right after the DEATH popup wrapper, `StaticPopup_Show` is wrapped: `"RECOVER_CORPSE"` returns nil when `CenturionIsLastStandBattlefield()`, and everything else passes straight through. |

UIParent_OnEvent (UIParent.lua line 727, patch-enUS-3) calls `StaticPopup_Show("RECOVER_CORPSE")`
through the global when it runs, so wrapping the global reaches it. Nothing in the client caches
it as a local. `RECOVER_CORPSE_INSTANCE` is left alone because it cannot come up inside the Hold.

Resurrection by another player (the `RESURRECT` popup from Rebirth or a soulstone) still works.

No server change. The reclaim countdown is still sent, but with the prompt gone nothing shows it.

## Verifying

```
python3 lua_run.py corpse_popup_test.lua WSF_PATH=<path>/WorldStateFrame.lua                            # pass=54 fail=0
python3 lua_run.py ../staged-vh-battleground/tests/spectator_popup_test.lua WSF_PATH=...    # pass=28 fail=0
python3 lua_run.py ../staged-vh-spectator/tests/laststand_test.lua WSF_PATH=...            # pass=19 fail=0
```
`lua_run.py` is in `../staged-ctf-flags/tests/` (it needs liblua5.3, so run it on the game server).
`corpse_popup_test.lua` fails 4 checks against the old 1.00253 file, which is the bug.

Published with `../staged-centurion-ui/publish_files.py spec_vhcorpse_A.json` from
`/tmp/centmode/vh-corpse` on the game server: 1870 -> 1870 names, 1 file read back, 5 samples
unchanged, then re-read over HTTP (version `1.00254`, file md5 `168815fa...`). Backups
`~/patch-backups/patch-enUS-A.{zip,version}.bak-vh-corpse-1.00253`.
