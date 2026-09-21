# Centurion UI changes (shipped 2026-09-16)

These are the exact files that were published, and the generators that build them.

| file | archive | what |
|---|---|---|
| `Interface/GlueXML/CharacterCreate.xml/.lua` | **patch-enUS-6 1.00009** (replaced in place; the owner's CharacterCreate lives here) | "Tournament character" checkbox next to the name box; a Challenges panel above it with one checkbox per challenge mode. Ticking the tournament box unticks every challenge and ticking a challenge unticks it; ticking one of an exclusive pair locks the other. Wrapping tooltips. Centurion/CenturionDev only |
| `Interface/GlueXML/CharacterSelect.xml/.lua` | **patch-enUS-6 1.00009** (new there; stock copies are in patch-enUS-3) | Up to 20 characters: 10 rows (9 while the create button shows), scroll bar, mouse wheel. World/Tournament badge per character. Move up/down buttons on the selected character and Shift+Up/Down. Centurion/CenturionDev only; other realms get the stock behaviour |
| `Interface/FrameXML/PVPBattlegroundFrame.xml/.lua` | **patch-Y** (published as `patch-Y.zip`; the launcher writes it to disk as `patch-X.MPQ` since 2026-09-17, see `FileMap['patch-X']`) | "Gurubashi chest" and "Tournament queue" checkboxes in place of the Wintergrasp timer, hidden until the server answers `CCGAMEREQ GURUCHEST` / `TQUEUE`. The tournament box is locked for nearly everyone, so it keeps its mouse scripts while disabled (`SetMotionScriptsWhileDisabled`, present in this client) and its tooltip spells out why it is locked and what a tournament match does to your gear, read from the `TQUEUEWHY` line. **Shipped** — the copies in `Interface/` are md5-identical to the live archive |
| `Interface/FrameXML/ArenaFrame.lua` | **patch-enUS-A 1.00130** (2026-09-19, `spec_arenabots_A.json`) | "Arena bot matches" (`CCGAMEREQ ARENABOTS`): whether an unrated arena queue may be popped as a clone-filled skirmish. It sits on the **Practice Battle** line, right-aligned at `RIGHT, ArenaFrame, TOPRIGHT, -40, -177`, because a skirmish is the only queue it touches. `ArenaFrame.xml` is stock and stays that way — the box is built in Lua like the frame's own extra zone buttons, and everything hangs off `ArenaFrame_OnLoad` because the `<Script>` tag runs the file before the frame exists. Hidden until the server answers |

A third checkbox in the Battlegrounds tab was tried first (patch-Y 1.00023) and **backed out
again in 1.00024**: the band above the battleground list only holds two boxes — the frame's
close button ends at -40 and the list starts at -79 — so the third one straddled the list
border and the list had to be pushed down a row. The arena frame is both roomier and the
right place for it. `make_ui.py` reproduces the pre-1.00023 bytes exactly
(`eb817939d1ca45276354a8efc06ed2db` / `b31cc01aebb70bda64fbb3297ac9da55`), which is what
`spec_arenabots_revert_Y.json` puts back.

`render/` holds the tool that caught that collision: `frame_render.py` (PVPBattlegroundFrame)
and `arena_render.py` (ArenaFrame) draw either frame 1:1 from the client's own BLPs and
FRIZQT__.TTF, so a layout can be judged before it costs a patch push. `candidates.py` renders
several layouts side by side. Art and font were pulled out of the stock archives with
`mpqread.py` + `blp.py` (mpqtool cannot read those; the python reader can) and are cached in
`render/art/`. **Use it before changing either frame.**

`render/lua_compile.py` is the other check worth running: copy it and the .lua to the game
server and `python3 lua_compile.py <file>` compiles the chunk with the box's
`liblua5.3.so.0`. `lua_check.py` here is only structural and will pass a file that does not
parse.

## What the screens depend on

- **Server** (branch LEGIONNAIRE_PLUS, `game/Miscellaneous/CharacterScreen.{h,cpp}`):
  `CharactersPerRealm` up to 20; `Centurion.CharacterSelect.Reorder`
  (table `character_select_order`); `Centurion.CharacterSelect.TournamentZoneId = 4658`,
  so a tournament character's zone arrives as "Argent Tournament Grounds", the only
  field the glue screens can read it from; `Centurion.CharacterCreate.Challenges`.
- **Server** (`game/Miscellaneous/TournamentMode.{h,cpp}`) for the Battlegrounds tab:
  `CCGAME TQUEUE:<on>:<locked>` followed by
  `CCGAME TQUEUEWHY:<QueueLockReason>:<QueueMinLevel>:<QueueRuleFlags>`. The second
  line is what the tooltip says; a client that never hears it shows the box with the
  plain description instead of nothing, and a server that never sends it is a server
  where the box was already hidden.
- **Wow.exe**: the character list is capped at 10 at 0x464C4C. The launcher's
  `patcher.ts` writes 0x14 at file offset 0x6404F.
- **client-tweaks DLL** (`clientedits/src/gluebridge.cpp`): registers
  `CenturionGlueRequest(text)`, which sends text to the server on opcode 0x002. The
  reorder buttons and the Challenges panel only appear when it exists.
  - `ORDER\t<name>,<name>,...`: every character on the account, top to bottom.
  - `CREATE\t<name>\t<mask>`: challenge modes for the character about to be created,
    bit n = ChallengeModeSettings n.
  - `SURNAME\t<name>\t<surname>`: the family name for the character about to be created
    on the create screen, or renamed on the rename prompt
    (`game/Miscellaneous/Surnames.h`).

## Why these archives

The client ranks patch archives by the **last character of the name only**: patch-enUS-A
ranks as A, patch-Y as Y. The `Data\` vs `Data\enUS\` folder and the locale part don't
count. A change goes into the highest-ranked archive that already carries the file:
PVPBattlegroundFrame is in patch-Y (Y outranks the copies in A and 8); CharacterCreate
is only in patch-enUS-6. CharacterSelect was only in the stock patches, so it went into
patch-enUS-6 alongside CharacterCreate.

## Surnames (shipped 2026-09-20 — `spec_surnames_6.json`, `spec_surnames_A.json`)

`make_charcreate.py` also builds a **LAST NAME** box beside the name box, for
`Centurion.Surnames.Enable` on the server (`game/Miscellaneous/Surnames.h`): the surname
is a second column on `characters`, pasted onto the name on the way out to the client, so
first names stay unique and stay the key everything is looked up by.

- The box is `CharacterCreateSurnameEdit`, a copy of `CharacterCreateNameEdit`. It is
  `hidden` in the XML and placed from Lua, so a realm without surnames, and a paid
  customize/rename, keep the stock screen exactly.
- When it is up, the name box slides to `BOTTOM (-82, 55)` so the **pair** is centred, and
  the tournament checkbox re-anchors to the right of the new box (the challenges panel
  hangs off the checkbox and follows on its own). Checked in game: it fits.
- The name box's own label is the `NAME` global ("Name"). It is given a name here
  (`CharacterCreateNameEditLabel`) so Lua can make it read "First Name" while the second
  box is up, and leave it alone otherwise.
- A **Randomize** button under the last name box, built like the stock one under the name
  box and a child of the last name box so it comes and goes with it. Its names are read
  out of the server's naming tool (`tools/surnames/surnames.py` in the server tree) at
  build time, keyed by `GetNameForRace()`, so both sides hand out the same names.
- Tab moves between the two boxes; Enter and Escape behave as they do in the name box.
- It travels like the challenge modes, `CenturionGlueRequest("SURNAME\t<name>\t<surname>")`
  just before `CreateCharacter`, so it is hidden without the client-tweaks DLL.
- The client checks what it can (2-12 letters, no three of the same in a row) and says so
  in a dialog rather than letting the server drop it silently, since the glue screens have
  no way to hear an answer back.

**CharacterSelect** changed too, and its generator now has the input it was missing:

- The character list arrives with the surname on it (`Player::BuildEnumData`), so
  "Elgrom Fernbloom" ran into the World/Tournament badge, which was right-aligned on the
  name line. Right-aligning it on the zone line instead put it under the selected row's
  move buttons, which sit at the right edge of every line. It now sits at the START of the
  zone line, in place of a tournament character's zone - that zone was only ever
  "Tournament grounds", the server's way of flagging the character. A world character has
  no badge at all; its NAME is drawn blue instead.
- The rename prompt (`CharacterRenameDialog`) gets a **LAST NAME** box of its own, since a
  rename takes the whole name and a last name is required. It starts out holding the
  character's current one. `RenameCharacter` is wrapped to send
  `SURNAME\t<new name>\t<surname>` first and to refuse, inside the prompt, a last name the
  server would not take - a glue dialog can open underneath the prompt, so the reason is
  written into the instructions line in red instead.

The **whisper box** is the third piece, and it is FrameXML, not glue:
`framexml/CenturionWhisper.lua` in **patch-enUS-A 1.00137** with its `FrameXML.toc` line.
3.3.5 only keeps a two-word whisper target together when the pair is on its autocomplete
list (friends, guild, group); anybody else was cut at the space, leaving "Tell Elgrom:"
with "Fernbloom" at the front of the message. On the Centurion realms the box waits for
the second word and takes both. A character with no family name still gets the whisper:
the server hands the second word back (`Surnames::SplitWhisperTarget`).

## Rebuilding

Extract fresh copies of the live sources into `ui/` (next to the scripts): patch-enUS-6's
CharacterCreate into `ui/Interface/GlueXML`, patch-enUS-3's CharacterSelect into the same
folder, patch-Y's PVPBattlegroundFrame into `ui/Y/Interface/FrameXML`. Then run
`make_ui.py`, `make_charcreate.py` and `make_charselect.py`, followed by `lua_check.py`
(a structural check; there is no Lua interpreter on either machine).

`ui/Interface/GlueXML/CharacterCreate.{lua,xml}` were never extracted and are now
**reconstructed**: the generator only inserts, so the input is the published output minus
exactly the blocks it adds. Re-running `make_charcreate.py` on them reproduces the
published files byte for byte (`15724a39…` / `e6893342…`), which is what proves the
reconstruction. CharacterSelect's input is still missing; the same trick recovers it.

`publish_files.py <spec.json>` replaces or adds files in a published archive server-side
with smpq. It refuses to publish if a live copy is not the md5 the change was built from.
It checks the listing, reads every file back and compares untouched samples before
swapping the archive in. Backups sit next to the live zip
(`.bak-<change>-<oldversion>`), and patch-Y's in `/home/brokilodeluxe/patch-backups`.
