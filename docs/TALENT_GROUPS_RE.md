# Talent groups / dual spec (Wow.exe 3.3.5a 12340)

How the client stores talent groups (dual spec), where two groups are hard-coded, and the exact sites a third spec would have to patch. Found 2026-09-16 while sizing "triple spec"; nothing was patched. Addresses are VAs, image base 0x400000; tool: `clientedits/reversing/peexplore.py`. Everything below was read from the disassembly unless marked *(inferred)*.

## Lua API (registration table `0xAD05F0`)

| function | address | notes |
|---|---|---|
| `GetNumTalentTabs` | `0x5C5CC0` | |
| `GetTalentTabInfo` | `0x5C6150` | |
| `GetNumTalents` | `0x5C5D40` | |
| `GetTalentInfo` | `0x5C7800` | |
| `GetTalentLink` | `0x5C7CE0` | |
| `GetTalentPrereqs` | `0x5C7ED0` | |
| `LearnTalent` | `0x5C80E0` | |
| `GetUnspentTalentPoints` | `0x5C6310` | |
| `GetNumTalentGroups(isInspect, isPet)` | `0x5C5DF0` | inspect -> `[0xC21010]`; pet -> `([0xC21000] && [0xC21028] > 0) ? 1 : 0`; else `[0xC20FF8]` |
| `GetActiveTalentGroup(isInspect, isPet)` | `0x5C5810` | inspect -> `[0xC21004]+1`; pet -> 1; else `[0xC20FF4]+1` |
| `SetActiveTalentGroup(group)` | `0x5C5E70` | see below |
| `GetPreviewTalentPointsSpent` | `0x5C63B0` | |
| `GetGroupPreviewTalentPointsSpent` | `0x5C6420` | |
| `AddPreviewTalentPoints` | `0x5C9590` | group ptr via `0x5C94F0` |
| `ResetPreviewTalentPoints` | `0x5C7130` | |
| `ResetGroupPreviewTalentPoints` | `0x5C7200` | |
| `LearnPreviewTalents` | `0x5C6A10` | active group only (`[0xC20FF4]`) |
| `GetNumGlyphSockets` / `GetGlyphSocketInfo(index, talentGroup)` | `0x5B71E0` / `0x5B7260` | group ptr via `0x5C6080(group, 0, 0)` |

`SetActiveTalentGroup` **does not send a talent opcode, it casts a spell** (on the server, `CMSG_SET_ACTIVE_TALENT_GROUP_OBSOLETE` 0x4C3 is `STATUS_NEVER` / Handle_NULL):

```
idx = arg - 1
if idx == [0xC20FF4] return          ; already active
if idx >= [0xC20FF8] return          ; beyond group count from the packet
0x5C5ED7  83 F8 02   cmp eax, 2      ; HARD CAP: 2 groups
          jae return
0x5C5EDC  8B 34 85 E8 05 AD 00   mov esi, [eax*4 + 0xAD05E8]
          call 0x80DA80(spell, 0, activePlayerGuid)   ; cast by id (inferred)
```

`0xAD05E8` = `{ 63645 "Activate Primary Spec", 63644 "Activate Secondary Spec" }`, exactly two dwords, immediately followed by the registration table at `0xAD05F0` (no room to extend in place).

## Globals

| address | meaning |
|---|---|
| `[0xC20FF0]` | player talent UI model has been built |
| `[0xC20FF4]` | player active group (0-based) |
| `[0xC20FF8]` | player group count, **stored raw from the packet, never clamped** |
| `[0xC20FFC]` | player unspent talent points |
| `[0xC21000]` | pet talent data present *(inferred)* |
| `[0xC21004]` / `[0xC21010]` / `[0xC21014]` | inspect active group / group count / unspent |
| `[0xC21008]` / `[0xC2100C]` | inspect target GUID lo/hi |
| `0xC21018` | player talent UI model (`[0xC2101C]`/`[0xC21020]` and `[0xC21028]`/`[0xC2102C]` are count/ptr arrays; `[0xC21028] > 0` gates pet talents) |
| **`0xC21030`** | **player `TalentGroup[2]`**, stride 0x54 |
| `0xC210D8` | pet `TalentGroup` (directly after the player array) |
| `0xC2112C` | inspect talent UI model |
| **`0xC21138`** | **inspect `TalentGroup[2]`**, stride 0x54 |
| `0xC211E0+` | unrelated data (referenced by `0x5CCDF8`, `0x5CA6C0`) |

