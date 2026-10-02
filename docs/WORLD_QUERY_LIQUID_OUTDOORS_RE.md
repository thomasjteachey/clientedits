# World position query: inside-WMO, IsOutdoors (mount gate), liquid/swim — RE notes

Wow.exe 3.3.5a build 12340. Reverse-engineered 2026-08-15 while fixing the Violet
Hold battleground pool (`tc-lplus/tools/violet_hold/vhr_outdoors.py`), whose water
became unswimmable after the WMO group was flagged EXTERIOR for mounting.
Method: `reversing/peexplore.py` (strings/xrefs/callers/func) plus a capstone
sweep of `.text` for immediates and memory-operand patterns.

## The per-unit world query object

`CGUnit_C + 0xB8` points at a query struct that is refreshed every frame:

| offset | meaning |
|---|---|
| +0x0C | bit 1 = query invalid?, **bit 2 = indoors, bit 4 = outdoors** (lighting/fog use), 0x200 = WMO hit |
| +0x18 / +0x20 | link offset / head of the "hit list" (mapobjdefs / group nodes the query landed in) |
| +0x60..+0x68 | probe position, +0x6C..+0x74 unit position |
| +0x7C | flags: **1 = inside a WMO group**, 0x20 = in liquid, 0x40 = under liquid surface, 0x100/0x200 from LiquidType row, 0x1000, 0x2000 (query mode) |
| +0x80 | liquid surface Z |
| +0xB8 | terrain-type / ground material of hit face (MOMT+0x20 via MOPY) |
| +0xBC | u16 liquid type (LiquidType.dbc id) |

`0x7C2F80` (clear list, dispatch) → `0x7C2A70` (normal mode):
casts a vertical segment through the position, `0x7C28F0` → `0x7C2700` iterates
mapobjdefs (`[0xD25440]` global list / tile list), `0x7C25D0` iterates the def's
groups, `0x7C1DC0` runs the group ray test (`0x7CB260`) and fills a 16-byte hit
record `{def, groupdef, z, u16 faceIdx, u16 isInterior}` where
**`isInterior = !(CMapObjGroup+0x30 (MOGP flags) & 0x8 EXTERIOR)`**.
Back in `0x7C2A70`, query flag bit 1 ("inside a WMO group") is set **only if the
best hit's `isInterior` is nonzero**; exterior groups never count as inside.
`0x7C15F0` then sets +0x0C bit 2 (indoors) unless the group flags carry 0x40
EXTERIOR_LIT (→ bit 4). Not inside → +0x0C = ... | 4 (outdoors).

`0x7C25D0` also consults **MOGI** flags (`CMapObj+0x130`, 32 B/entry, via
`0x7AE7B0(mapObj, idx)`): groups with `0x410080` are skipped; groups whose MOGI
lacks EXTERIOR must additionally pass the containment test `0x7AE920` (interior
BSP style); exterior-in-MOGI groups go straight to the ray test.

## CGUnit_C::IsOutdoors — the mount gate

`Script_IsOutdoors 0x612360` / `Script_IsIndoors 0x612300` → `CGUnit_C::IsOutdoors 0x71B7F0`
→ `0x77FBF0(query)`; Spell_C uses the same at `0x80BBFB` when a spell has
ATTR0 INDOORS_ONLY (0x4000) or OUTDOORS_ONLY (0x8000).

```
bool IsOutdoors(query)
  if (!query) return true
  if (0x7A13E0(query, &group, &mapObj, &def, &grp, 0))      // a WMO node is in the hit list == "inside"
      row = WMOAreaTable_bsearch(0x990560)(mapObj->MOHD.wmoID ([+0x120]+0x20),
                                          def->nameSet (+0x104), group->uniqueID (+0x180))
      if (row) return (row->Flags(+0x24) & 4) ? true : false   // Flags&2 or neither → indoors
      return false                                              // no row → indoors
  areaId = 0x7A0490(&pos)                                       // terrain MCNK area
  flags = AreaTable[areaId].Flags(+0x10)
  if (flags & 0x04000000 OUTSIDE) return true
  if (flags & 0x02000000 INSIDE)  return false
  return true
```

