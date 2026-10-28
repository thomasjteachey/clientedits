# Solo Shuffle (shipped 2026-10-15) - now Buffle Shuffle

**Renamed Buffle Shuffle 2026-10-22, patch-enUS-A 1.00232** (the owner's call). Built from the
live 1.00231 copies by `~/shuffle-rename-20261022/shuffle_rename.sh` on the server: ArenaFrame.lua
(title, the "2v2/3v3 Buffle Shuffle" rows) -> `bf1252c1...`, BattlemasterList.dbc row 889 -> `6ed2e292...`,
plus GameTips.dbc tip 17 and two comments in CenturionSquadFrames.lua. The DBC names were renamed by
appending the new string and repointing the field (`dbc_restring.py`), so the old text is still in
the string blocks, unreferenced. The servers' BattlemasterList.dbc got the same row-889 rename
(`7cc2f5eb...`). The directory, the Lua globals (`CENTURION_SOLO_SHUFFLE_*`) and the server's code
names keep the old name; the text below describes the files as first shipped.

| file | archive | what |
|---|---|---|
| `Interface/FrameXML/ArenaFrame.lua` | **patch-enUS-A 1.00192** (1.00190 had the 3v3 row only) | `CCGAME ARENAQUEUE:SHUFFLE` (sent by Solo Shuffle's battlemaster, Dealer Rixa) puts the arena window in shuffle mode: title "Solo Shuffle", the rated rows put away and their header saying "Queue alone. New teams every round.", the practice rows "2v2 Solo Shuffle" and "3v3 Solo Shuffle" (3v3 picked by default) beside "Arena bot matches", no Join as Group. Any numeric `ARENAQUEUE:<n>` leaves the mode. |
| `DBFilesClient/BattlemasterList.dbc` | **patch-enUS-A 1.00190** | Row 889 "Solo Shuffle", a copy of All Arenas (6) with levels 10-80. Byte-identical to tc-centurion's server copy; tc-centuriondev got the same row added to its own file. Without the row the client cannot show the queue status for type 889. |

Built from the live 1.00189 copies (ArenaFrame.lua `eeaa45bb...`, BattlemasterList.dbc
`ef63f3da...`); `spec_soloshuffle_A.json` is the `publish_files.py` spec that made 1.00190,
with backups `~/patch-backups/patch-enUS-A.*.bak-solo-shuffle-1.00189`.
`spec_soloshuffle2_A.json` made 1.00192 (2v2 row, ArenaFrame.lua `50539024...` built on 1.00190's
`71bc5d05...`), backups `*.bak-solo-shuffle-2v2-1.00191`.

**Groups (staged 2026-10-18, NOT published yet - ships with the server build that takes
shuffle parties):** `spec_soloshuffle_groups_A.json`, built on live **1.00198**
(ArenaFrame.lua `50539024...`, BattlemasterList.dbc `7f8c354d...`).
- ArenaFrame.lua -> `43417c97...`: Join as Group shows in shuffle mode (enabled for a party or
  raid leader), the header reads "Solo or grouped. New teams every round.", and a party leader
  opening the shuffle window lands on 3v3 instead of the ordinary window's 2v2 practice default.
- BattlemasterList.dbc -> `f91d347d...`: row **6** (All Arenas) MaxGroupSize 5 -> 6, one byte
  (offset 772). The client refuses Join as Group by itself when the party or raid is bigger than
  the MaxGroupSize of the list it is showing (Wow.exe `0x54CCC5`: record `+0x30` against
  GetNumPartyMembers `0x52BD10` and the raid count `[0xBEB608]`), and the server sends every arena
  window, the shuffle's included, as row 6. Six is a raid. The server no longer reads MaxGroupSize
  for arenas at all (`Group::CanJoinBattlegroundQueue`), so its copy stays as it is.
- Tested on the server's liblua5.3 (`tests/arena_group_test.lua`, run with staged-ctf-flags/tests/lua_run.py):
  17/17 on the new file; the live file fails the 7 new-behaviour checks and passes the 10 others.

`bml.py` dumps BattlemasterList.dbc and appends a row copied from another (`add <in> <out>
<newId> <copyFromId> <name> [maxGroupSize]`). `render/render_shuffle.py` draws the window
next to the ordinary one with `staged-centurion-ui/render`; `render/solo_shuffle_window.png`
is its output.

The in-match enemy frames need nothing new: the server feeds a shuffle's two sides to
`CenturionSquadFrames.lua` (staged-fourcorners) as two squads, re-sent every round.

**Wins column (2026-10-23, patch-enUS-A 1.00241, from live 1.00240):** `CenturionSquadFrames.lua`
(staged-fourcorners) `8921d337...` -> `ce9cbbca...` shows each player's rounds won as a "Wins"
column on the shuffle's scoreboard, in the column an unrated arena leaves hidden (rating change),
and widens the frame one column for it. Fed by the server's new `SQV:<squad>:<wins,...>` line
(tc-lplus 57b0876201); without it the scoreboard is unchanged. Tests: squadframes_test.lua 67/67.
Backups `~/patch-backups/patch-enUS-A.*.bak-shuffle-wins-1.00240`.

**Rounds Won next to the names (2026-10-28, patch-enUS-A 1.00255, from live 1.00254):** the owner
read the far-right "Wins" column as missing and the Killing Blows column as rounds won (in a 3v3
every round is exactly three deaths, so the lobby's killing blows total its round wins). Now
`Name | Rounds Won | Killing Blows | Damage Done | Healing Done`: the hook anchors the borrowed
column to the names (+4) and Killing Blows to it (-5), in that order (the other order is an
anchor loop while the column still trails Healing Done), retitles it "Rounds\nWon" like the
stock two-line headers, and clears its sortType (the client cannot sort by it). The stock
update re-anchors both columns every refresh, so the next arena or battleground gets its own
layout back. `CenturionSquadFrames.lua` `ce9cbbca...` -> `624d3191...`; spec
`spec_shuffle_roundswon_A.json`. Tests: squadframes_test.lua 73/73 (the 1.00254 file fails only
the 5 new checks); also run against the real live WorldStateFrame.lua with an anchor-loop
check. Verified over HTTP. Backups `~/patch-backups/patch-enUS-A.*.bak-shuffle-roundswon-1.00254`.
