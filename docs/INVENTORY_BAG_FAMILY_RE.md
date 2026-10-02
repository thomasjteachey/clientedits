# Inventory: bag families and item-move errors (Wow.exe 3.3.5a 12340)

2026-09-16. Question answered: does the client refuse to put an item into a quiver, ammo
pouch or other specialty bag on its own, or does it leave that to the server? **It leaves
it to the server.** Making quivers hold any item (`Centurion.Items.QuiversHoldAnyItem`,
`BagHoldsAnyItem` in the server's Item.cpp) needed no client change.

All addresses verified by disassembly with `clientedits\reversing\peexplore.py`.

## Item cache and BagFamily

| Address | What |
|---|---|
| `0xC5D828` | item cache (`WDB` itemcache) object; `mov ecx, 0xC5D828; call 0x67CA30` looks a record up (returns 0 when not cached) |
| record `+0x1B8` | `BagFamily` (the server's `item_template.BagFamily`, from SMSG_ITEM_QUERY_SINGLE_RESPONSE) |
| `0x7072B0` | `CGItem_C::GetBagFamily()` - thiscall, looks the item's entry up in the cache and returns `+0x1B8` (0 when uncached) |

Every reader of `+0x1B8` next to an item-cache lookup (scanned all 274 lookups):

| Site | Use |
|---|---|
| `0x510F20` | Lua `GetItemFamily(item)` |
| `0x5D77DD` | Lua `GetContainerNumFreeSlots(bag)` - second return value `bagType` |
| `0x753B00` | item-search predicate `(GetBagFamily(item) & *mask) != 0`, passed to the inventory enumerator `0x7546F0` (from `0x754ADC`); `0x753AC0` is its class/subclass twin |
| `0x6D6720` | error formatter, see below |

None of them sits on the item-move path, so no local "wrong bag" refusal exists.

## Item-move packets

| Address | What |
|---|---|
| `0x6DFB74` | builds CMSG_SWAP_INV_ITEM (0x10D) |
| `0x6DFBBC` | builds CMSG_SWAP_ITEM (0x10C): `PutU32 0x47B0A0`, then `0x47AFE0` puts the four bytes, `0x6B0B50` sends |
| `0x6DBB59` | builds CMSG_AUTOSTORE_BAG_ITEM (0x10B) |
| `0x5D7FF0` | Lua `PickupContainerItem` (table `0xAD0D80`), `0x5EA780` Lua `PutItemInBag` (table `0xAD12F0`) |

## Game errors

| Address | What |
|---|---|
| `0xAC8AF0` | GameError table, 20-byte entries `{char* name, int type, char* sound ("NONE"), int, int}`, 0x2DA entries. Index 0 `ERR_INV_FULL`, 2 `ERR_CANT_EQUIP_LEVEL_I`, 9 `ERR_WRONG_SLOT`, **19 `ERR_WRONG_BAG_TYPE`**, **296 (0x128) `ERR_WRONG_BAG_TYPE_SUBCLASS`** |
| `0x513DD0` | `GetGameErrorName(code)` |
| `0x5216F0` | `DisplayGameError(code, ...)` - cdecl, varargs; 682 call sites. Code 0xB plays sound 0x39 |
| `0x6E2E90` | multi-opcode handler registered for SMSG_INVENTORY_CHANGE_FAILURE (0x112) at `0x6E83D9` via `0x6B0B80` |
| `0x6E31F7` | inside it: calls `0x6D6720` with the bag from the packet; when that returns 0 the plain error is shown |
| `0x6D6720` | thiscall(bag index): bag item's `GetBagFamily`, log2 + 1 -> row in the store at `0xAD3D88` (ItemBagFamily), then `DisplayGameError(0x128, name)` = "Only <family> can go in that bag". Formatting of the SERVER's failure only |

No call site pushes code 19 as a constant; 0x128 is pushed only at `0x6D67B4`.
