# Heirloom link colour fix (2026-09-16)

Owner report: `You receive item: ffe6cc80[Legionnaire Mark of Honor].` Heirloom links showed the colour digits as text. Only artifact gear is supposed to be recoloured.

## Cause

Two layers wrote `ITEM_QUALITY_COLORS[7]` in different shapes:

- patch-enUS-A's `Interface/FrameXML/BPlusItemQualityColors.lua` (B+/Centurion only) stores the stock 3.3.5 shape, `hex = "|cffe6cc80"`. Its chat filter rebuilds every item link as `hex .. "|Hitem:...|h"`.
- The `CENTURION_HeirloomColorFix` addon (every realm) loads after FrameXML. At load and again on `VARIABLES_LOADED`, it always overwrote the heirloom entry with `hex = "ffe6cc80"`, which has no `|c`.

The filter then printed `ffe6cc80|Hitem:...` and the client drew the digits. On realms without the FrameXML module (L+ etc.), the same bad hex broke the heirloom loot-bind and roll popups (`ffe6cc80Name`). Client facts: `clientedits/docs/ITEM_QUALITY_COLORS_RE.md`.

## Fix

`addon/CENTURION_HeirloomColorFix/CENTURION_HeirloomColorFix.lua` fills heirloom only when nothing has filled it, using `GetItemQualityColor(7)` in the client's own shape. The .toc is unchanged. patch-enUS-A is untouched.

Published as **addons 1.00066 -> 1.00067**. Backups sit next to the live files: `addons.zip.bak-heirloomhex-1.00066` and `addons.version.bak-heirloomhex-1.00066`. The only member that changed is this .lua; all other members match by CRC and the zip still holds 89 files. Both localhost and centurionpvp.com serve the new md5 `10e72f1c36a56e695b4a213c9ce85dfb`.

`build_addons.sh` / `install_addons.sh` are the exact scripts that ran, via `plink claudeops@192.168.1.226 'sudo -n -u brokilodeluxe bash -s' < script`. They expect the tested addon at `/tmp/claude-iqc/addon_fixed.lua`.

## Test

`tests/iqc_test.lua` runs the live `BPlusItemQualityColors.lua` together with the live addon and the fixed addon, using real Lua 5.3 on the game server (see `staged-ctf-flags/tests/lua_run.py`). It mocks `GetItemQualityColor` exactly as disassembled and mirrors the stock UIParent loop and ChatFrame filter loop.

    python3 lua_run.py iqc_test.lua BPLUS=<BPlusItemQualityColors.lua> ADDON_LIVE=<old addon> ADDON_FIXED=<new addon>

Result on 2026-09-16: `fail=0`. The live pair reproduces the reported line byte for byte. With the fixed addon, heirloom stays `|cffe6cc80`, artifact becomes `|cffe64545` and epic is left alone. That holds on the B+/Centurion chain in both event dispatch orders and on the L+ chain (addon alone).
