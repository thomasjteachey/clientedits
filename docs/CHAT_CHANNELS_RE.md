# Chat channels and the saved chat config — RE notes

Wow.exe 3.3.5a 12340. Written 2026-09-16 while fixing the World channel
(server commit `9a750a6c13`). Everything below was read from the binary with
`reversing/peexplore.py` and cross-checked against real `chat-cache.txt` files
and `character_account_data` rows; addresses are verified unless marked.

## The rules that matter server-side

1. **The client joins its default channels from the saved chat config, not
   from ChatChannels.dbc.** At login it joins every DBC row whose bit is set in
   the top-level `ZONECHANNELS` mask of `chat-cache.txt` (bit = `1 << (id-1)`).
   The DBC `INITIAL` flag (0x1) only seeds that mask for a character with no
   saved config. A config saved before a new default channel existed never gains
   it — the client saves back the mask it loaded.
2. **The client ignores a "you joined" notice for a channel it did not ask for.**
   `SMSG_CHANNEL_NOTIFY` YOU_JOINED only confirms an entry the client created
   itself when it sent `CMSG_JOIN_CHANNEL`. If the server joins a player to a
   channel on its own, the server counts them in and the client shows nothing.
3. **A pending entry is a dead end.** If the client sent a join and the server
   never answered (e.g. it silently returned because the player was already a
   member), the entry stays pending: a later `/join` finds it and sends nothing,
   and `/leave` refuses to leave it. Rules 2 + 3 together are why `/join World`
   did nothing.
4. **A newer server timestamp makes the client download the saved config.** So
   the server can rewrite `character_account_data` type 7 and have it take effect
   at the very next login — the login channel joins wait for the config to load.

## Lua API (registration table around `0x00AC7AA0`, `{name*, fn}` pairs)

| Lua | handler |
|---|---|
| `JoinChannelByName`, `JoinTemporaryChannel` | `0x0050DAA0` (push 0 → `0x50D900`) |
| `JoinPermanentChannel` | `0x0050DAC0` (push 1 → `0x50D900`) |
| `LeaveChannelByName` | `0x005096D0` → `0x5093F0(name, 1, 0)` |
| `EnumerateServerChannels` | `0x004FF200` |
| `GetChannelList` | `0x004FE650` |
| `GetChannelName` | `0x004FE850` |
| `AddChatWindowChannel` | `0x00501DB0` |
| `GetChatWindowChannels` | `0x004FEFF0` |

`0x50D900` (Script join, `esi` = lua_State, `[ebp+8]` = permanent): arg1 name,
arg2 password (default `""`), arg3 frame id (1-based → index), arg4 voice. Fails
(returns nothing → Lua prints "Invalid channel name") on a space in the name,
name/password ≥ 0x80, or the profanity filter `0x509A50` (error string 0x24E).
Returns `(zoneChannelId, channelName)` on success — including when nothing was
sent because the channel was already in the list.

## Core join — `0x00507A80`

`bool Join(name, password, frameIndex, arg4, char const** outName, int* outZoneId,
uint8 autoJoin, uint8 voice, bool permanent, arg10)`, cdecl.

- Current zone name `[0xBD0780]`, area id `[0xBD080C]` (AreaTable lookup
  `[0xAD3144]` min / `[0xAD3140]` max / `[0xAD3154]` index).
- Walks the in-memory ChatChannels store: count `[0xAD33C4]`, records
  `[0xAD33D8]`, **record size 0x14** = `ID, Flags, FactionGroup, Name, Shortcut`
  (one locale). Case-insensitive compare of the typed name with **Shortcut**
  (`+0x10`). On a match: `*outName = Shortcut`, `*outZoneId = ID`, and the real
  name is `sprintf(Name, zoneName)` — or the city string `[[0xB74464]+0x2C]` when
  the row has `CITY_ONLY2` (0x20) and the area has flag 0x100. GUILD_REQ rows
  touch the guild-recruitment option (`0x508260`).
- Already in the list? zone channel: scan entries for `+0x94 == id`; custom:
  `FindByName 0x4FDFC0`. If found → skip straight to the window add, **no packet**.
- Otherwise `AddPending 0x5076B0` then `CMSG_JOIN_CHANNEL (0x097)`:
  `uint32 zoneId, uint8 autoJoin, uint8 voice, cstring name, cstring password`
  (autoJoin is 1 for the login joins, 0 for `/join`).
- Finally, if `frameIndex < 10`: `0x501A70(window[frameIndex], name, zoneId)`
  (window `n` = `0xBCF0B0 + n*0x100`) and mark the config dirty `[0xBCEFE0] = 1`.

## Joined-channel list

Count `[0xBCF090]`, entries `[0xBCF094]`, **0xA8 bytes each**, at most 10
(error string 0x1BA when full):

