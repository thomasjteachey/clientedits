# Remote-unit movement packet handling (Wow.exe 3.3.5a 12340)

How an observing client applies movement packets for *other* units. Written
2026-09-16 while fixing server-driven playerbots that "teleport around" on
observers while their server position is correct. Everything below was read
from the binary with `reversing\peexplore.py`; addresses are VAs (imagebase
0x400000). "Verified" = traced instruction by instruction.

## Handler registration (verified)

`RegisterHandler(opcode, handler, param)` = `0x6B0B80`, calls around `0x742250..0x742913`.

| Opcode | Handler | Per-unit worker |
|---|---|---|
| MSG_MOVE_STOP/TELEPORT/SET_FACING/ROOT/UNROOT/HEARTBEAT (0xB7, 0xC5, 0xDA, 0xEC, 0xED, 0xEE) | `0x741B60` | `CGUnit_C 0x740D30` (switch table `0x741118`/`0x7410AC`) |
| MSG_MOVE_TELEPORT_ACK, SMSG_FORCE_MOVE_ROOT/UNROOT (0xC7, 0xE8, 0xEA) | `0x732450` | `CGUnit_C 0x7307A0` (table `0x730C54`/`0x730C1C`) |
| SMSG_MONSTER_MOVE / _TRANSPORT (0xDD, 0x2AE) | `0x73F590` | `CGUnit_C 0x73C8E0` |
| SMSG_SPLINE_MOVE_ROOT/UNROOT (0x31A, 0x304) | `0x741C30` | `CGUnit_C 0x740BA0` |
| SMSG_MULTIPLE_MOVES (0x51E) | `0x716AF0` | |

`CMovement` is embedded at `unit+0x788`; movement flags at `CMovement+0x44`
(= `unit+0x7CC`), flags2 at `+0x48`, position at `+0x10`, active spline object
pointer at `+0xBC` (flags dword at spline `+0x20`, end point at spline `+0x1F8`).
Unit update fields: `[unit+0xD0]` points at the UNIT fields (`+0x18` CHARMEDBY,
`+0xD4` UNIT_FIELD_FLAGS).

## The per-unit event queue (verified)

`CMovement+0x138` is a list of movement events **sorted by event time**
(`0x6EC090` sorted insert). Event: `+0x08` time, `+0x0C` type, `+0x10..` position,
`+0x30` movement flags, `+0x4C` ack counter, `+0x50` has-MovementInfo,
`+0x51` needs-ack. Types seen: `0x29` root, `0x2A` unroot, `0x2B` heartbeat,
`0x2C` teleport.

- **MSG_MOVE_\*** (`0x6ED990` -> `0x6EB730`): the packet timestamp is mapped onto
  local time through a per-unit base (`CMovement+0xC0` local, `+0xC4` packet),
  drift clamped to [-500, +1000] ms. If the mapped time is still in the future
  the packet is **not applied**: `0x6EC8B0` copies the MovementInfo into an event
  (`0x6E9050`, sets `+0x50`) and inserts it at that future time.
- **SMSG_FORCE_MOVE_ROOT/UNROOT** (`0x6EEFF0`): always queued, stamped with the
  **receive time**, `+0x51` = 1.
- Pump `0x6ED7E0` drains the whole list through `0x6ED0F0` (type switch at
  `0x6ED730`). Root (`0x6ED584`) calls `0x98C4F0` (set 0x800); unroot (`0x6ED5C7`)
  calls `0x98C550` (clear 0x800 and 0x100000). Both run for remote units. Any
  event with `+0x50` then goes through `0x6EA9B0`, which copies movement flags
  with mask `0x77FFFDFF` (root included) and the position.
- `0x73C8E0` runs the pump **before** every SMSG_MONSTER_MOVE, so queued
  MSG_MOVE_\* never land after a newer spline.

Consequence: a MSG_MOVE_\* whose timestamp maps slightly into the future sorts
**after** a force unroot received just behind it, and re-applies whatever
movement flags it carried.

## Spline launch refuses rooted units (verified)

`CMovement::LaunchSpline 0x6EB680`: returns 0 if spline flags have `0x1800000`,
or if movement flags have `0x100800` (ROOT | PENDING_ROOT) and the spline is
neither falling nor parabolic (`0xA00`). `0x73C8E0` then takes its fallback at
`0x73D199`: apply facing, then `0x6F11B0` -> `0x6E9470`, which **writes the
spline's final point straight into the position**. A unit the client believes is
rooted therefore jumps to the end of every spline it is sent.

## SMSG_MONSTER_MOVE shapes (verified)

- **Stop (type 1)**, `0x73C9D6`: distance from the drawn position to the stop point
  is compared with a CVar at `[[0xCA1194]+0x2C]`. Within it: `0x6F11B0` places the
  unit directly. Beyond it: a 2-point spline **from the drawn position to the stop
  point** is synthesised and launched, so the model runs there. Never publish a
  teleport as a stop.
- **Normal, linear**: the path starts at the packet's start position, not the drawn
  position, so a spline that starts elsewhere snaps the model to its start first.
  Catmullrom/flying (`0x42000`) paths start from the drawn position instead
  (`0x73CDA6`).
- **Zero-length Normal** (one point equal to the start, e.g. TC's creature
  `SetFacingTo`): fewer than 4 path points after control-point padding
  (`cmp edx,3; jle` at `0x73CF3F`) -> same `0x73D199` fallback -> facing applied and
  the unit placed at the point.

## MSG_MOVE_TELEPORT (verified)

`CGUnit_C 0x7189A0` -> `CMovement 0x6F1120`: event type `0x2C` through `0x6ED990`
(so it can be deferred like any MSG_MOVE_\*), then `0x6ECF80` writes the position,
then dead reckoning (`0x6E9B70`) only if flags & `0xC0100F` (directional
movement, falling, ascend/descend). No other state implies more client movement.

## Unit flag 0x4 (UNIT_FLAG_REMOVE_CLIENT_CONTROL)

No test of bit 0x4 on `[fields+0xD4]` exists in the binary (21 distinct masks
tested, none include it), so the flag does not affect how remote units are drawn.

## Server rules that follow

For socketless server-driven players (playerbots, clones), in `tc-lplus`
`Unit.cpp` `SOCKETLESS_OBSERVER_STRIPPED_MOVEMENT_FLAGS`:
- never publish ROOT/PENDING_ROOT or motion flags inside a MSG_MOVE_\* packet;
  root state goes through the root/unroot opcodes only;
- near teleports go out as MSG_MOVE_TELEPORT, never as a MonsterMove stop.
