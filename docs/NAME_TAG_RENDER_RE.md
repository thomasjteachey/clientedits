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