## `TalentGroup` (0x54 bytes)

| offset | field |
|---|---|
| +0x00..+0x2F | hash table of learned talents (vtables `0xA198C4`/`0xA19850` at +0/+4, `0xDDDDDDDD` at +8, self-linked list head at +0xC) |
| +0x30 / +0x34 | count / pointer of 0x1C-byte per-tab records (+4 and +8 hold point sums) |
| +0x38 | unspent points for this group (filled by `0x5C9CA0`) |
| +0x3C..+0x53 | glyph ids, 6 x dword |

## Packet path

- `0x5C9E50(CDataStore*)` - player talent info, called from `0x6CD7A0`. Saves old active/count, clears via `0x5C9A30(0)`, parses, rebuilds the UI model if `[0xC20FF0]`, fires event `0x271`, and if the active group changed fires `0x272` with (new+1, old+1) (= `ACTIVE_TALENT_GROUP_CHANGED` *(inferred from args)*).
- `0x5C9740` - shared parser. `ecx` = &active, `eax` = &unspent, `edi` = data store, stack = (groups*, maxGroups, &count). Reads `int32 unspent, uint8 count, uint8 active`; per group `uint8 n, n x (uint32 talentId, uint8 rank)` -> `0x5C9310`, then `uint8 m, m x uint16 glyph` -> `[grp+0x3C+j*4]` for j < 6. **Groups at index >= maxGroups are read and discarded**, but `count` is stored as sent.
- `0x5C9FF0` - inspect reader: clear `0x5C9AD0`, parse into `0xC21138` with max 2, rebuild `0xC2112C`.
- `0x5C9CA0(eax=model, groups*, capacity, active, count, unspent)` - builds the UI model; loops over **capacity**, not count.
- `0x5C9A30(isPet)` - clear: pet -> `0x5C9910(0xC210D8)`, else loops 2 player groups. `0x5C9AD0` - same for the 2 inspect groups.
- Static ctors `0x9CAEA0` (player array), `0x9CB010` (inspect array), `0x9CAFE0` (pet); dtors `0x9D6150`, `0x9D6270`, `0x9D6260`, registered through `0x40C8FA`. They run in the CRT initialisers, i.e. **after** `dinput8.dll`'s `DllMain`, so a DLL can still retarget them.

## Group accessors: bounded by the packet, not by the array

`0x5C5FC0(group, isPet)` returns `0xC21030 + group*0x54` after checking `group < [0xC20FF8]`; `0x5C6080(group, isInspect, isPet)` does the same for inspect against `[0xC21010]`. The same pattern is inlined in `0x5C5F70`, `0x5C6010`, `0x5C6A10`, `0x5C7010`, `0x5C7070`, `0x5C7750`, `0x5C94F0`; `0x5C5BE0` (point sum) and `0x5C70D0` (calls `0x5C6FB0` per group) loop to `[0xC20FF8]`.

**Consequence: if the server sends 3 groups to an unpatched client, group index 2 aliases the pet `TalentGroup` at `0xC210D8` (inspect: `0xC211E0`).** Only send more than 2 groups to clients known to carry a patch.

## Patch sites for N groups (verified bytes, not applied)

