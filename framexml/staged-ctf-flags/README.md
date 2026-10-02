# CTF flag icons and PvP portrait (shipped 2026-09-16)

| file | archive | change |
|---|---|---|
| `Interface/FrameXML/WorldStateFrame.lua` | **patch-enUS-A 1.00106** (Barracks+, Centurion, CenturionDev) and **patch-enUS-8 1.00371** (Legionnaire+), replaced in place | cross-faction WSG/Twin Peaks flag icons on the world map and battlefield minimap |
| `Interface/FrameXML/BPlusPVPPortraitFix.lua` + a `FrameXML.toc` line | same two archives, new file | PvP window portrait no longer hollow |
| server: push flag carrier positions on every flag event | `server/ctf-flag-positions-push.patch` (branch `claude/ctf-flag-positions-push` off LEGIONNAIRE_PLUS 9a2100b13b, not built) | closes the last stale-marker window, see below |

Backups: `patch-enUS-A.zip.bak-ctf-flags-portrait-1.00105` and `patch-enUS-8.zip.bak-ctf-flags-portrait-1.00370` (plus `.version` backups) next to the live zips. patch-Y was not touched.

## What was wrong

The CWSG flag correction does not live in `BattlefieldFrame.lua`. That file's "flag-state bridge" (patch-enUS-7 2026-03-24, A/8 later) is dead code: patch-Y's stock copy shadows it, and even when loaded, `WorldStateFrame.lua` loads after it (toc lines 52 / 123) and redefines the functions the maps call. The working implementation is `WorldStateFrame.lua` in A/8, drawn by patch-enUS-7's `WorldMapFrame.lua` and `Blizzard_BattlefieldMinimap.lua`. It mostly worked; two cases failed, both traced in Wow.exe (see `clientedits/docs/BATTLEFIELD_FLAG_POSITIONS_RE.md`):

1. **Two carriers: icons swapped.** The server writes the physical Alliance-flag carrier first. The client's MSG_BATTLEGROUND_PLAYER_POSITIONS handler stores N carriers from slot N-1 down to 0, so `GetBattlefieldFlagPosition(1)` is the Horde-flag carrier. The old code assumed the opposite.
2. **Dropped/captured flag still shown on its carrier.** The client only asks for carrier positions from its map UI, at most once every 5 s, and nobody tells it a flag changed hands. CWSG already knew, but the stale carrier marker was still drawn. The fix drops markers CWSG says are no longer on a player (in place in the caller's list, so the dropped-flag marker moves up into the two drawn frames), and remembers which flag was carried alone while a second pickup has not reached the slots.

Remaining without the server patch: a flag picked up again before the client's next refresh can show on the previous carrier until then. The server patch pushes fresh positions with every flag event (the client applies the packet unasked).

The stock client token is race-based (carrier race -> ChrRaces faction -> FactionTemplate group), so on an all-Alliance-race realm every stock icon is "HordeFlag"; the Lua has to supply every colour.

The portrait block used to sit at the top of A/8's `PVPBattlegroundFrame.lua`, shadowed by patch-Y's copy, so it had never loaded. It is now its own toc entry.

## Verification done

- `tests/ctf_test.lua` runs the real `WorldStateFrame.lua` in Lua 5.3 (the server's `liblua5.3.so.0` via ctypes, `tests/ctf_test.py`) against the real patch-enUS-7 map and minimap functions, with the client slot model from the disassembly. Live copy: 8/16 scenarios fail, reproducing both symptoms. Fixed copy: 16/16 on the map and the minimap. Loading the old bridge first changes nothing that is drawn (same 8/16) and adds `SetMapToCurrentZone` calls.
- `tests/portrait_test.lua`: 9/9 against mocked stock PvP frames.
- Publish via `/tmp/centmode/uipub/publish_files.py` (smpq, base md5s pinned). Afterwards every file of both new archives was extracted and diffed against the backups: only these three files differ.
- Server patch: `/Zs` and `.obj` builds of the four changed TUs, `dumpbin` shows the new symbols defined and referenced as expected.

## Rebuild

`python build_ctf.py` rebuilds `shipped-rebuild/` from `inputs/` (the live copies, md5-pinned) plus `wsf_fixed_tokens.lua` / `wsf_note_single.lua`; the output is byte-identical to `shipped/`. Tests run on the game server:
`python3 tests/ctf_test.py label=/path/WorldStateFrame.lua[+/path/BattlefieldFrame.lua]` (expects the patch-enUS-7 extracts under `/tmp/centmode/bffmerge/x/patch-enUS-7.zip/`, produced by `tests/srv_scan2.py`).
