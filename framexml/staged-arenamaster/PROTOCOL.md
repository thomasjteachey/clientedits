# Arena Master — addon protocol (server ⇄ CENTURION_ArenaMaster)

All lines are addon whispers to self, LANG_ADDON, as the realm's other addons do:

* server → client: prefix `CCGAME`, body below (`CHAT_MSG_ADDON` prefix "CCGAME", message = body).
* client → server: `SendAddonMessage("CCGAMEREQ", body, "WHISPER", UnitName("player"))`.

Fields are `:`-separated. Names never contain `:`; the server strips `:` `|` `;` `,` from free text.

## Opening

A plain arena battlemaster (BATTLEGROUND_AA — every one left after the cleanup is the Arena Master)
sends, before SMSG_BATTLEFIELD_LIST:

```
ARENAQUEUE:0          resets the old FrameXML ArenaFrame.lua to its ordinary mode (clients without the addon)
ARENAQUEUE:MASTER     the addon takes over this window
AM:MODES:<rated>:<skirmish>:<multi>:<shuffle>:<allstar>:<replays>:<squads>
                      each 0|1 = mode available (not disabled); squads = Four Corners team count (4)
```

then, a moment later (async character query):

```
AM:LINEUP:<shuffled 0|1>:<class>,<name>;<class>,<name>;...
                      the All-Star lives in life order: life 1 = this character, then the account's
                      level-60 characters (one per class, character-list order), then the classes nobody
                      has, with an EMPTY name = the class's standard kit. class = numeric class id.
                      shuffled = Centurion.AllStar.ShuffleLives (lives after the first are shuffled each match).
```

The stock ArenaFrame still opens (BATTLEFIELDS_SHOW); in MASTER mode the addon hides it at once
(HideUIPanel) and shows the Arena Master instead. The flag clears when the Arena Master closes.

## Joining

```
client: ARENAMASTER:JOIN:<mode>:<size>:<group 0|1>
   mode R  = rated arena (your arena team of <size>, always as group)       size 2..5
        S  = skirmish                                                       size 2..5
        MR = Four Corners rated (arena team of <size> = one squad)          size 2..5
        MS = Four Corners skirmish                                          size 2..5
        B  = Buffle Shuffle (party up to 2*size)                            size 2|3
        A  = All-Star                                                       size 1..5
```

The server runs the ordinary arena join (same checks and error packets as the old window) against the
Arena Master the player last spoke to; too far away → UI error "Speak to an Arena Master first.".
The window closes itself after sending a join.

`ARENABOTS` / `ARENABOTS:0|1` → `ARENABOTS:<on>` is the existing "Arena bot matches" toggle; it applies
to every unrated queue (skirmish, Four Corners skirmish, Buffle Shuffle, All-Star).

## Replays

```
client: REPLAYS:LIST:RECENT          last 10 recorded matches
client: REPLAYS:LIST:FAV             this character's favorites (latest 20)
server: RPL:BEGIN:<RECENT|FAV>
server: RPL:R:<id>:<arenaType>:<typeId>:<mapId>:<ageSec>:<watched>:<fav 0|1>:<rated 0|1>:<wRating>:<lRating>
server: RPL:T:<id>:<W|L>:<team name>         rated arena team name (empty for a skirmish)
server: RPL:P:<id>:<W|L>:<class>.<name>,<class>.<name>,...   players (first names), numeric class
server: RPL:END:<RECENT|FAV>:<count>
        rated is explicit (a rated team can sit at rating 0). Arena name/art: CENTURION_ArenaMaster_Maps[mapId]
        (ArenaMaps.lua, generated from Map.dbc + LoadingScreens.dbc; screen "" = none -> fall back).

client: REPLAYS:WATCH:<id>           starts the replay (Arena Master must be in reach)
client: REPLAYS:FAV:<id>:<0|1>       favorite / unfavorite
server: RPL:FAV:<id>:<0|1>           done
server: RPL:ERR:<text>               e.g. "There is no replay 4807."
```

Replays are only recorded when both teams are fully human (BGReplay.cpp skips custom games, bot-filled
matches and any match with a bot in it).