Relocate each array to a buffer of N x 0x54 bytes (e.g. in the DLL's .bss) and rewrite:

**Player array `0xC21030` -> NEW**

| site | bytes | rewrite |
|---|---|---|
| `0x5C5C2D` | `BF 64 10 C2 00` mov edi | NEW+0x34 |
| `0x5C5FB1`, `0x5C604D` | `81 C1 30 10 C2 00` add ecx | NEW |
| `0x5C6002`, `0x5C6A77`, `0x5C7059`, `0x5C70B4`, `0x5C778E`, `0x5C9540` | `05 30 10 C2 00` add eax | NEW |
| `0x5C7106` | `B9 30 10 C2 00` mov ecx | NEW |
| `0x5C9A49` / `0x5C9A4E` | `BE 60 10 C2 00` / `C7 45 08 02 00 00 00` | NEW+0x30 / N |
| `0x5C9D95` `0x5C9D97`, `0x5C9E79` `0x5C9E7B`, `0x5C9EAF` `0x5C9EB1` | `6A 02` / `68 30 10 C2 00` | N / NEW |
| `0x9CAEA7` / `0x9CAEAE` (ctor) | `C7 45 FC 01 00 00 00` / `BE 3C 10 C2 00` | N-1 / NEW+0x0C |
| `0x9D6157` / `0x9D615E` (dtor, walks backwards) | `C7 45 FC 01 00 00 00` / `BE E0 10 C2 00` | N-1 / NEW+N*0x54+8 |
| `0x5C5ED7` / `0x5C5EDC` (SetActiveTalentGroup) | `83 F8 02` / `8B 34 85 E8 05 AD 00` | N / new spell-id table |

**Inspect array `0xC21138` -> NEWI** (only needed if inspect is sent more than 2 groups; the 3.3.5a inspect frame shows the active group only, so the server can instead send just that one)

| site | bytes | rewrite |
|---|---|---|
| `0x5C609D`, `0x5C636C`, `0x5C77CD` | `05 38 11 C2 00` add eax | NEWI |
| `0x5C9AD7` / `0x5C9ADC` | `BE 40 11 C2 00` / `C7 45 FC 02 00 00 00` | NEWI+8 / N |
| `0x5C9FCC` `0x5C9FCE`, `0x5CA005` `0x5CA007`, `0x5CA03F` `0x5CA041` | `6A 02` / `68 38 11 C2 00` | N / NEWI |
| `0x9CB017` / `0x9CB01E` (ctor) | `C7 45 FC 01 00 00 00` / `BE 44 11 C2 00` | N-1 / NEWI+0x0C |
| `0x9D6277` / `0x9D627E` (dtor) | `C7 45 FC 01 00 00 00` / `BE E8 11 C2 00` | N-1 / NEWI+N*0x54+8 |

A sweep of `0x5C4400-0x5CA800` for other immediate 2s found only Lua stack indices and unrelated compares (`0x5C5A68`/`0x5C5A91` class-mask bits, `0x5C7978` rank pick, `0x5CA79F`).

## UI side (as of 2026-09-16)

- `Blizzard_TalentUI.lua` winner is a **custom override in patch-enUS-A** (B+, Centurion, Centurion Dev); identical bytes in patch-enUS-8 (L+). It sets `CENTURION_DISABLE_GLYPHS` and `CENTURION_DISABLE_PET_TALENTS`. Its `specs` table has only spec1/spec2/petspec1; `talentSpecInfoCache` likewise; the activate button uses `TALENT_ACTIVATION_SPELLS[spec.talentGroup]`.
- `Constants.lua` (patch-enUS-3): `TALENT_SORT_ORDER = { "spec1", "spec2", "petspec1" }`, `TALENT_ACTIVATION_SPELLS = { 63645, 63644 }`; the talent UI is their only consumer.
- `Blizzard_TalentUI.xml` (stock, patch-enUS): `PlayerSpecTab1` = spec1, `PlayerSpecTab2` = spec2, `PlayerSpecTab3` = petspec1 (hidden while pet talents are disabled). Tabs are re-anchored in Lua; a 4th side tab fits.
- Already generic: `Blizzard_GlyphUI` (uses `PlayerTalentFrame.talentGroup`), `InspectTalentFrame` (active group only), `/usetalents` in `ChatFrame.lua` (no Lua range check), `CENTURION_CharacterPane`.