| off | field |
|---|---|
| +0x00 | channel number (1-based slot; 0 = free) |
| +0x04 | name, char[0x80] (rewritten by the server's name on confirm) |
| +0x90 | byte from the notify (channel flags) |
| +0x94 | zone channel id (ChatChannels id; 0 = custom) |
| +0x98 | uint32 from the notify |
| +0x9C | **pending** — 1 from AddPending until YOU_JOINED |
| +0xA0 | **leaving** — set by a user leave |
| +0xA4 | voice |
| +0xA5 | permanent (only these custom channels are saved) |

`FindByName 0x4FDFC0` (case-insensitive), `FindByZoneId 0x4FE020`.
`AddPending 0x5076B0(name, zoneId, arg, voice, permanent, slotHint)` refuses a
duplicate name or zone id, allocates the first free slot, sets pending = 1.

## `ZONECHANNELS` mask — `[0x00BCF010]`

| who | where | what |
|---|---|---|
| defaults (no saved config) | `0x501F00` | every row with `Flags & 1`: assert `id < 32`, set bit, add to **window 1** |
| ADDEDVERSION upgrade | parser near `0x508B18` | table `0x9FB9E8` of `{channelId, addedInVersion}` — one entry, `{26 LookingForGroup, 4}`; current version is 13. Sets the bit and adds to window 1, i.e. what `AddWorldChannelToChatCache` now does server-side for World |
| confirm join | `0x507880` (`or` at `0x5078D2`) | set bit when YOU_JOINED confirms a zone channel |
| user leave | `0x5093F0` (`and` at `0x509639`) | clear bit |
| writer | `0x4FD300` / `0x4FDB2F` | `ZONECHANNELS %d` (signed) |
| reset | `0x50F120` | mask, `[0xBCEFE4]`, `[0xBCEFE8]` zeroed at (re)login |

## Login joins — `0x00508090`

Runs when the chat config has been parsed (`[0xBCEFE4]`, set at `0x5093B9`
immediately before the call at the end of the parser `0x508320`) and not yet
done this session (`[0xBCEFE8]`). Also retried from `0x51D915` and `0x729002`.
For each ChatChannels row with its mask bit set: `Join(Shortcut, "", 10, 0,
..., autoJoin=1, voice=0, permanent=1)`. Then the saved custom channels
(count `[0xBCFAB4]`, names `[0xBCFAB8]`).

## `SMSG_CHANNEL_NOTIFY` — handler `0x0050E120`

Reads `uint8 type`, `cstring name`, then jumps through `0x50EB08` (type ≤ 0x24).

- **YOU_JOINED** (case `0x50E1E4`): `uint8 flags, uint32 zoneId, uint32 unk`,
  optional string. Entry by zone id (or name for custom). Entry present and not
  pending → notice "YOU_CHANGED"; otherwise "YOU_JOINED". Then `ConfirmJoin
  0x507880(zoneId, name, unk, flags)`, which **returns without doing anything when
  there is no entry**.
- **YOU_LEFT** (case `0x50E2DE`): `uint32 zoneId, uint8 isConstant`. If the entry
  is not leaving and isConstant → notice "SUSPENDED", entry back to pending
  (`0x507900`) — how Trade survives walking out of a city. Otherwise "YOU_LEFT"
  and the entry is removed (`0x507A30`, from the handler tail).
- Every notice is posted to Lua by `0x509DD0` (`CHAT_MSG_CHANNEL_NOTICE`).
  `ChatFrame.lua` drops channel messages whose channel is not in that frame's
  `channelList` / `zoneChannelList`.

## Leave — `0x005093F0(name, arg2, force)`

Resolves a number to a name (`0x4FE160`) unless forced, formats zone names like
the join, removes the channel from **all 10 windows** (`0x4FD260`), then finds the
entry. A pending entry is **not left** unless `force`. Otherwise sends
`CMSG_LEAVE_CHANNEL (0x098)`: `uint32 zoneId, cstring name`, sets leaving = 1,
clears the mask bit, marks the config dirty.

## chat-cache.txt layout (writer `0x4FD300`)

LF line endings, as stored in `character_account_data.data` (type 7) byte for
byte:

```
VERSION 5
ADDEDVERSION 13
OPTION_GUILD_RECRUITMENT_CHANNEL AUTO   (optional)
CHANNELS            custom permanent channels only: "<name> <voice> <number>"
END
ZONECHANNELS <mask>
COLORS ... END
WINDOW 1 .. WINDOW 10, each:
  NAME/SIZE/COLOR/.../MESSAGES ... END
  CHANNELS          channels shown in the tab, by Shortcut name (LookingForGroup, World)
  END
  ZONECHANNELS <mask>   per-window zone channels (General/Trade/LocalDefense)
  END
```

(blank lines between blocks omitted above.)

## Account data sync

- `SMSG_ACCOUNT_DATA_TIMES (0x209)` handler `0x6B94C0`: slots at `0xAD94E1`,
  step 0x40, eight types. Per type: server time newer than local → request the
  server copy (`0x6B92C0`, `CMSG_REQUEST_ACCOUNT_DATA`); local newer or dirty →
  upload (`0x6B9100`, `CMSG_UPDATE_ACCOUNT_DATA`).
- `SMSG_UPDATE_ACCOUNT_DATA (0x20C)` handler `0x6B9730`.
- The local timestamps live in `WTF\Account\<acct>\<realm>\<char>\cache.md5`:
  20 bytes per type (`md5[16]` + `uint32 time`); the per-character file holds
  types 1, 3, 5, 6, 7 in that order. The times are server time (they match the
  `character_account_data.time` of the same upload).
