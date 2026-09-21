# The 3D name over a unit — client reverse-engineering notes

Wow.exe 3.3.5a 12340. Static pass only (peexplore.py); everything here is an
address the file itself gives up, nothing is remembered from elsewhere. The
open questions at the end need a **runtime probe**, the way the collision
project settled its own.

Goal that prompted this: draw a coloured word — **World** (blue), **Tournament**
(orange), **Bot** (purple) — above a player's own name in the world.

## 1. The anchor: the name CVars are one bitmask

`UnitNameOwn`, `UnitNameNPC`, `UnitNamePlayerGuild`, `UnitNamePlayerPVPTitle`
and the rest are all registered at **0x007E6150** through the same callback,
**0x007E60E0**, each with its own bit:

| CVar | bit |
|---|---|
| `UnitNameOwn` | 1 |
| `UnitNameNPC` | 2 |
| `UnitNamePlayerGuild` | 4 |
| `UnitNamePlayerPVPTitle` | 8 |

The callback ORs the bit in when the CVar is truthy and ANDs it out when not,
into one global:

    0x00D380A0   name-display flags (the whole set of UnitName* CVars)

Only three instructions read or write it, which is what makes it a good anchor:
the callback above, and **0x007E567D** and **0x007E5734** — both inside the
function below.

## 2. The per-unit name tag: 0x007E5640

`__thiscall`, `this` = a name-tag record, frame `sub esp, 0x4EC`.

    this+0x08   something released through 0x6BDFC0 when non-zero (a string?)
    this+0x0C   filled by unit vtable +0x78
    this+0x10   guid low       } the unit this tag belongs to
    this+0x14   guid high      }
    this+0x18   flags; bit 1 is consumed-and-cleared at 0x007E56C0

What it does, in order:

1. Resolves the unit: `0x004D4DB0(guidLo, guidHi, 1, "…", 0xE5)` — the objmgr
   getter with its file/line arguments. No unit, nothing drawn.
2. **`call [unitVtable + 0xD0]` with the 0x00D380A0 mask in `ecx`** and keeps
   the result in `edi`. This is the decision "what does this unit show" and it
   is the single most useful call on the page.
3. Splits that result into **two** fields: `edi & 3` and `edi >> 2`, each turned
   into a float and multiplied by constants at 0x009E8CE4. Two fields, two
   stacked lines — the name and the guild line under it.
4. Distance gate **0x007E52A0**: camera position is `[0x00B7436C] + 0x7E20`;
   it subtracts, takes the length and compares against 0x009E2EC8. Returns a
   bool in `al`, which becomes the **alpha byte** written into the geometry.
5. Sets render state through 0x00408BF0 / 0x00408C30 (state, value), then
   allocates from the batch manager `[0x00C5DF88]` with
   `0x00684850(0, 0x18, 4)` and fills **four 0x18-byte vertices** at +0x00,
   +0x18, +0x30, +0x48. Per-vertex colour is the four bytes at +0x24..+0x27 of
   each vertex, written as `bl` (alpha, from the distance gate) and `cl` (zero).
   Hands the block to `[[0x00C5DF88]]+0xD8`.
6. Position and offsets come from globals `0x00AF46AC`/`B0`/`B4` with per-corner
   offsets `0x00AF46DC`…`0x00AF46F0`.

It has **no direct callers** — nothing does `call 0x7E5640`. It is reached
through the dispatch table at **0x00D380B0** (`jmp dword ptr [ecx + 0xD380B0]`
at 0x007E53F4), so a hook goes on the function itself, not on a call site.

## 2.5 RUNTIME PROBE, 2026-09-20 - the pipeline, end to end

The stage-1 probe (`src/nametag.cpp`) logged the unit's vtable for the player
it saw: **0x00A326C8**. Reading that vtable statically gives the same +0xD0
the probe logged at runtime, so the slots below are read straight off the file.