`0x990560` is a `bsearch` on the WMOAreaTable rows sorted by (WMOID, NameSet,
WMOGroupID) with an exact-triple comparator (`0x990530`) — no fallback to the
root (-1) row. The 0x8000 / 0x200000 "mount allowed" MOGP bits documented for
other client versions are **not** consulted here.

## Liquid / swimming

`CGUnit_C` swim update `0x730D10` reads the query through `0x77F1E0`
(+0x7C & 0x20 → liquid present, +0x80 → surface Z, +0xBC → type) and toggles
MOVEMENTFLAG_SWIMMING (0x200000) at `0x730EC0..0x730EF2`.

`0x7A1BC0(query)` fills the liquid fields:

```
if (query->flags & 1)            // inside a WMO group
    0x7A1A30(query)              // that group's MLIQ only: 0x7A13E0 → group,
                                 //   0x7C8360(group, localPos, &type, &z)
else
    0x7A0B00(&pos, &type, &z, &out, 1)
      0x7A09D0: for def in global mapobjdef list, skip def->flags & 0x100,
                point in def world bbox (+0x48..+0x5C), local = pos * def->invMatrix(+0xB0),
                0x7AE810(mapObj, local) bbox,
                0x7AEB90(mapObj, mask=0x2000, local, &type, &z):
                    for each MOGI entry i: if (MOGI[i].flags & mask) skip   // INTERIOR groups skipped
                        if point in MOGI[i].bbox and group i loaded ([+0x1F8+i*4], +0x198 & 1)
                            if 0x7C8360(group, local, &type, &z) return true
      else 0x7A0820: terrain (MH2O/MCLQ) liquid
```

So WMO water is reachable on both branches, but with different keys: inside →
the hit group's MLIQ; not inside → any group whose **MOGI** flags lack INTERIOR.
Water rendering (`0x793D20`, CWorldView group-liquid gather) does not depend on
either, which is why mis-flagged water is visible but not swimmable.

## Consequences / recipes

* Stock interior group (MOGP 0x2000, MOGI 0x2000): inside → its liquid; mount
  needs a WMOAreaTable row with Flags&4 for the exact (wmoID, nameSet, uniqueID).
* MOGP |= EXTERIOR only: not inside → outdoors via terrain AreaTable, but the
  WMO is culled (MOGI still interior) — observed on Violet Hold 2026-08-06.
* MOGP |= EXTERIOR and MOGI = 0x2008: renders, mounts, **water unswimmable**
  (MOGI INTERIOR excludes it from the outdoor liquid path) — the shipped bug.
* MOGP 0x380D, **MOGI 0x0008**, MOHD 0xD, batches ext: renders, mounts, swims.
  This is what `vhr_outdoors.py fix-client-mogi` writes.

## Other addresses met on the way

| addr | what |
|---|---|
| `0x7AEA80` | CMapObj::GetGroup(idx, force) → `[+0x1F8+idx*4]` if `[+0x198]&1` |
| `0x7AE880` | segment vs MOGI bbox of group idx |
| `0x7B39B0` | (def, groupIdx, faceIdx) → ground material type (MOPY→MOMT+0x20) |
| `0x782560` | area id at query: terrain chunk `+0xB0`, or WMOAreaTable row `+0x28` (AreaTableID) |
| `0x7A1500` | zone name: WMOAreaTable root row (-1) name/area |
| `0x79C360` / `0x79D2B0` | terrain "liquid chunks within radius" gather (sound), `LiquidChunkAndDistance` |
| `0x793D20` | WMO liquid render gather; "WMO: Liquid type [%d] not found" at `0x793E1D` |
| `0x0A3F884` | that string; `.\MapObjGroup.cpp` `0xA403A4`, `.\MapObjRead.cpp` `0xA405A0`, `.\World.cpp` `0xA3E559` |
