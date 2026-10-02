# GameObject use / predicted open state (Wow.exe 3.3.5a 12340)

RE'd 2026-09-24 while chasing "after a jump I could never click that BfG banner again".
All addresses were verified by static disassembly (`reversing/peexplore.py`).

## Per-object cached state: `CGGameObject_C + 0x204`

- The client keeps its own copy of the GO state (`GO_STATE_*`: 0 active, 1 ready,
  2 active-alternative) at `+0x204`. It is initialised from the descriptor
  (`[go+0xD0]+0x2C`, which is `GAMEOBJECT_BYTES_1` byte 0) in the object init
  `0x714389`.
- It is re-synced from the descriptor by `0x70F810` when the state field
  changes, and by `0x711050`.
- `0x70F7D0(newState)` **overwrites it locally**, then fires the type handler's
  state-change virtual (`vtbl+0x30`).

## Who writes the local override

- `0x80E1B0`, reached only from the shared SPELL_START/SPELL_GO handler `0x80FEE0`,
  and only on opcode `0x132` **SMSG_SPELL_GO** (`0x80FFC1`). It applies only when
  the caster is the active player (`0x80F5E7`), the spell has effect 33 OPEN_LOCK,
  and the GO type is DOOR (0), BUTTON (1) or CHEST (3). GOOBER (10) is **not**
  predicted. The matching Lock.dbc `Action` picks the new state: action 0/1 sets 0
  (active, "opened"), action 2 sets 1 (`0x80F761`-`0x80F776`).
- `0x58971C` (flag-driven, sets 1). Not investigated.

## Who clears it

- **SMSG_GAMEOBJECT_RESET_STATE (0x2A7)**, handler `0x7FD900` (registered at
  `0x81022D`). Payload: a plain uint64 GUID. It looks up the GO and calls
  `0x70F810` (re-sync from the descriptor).
- Any real change to the descriptor's state byte, or the object being destroyed
  and recreated.

## Who reads it

- Goober/button type handler vtable `0xA33C38` (ctors `0x712770` goober and
  `0x712560` button, picked by the GO-type jump table at `0x714998`):
  - `vtbl+0x1C` CanUse = `0x711470`
  - `vtbl+0x24` Use = `0x7116C0`
- `0x7116C0` calls `0x70EF90(lock.Action[0])`. For action 0/1/3 it requires state
  == 1 (ready). When that fails on a non-chest it **returns false with no error
  message** (`0x711893`). The player just can't click the object.
- Right-click entry `0x70F680` (callers `0x71B2DE` and `0x711192`) sends
  CMSG_GAMEOBJ_REPORT_USE (0x481) after a successful Use. On a CanUse failure it
  either click-to-moves (reason 0xF0, `0x72B730` -> `0x727400(7, ...)`) or shows
  a UI error (`0x5216F0`).

## Consequence for the server

If the local player's Open Lock completes (SPELL_GO is sent) on a DOOR, BUTTON or
CHEST and the server leaves that object's state as it was, that client considers
the object opened and silently refuses it until the object is recreated. Retail
sends SMSG_GAMEOBJECT_RESET_STATE for this. TrinityCore never did.
`Spell::EffectOpenLock` now sends it after a battleground banner click that
leaves the banner spawned. AB and BfG owned and contested banners (180058-180061)
are BUTTONs.
