# Item quality colours (Wow.exe 3.3.5a 12340)

Where the client gets item quality colours, what shape Lua expects them in, and every custom layer that rewrites them. Found 2026-09-16 while fixing heirloom links printing as `ffe6cc80[Legionnaire Mark of Honor]` (`clientedits/framexml/staged-heirloom-color`). Addresses are VAs, image base 0x400000; tool: `clientedits/reversing/peexplore.py`.

## Tables (verified)

| address | meaning |
|---|---|
| `0xAD2A84 + q*4` | pointer to the hex escape string for quality `q` |
| `0xAD2D84 + q*4` | colour bytes for quality `q`, stored **B, G, R, A** |
| `0xA2462C` | `"\|cffe6cc80"`, shared by quality 6 (artifact) and 7 (heirloom) |

| q | 0 | 1 | 2 | 3 | 4 | 5 | 6 | 7 |
|---|---|---|---|---|---|---|---|---|
| colour | 9d9d9d | ffffff | 1eff00 | 0070dd | a335ee | ff8000 | e6cc80 | e6cc80 |

The strings after index 7 (`66bbff`, `ff89f8`, `71d5ff`, `4e96f7`) belong to something else; the lookups never reach them.

## `GetItemQualityColor(q)` `0x510E20`

Name string `0x9FD88C`, registration entry `0xAC8720`. Converts the argument with `fistp`, then:

- `0x61A510(q)` -> `&[0xAD2D84 + q*4]`, `0x61A530(q)` -> `[0xAD2A84 + q*4]`. Both use an unsigned `cmp q, 8 / jb`, so **any q >= 8 or negative falls back to 1** (white).
- Returns 4 values: `r = byte[2]/255`, `g = byte[1]/255`, `b = byte[0]/255` (constant `0x9ECE88` = 1/255), and the hex string.

**The hex INCLUDES `|c`**: `"|cffe6cc80"`, not `"ffe6cc80"`. (Cataclysm and later return it without, which is where the wrong shape keeps coming from.) Every stock consumer writes `ITEM_QUALITY_COLORS[q].hex .. name .. "|r"`.

## Stock FrameXML (winner: patch-enUS-3)

- `UIParent.lua:96` builds `ITEM_QUALITY_COLORS` for **-1..6 only**, so heirloom (7) has no entry until something fills it.
- `.hex ..` consumers: `UIParent.lua` 583 (LOOT_BIND), 844/846/855/857 (CONFIRM_LOOT_ROLL); `LootFrame.lua` 303 (CONFIRM_LOOT_DISTRIBUTION). `.r/.g/.b` consumers: `LootFrame.lua` 98, 359 (nil-index errors if 7 is missing).
- `ChatFrame.lua:2681` filter loop: each filter gets `(self, event, arg1..arg12)`; a truthy first return drops the message, otherwise a non-nil second return replaces all 12 args. `ChatFrame_AddMessageEventFilter` (2962) ignores a filter already in the list.
- Chat item links are built by the client in C from the table above, so an artifact link arrives `|cffe6cc80` exactly like an heirloom one. Lua can only tell them apart with `GetItemInfo` (nil until the item is cached).

## Custom layers (as of 2026-09-16)

| where | realms | what it does |
|---|---|---|
| `Interface/FrameXML/BPlusItemQualityColors.lua` in patch-enUS-A (toc line after BPlusBeastTrainingTradeSkill.lua) | B+, Centurion, Centurion Dev | fills 7 from `GetItemQualityColor`, overrides 6 to `\|cffe64545`, and a `CHAT_MSG_*` filter rewrites every cached item link's colour from the table. **Not in patch-enUS-8**: L+ has no filter and no FrameXML fill. |
| addon `CENTURION_HeirloomColorFix` (addons.zip) | all | fills 7 only when nothing has; `\|c` shape. Before addons 1.00067 it always overwrote 7 with `"ffe6cc80"`, clobbering the FrameXML entry, so the chat filter emitted `ffe6cc80\|Hitem:...` |
| addon `CENTURION_Tooltips` | all | at `PLAYER_LOGIN` sets 6 to `\|cffff0000`; artifact tooltips' name line red, green lines pink (checks quality == 6). Runs after the FrameXML override, so artifact chat links end up ff0000, not e64545. |
| addon `CENTURION_TemporalTint` | all | red icon wash on quality 6 (heirloom wash exists but is off). |

Any new code writing `ITEM_QUALITY_COLORS[q].hex` must keep the `|c`, and must not overwrite an entry another layer owns: addons load after FrameXML, so an addon always wins.