| slot | function | what it does |
|---|---|---|
| +0x78 | 0x00718AC0 | **name colour**. Writes one ARGB dword to its argument: a fixed colour from 0x00ADAA98 when `unit+0xA30 & 0x10`, otherwise the reaction colour from the unit's guid via 0x00521BF0. The tag keeps it at `tag+0x0C`. |
| +0xCC | 0x006E6FA0 -> 0x0072D4F0 | **name-tag text**. `(mask, char* out, size 0x400)`; writes the whole tag as ONE newline-separated block and **returns the number of lines**. The guild line is appended with the format `"\n<%s>"` (0x00A34CB4); the name itself is assembled with `"%s%s%s%s%s%s%s%s%s"` (0x00A34C88), and cross-realm names get FOREIGN_SERVER_LABEL. |
| +0xD0 | 0x00729C70 | **visibility policy**. Fetches the active player and the unit's owner/charmer and returns which name-display bits apply (own, pet, enemy player, guild line...). Decides *whether*, not *what*. |

And inside 0x007E5640, the part that only runs when the tag is **dirty**
(`tag+0x18 & 2`, cleared on the way in):

1. `[vtable+0x78](&tag+0x0C)` - the colour.
2. A 0x400-byte stack buffer at `[ebp-0x4EC]` is zeroed, then
   `[vtable+0xCC](mask, buffer, 0x400)` fills it; the returned line count
   times a constant becomes the tag's height at `tag+0x30`.
3. **0x006BE2B0** - the font-string constructor - is called with the name-tag
   font at **`[0x00D380AC]`**, the text (after 0x00482110), `&tag+0x08` for the
   result, flags 2 and 1, max width 0xC8, **a pointer to the colour** and two
   scale floats. `tag+0x08` is the resulting font string, linked into the
   font's list (0x006BDFC0 is the generic unlink that drops it).

Every frame after that, the tag only draws that finished string as one quad
whose vertex colour carries nothing but the distance fade - which is why the
quad read in section 2 had alpha and no colour. The colour is baked into the
string when it is built.

### What this means

- **The name is rebuilt only when dirty**, so anything done in the dirty block
  costs nothing per frame.
- **Colouring a name** is one override of the dword `[vtable+0x78]` writes.
- **A marker line** does not need its own projection or font: prepend
  `"<marker>\n"` to the buffer after `[vtable+0xCC]` returns (the call site is
  0x007E5754, `call edx`; the instructions after it, `test eax, eax` /
  `mov [ebp-0x10], eax` at 0x007E5756, are five relocation-free bytes to hook)
  and add one to the line count it returned. The height, the fade, the distance
  gate and the position all follow for free.
- The string is drawn in ONE colour. A marker in a colour of its own needs
  WoW's inline `|cffRRGGBB...|r` codes to be honoured by 0x006BE2B0 (and
  0x00482110 before it). That is the one thing left to prove, and a single test
  build that prepends a coloured line proves it either way.

## 3. What is still open

The static pass did not reach two things, and neither is guessable:

- **Where the glyphs are drawn.** The quad above carries a colour whose only
  varying component is alpha, which reads more like a background or shadow than
  the lettering. The text itself is emitted somewhere past this, or by whatever
  consumes the batch.
- **World → screen for the text baseline.** 0x007E52A0 is a distance test, not
  a projection, so the projection is elsewhere - possibly already done by the
  caller, since the position globals at 0x00AF46AC are written before this runs.

Both want the same experiment: hook 0x007E5640, log `this`, the guid, the value
from vtable+0xD0 and the batch pointer for one frame with one player on screen,
then walk what the batch receives. The tracer in `src/trace.h` is the tool.

## 4. What this means for the two designs

**Colouring the name** is well anchored already: the value from
`[unitVtable + 0xD0]` decides what is drawn per unit, and per-vertex colour is
written in plain sight in this function. A hook there can pick a colour per
unit with what is known today.

**Adding a third line above the name** needs the two open items first. The
encouraging part is step 3: the engine already stacks *two* lines from one
packed value, so a third is following a path the code already walks rather
than inventing one.

## 5. Where the marker would come from

The client has to know which of the three a unit is. The realm already gates
client features on hidden auras (see `PLAYER_COLLISION_DESIGN.md`), and both
"tournament character" and "bot" are things the server knows. One aura per mode
keeps this off the wire entirely and gives the DLL something it can read off the
unit — no new packet, no name-string games.
