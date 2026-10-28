# All-Star (shipped 2026-10-28)

The client half of the All-Star arena (battleground 890, tc-lplus 351950fb9a). Built on the
server in `~/allstar-20261028/client/`; these are the exact published files and the
generator.

| file | archive | what |
|---|---|---|
| `Interface/FrameXML/ArenaFrame.lua` | **patch-enUS-A 1.00256** | `CCGAME ARENAQUEUE:ALLSTAR` (sent by the All-Star battlemaster, Champion Tavrin 922154) puts the arena window in All-Star mode: title "All-Star", the rated rows put away and their header saying "Nine lives: one for each class.", the four practice rows "1v1/2v2/3v3/5v5 All-Star" (2v2 picked by default) beside "Arena bot matches", Join as Group as ever. The server reads the row as the size (slot 0..3 -> 1v1, 2v2, 3v3, 5v5). SHUFFLE and the numeric modes clear the All-Star flag, and ALLSTAR clears theirs. md5 `db0da183b64ba1649a3302a20c88dbee`, built by `make_arenaframe.py` from the live 1.00255 copy `bf1252c1...`. |
| `DBFilesClient/BattlemasterList.dbc` | **patch-enUS-A 1.00256** | Row 890 "All-Star", a copy of 889 (Buffle Shuffle), MaxGroupSize 5 (`bml.py add ... 890 889 All-Star 5`, staged-soloshuffle). md5 `e338ef9e...`, built on A's own `6ed2e292...`. The client needs the row to show the queue status. Both servers' `data/dbc` get the same row from their own copy (`c8a672e6...`, built on `7cc2f5eb...`; CenturionDev has it, `.bak-allstar-20261028`). |
| `CENTURION_AllStar/` | **addons.zip 1.00127** | Hides the "You have (un)learned" and skill lines a class swap prints, for 5 s after the server's `CCGAME ALLSTAR:QUIET`. Nothing else. |

`spec_allstar_A.json` is the `publish_files.py` spec that made 1.00256 (backups
`~/patch-backups/patch-enUS-A.*.bak-allstar-1.00255`); `addons_add_allstar.sh` added the
addon folder (backups `addons.*.bak-allstar-1.00126` in the patches dir). Both Lua files
pass the server's `lua_compile.py`. Not seen in game yet.

The in-match frames need nothing new: All-Star is a two-team arena, and the stock arena
frames and the "Players Remaining" line (players with a life left) serve it.
