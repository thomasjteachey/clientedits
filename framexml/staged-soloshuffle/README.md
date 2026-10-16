# Solo Shuffle (shipped 2026-10-15)

| file | archive | what |
|---|---|---|
| `Interface/FrameXML/ArenaFrame.lua` | **patch-enUS-A 1.00192** (1.00190 had the 3v3 row only) | `CCGAME ARENAQUEUE:SHUFFLE` (sent by Solo Shuffle's battlemaster, Dealer Rixa) puts the arena window in shuffle mode: title "Solo Shuffle", the rated rows put away and their header saying "Queue alone. New teams every round.", the practice rows "2v2 Solo Shuffle" and "3v3 Solo Shuffle" (3v3 picked by default) beside "Arena bot matches", no Join as Group. Any numeric `ARENAQUEUE:<n>` leaves the mode. |
| `DBFilesClient/BattlemasterList.dbc` | **patch-enUS-A 1.00190** | Row 889 "Solo Shuffle", a copy of All Arenas (6) with levels 10-80. Byte-identical to tc-centurion's server copy; tc-centuriondev got the same row added to its own file. Without the row the client cannot show the queue status for type 889. |

Built from the live 1.00189 copies (ArenaFrame.lua `eeaa45bb...`, BattlemasterList.dbc
`ef63f3da...`); `spec_soloshuffle_A.json` is the `publish_files.py` spec that made 1.00190,
with backups `~/patch-backups/patch-enUS-A.*.bak-solo-shuffle-1.00189`.
`spec_soloshuffle2_A.json` made 1.00192 (2v2 row, ArenaFrame.lua `50539024...` built on 1.00190's
`71bc5d05...`), backups `*.bak-solo-shuffle-2v2-1.00191`.

`bml.py` dumps BattlemasterList.dbc and appends a row copied from another (`add <in> <out>
<newId> <copyFromId> <name> [maxGroupSize]`). `render/render_shuffle.py` draws the window
next to the ordinary one with `staged-centurion-ui/render`; `render/solo_shuffle_window.png`
is its output.

The in-match enemy frames need nothing new: the server feeds a shuffle's two sides to
`CenturionSquadFrames.lua` (staged-fourcorners) as two squads, re-sent every round.
