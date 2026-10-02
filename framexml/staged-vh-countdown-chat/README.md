# Violet Hold wave countdown: centre screen only (patch-enUS-A 1.00136, 2026-09-20)

Owner: *"i don't want to see 'Next wave in X...' in my chatlog. only at the top of the screen."*

`BattlegroundVHR::AnnounceWaveCountdown` sends the countdown as
`CHAT_MSG_RAID_BOSS_EMOTE` (trinity_string 20100, `Next wave in %u...`) because that
is the only message type the 3.3.5 client draws as large centre-screen text. The
server sends **one** packet; the client shows it in **two** places:

- `RaidBossEmoteFrame` registers `CHAT_MSG_RAID_BOSS_EMOTE` itself in
  `RaidWarning.lua` (`RaidBossEmoteFrame_OnLoad`) - that is the centre text, plus the
  `RaidBossEmoteWarning` sound.
- the chat windows are registered for the same event through the message group
  `ChatTypeGroup["MONSTER_BOSS_EMOTE"]`, so every tick also landed in the chat log.

`ChatFrame_AddMessageEventFilter` cuts exactly the second one: filters are consulted
inside `ChatFrame_MessageEventHandler` (patch-enUS-3's `ChatFrame.lua`, ~line 2681),
which `RaidBossEmoteFrame_OnEvent` never goes through. So the countdown keeps its
centre display and loses its chat lines, and no server change was needed.

| file | archive | what |
|---|---|---|
| `Interface/FrameXML/CenturionChatFilters.lua` | **patch-enUS-A 1.00136** (new) | swallows chat lines matching `^Next wave in %d+%.%.%.$` on `CHAT_MSG_RAID_BOSS_EMOTE` |
| `Interface/FrameXML/FrameXML.toc` | **patch-enUS-A 1.00136** | one line added after `BPlusPVPPortraitFix.lua` |

The pattern is anchored to the whole line. If string 20100 is ever reworded, reword
the pattern with it or the chat lines come back - `tests/filter_test.lua` covers that
case explicitly.

## Verifying

```
python3 lua_run.py filter_test.lua FILE=<path>/CenturionChatFilters.lua   # -> pass=12 fail=0
```
(`lua_run.py` lives in `../staged-ctf-flags/tests/`; it needs the game server's
`liblua5.3.so.0`, see the lua-test-harness note.)

Published with `../staged-centurion-ui/publish_files.py spec_vhchat_A.json` on the game
server; backups `~/patch-backups/patch-enUS-A.{zip,version}.bak-vh-countdown-chat-1.00135`.
Archive went 1707 -> 1708 files, five sampled untouched files byte-identical.
