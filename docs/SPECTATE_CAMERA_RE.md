# Spectate camera: following a unit without turning with it

Wow.exe 3.3.5a 12340. RE'd 2026-10-29 with `reversing/peexplore.py`, statically
(no live session). Used by `src/spectate.cpp` (`CenturionSpectate`).

## Why Bind Sight spins

Bind Sight, Mind Vision, Eyes of the Beast and farsight all set `PLAYER_FARSIGHT`,
and the client hands that object to the camera as its target
(`CGCamera::SetTarget`, `0x006066E0`, thiscall(camera, CGObject*, int); target
guid at `camera+0x88`). The normal camera keeps its yaw relative to its target:
`FlipCameraYaw` (`0x005FF2C0`) adds to `camera+0x12C`, and `0x005FF530`
normalises `+0x11C` / `+0x12C` and clamps pitch `+0x120`. So the view swings
with every turn of the watched unit. That is AzerothCore's arena spectator
(spell 6277 Bind Sight).

## The commentator camera

Blizzard's tournament spectator code (`CommentatorFrame.cpp`, Lua
`Commentator*`) has a camera that does not do that.

| address | what |
|---|---|
| `0x006DE980` | `CGPlayer_C::IsCommentator`, thiscall(player) -> bool. PLAYER_FLAGS (`[player+0x08]` + `0x08`) bit `0x00080000`, then bit `0x00400000` **or** the current map's Map.dbc InstanceType (`[0x00BD088C]` row + `0x08`) == 4. Starts `A1 8C 08 BD 00` (`mov eax,[0xBD088C]`). 18 callers. |
| `0x004FA69B` | the world frame's per-frame camera block. `IsCommentator(active player)`: drop the camera's target if it has one (`SetTarget(NULL, 0)`), then `CGCommentator::Update(frameTime)` on `0x00ACE4A8`. Otherwise: keep the camera's target (`+0x88`, then `+0x90`), or when that object is gone, `SetTarget(active player)`. So clearing the answer gives the camera straight back to the player. |
| `0x00ACE4A8` | the `CGCommentator` singleton (fields below) |
| `0x005689A0` | `CGCommentator::Update(this, float dt)`, ret 4 |
| `0x00569B50` | `CommentatorFollowPlayer(faction, index)`: looks the player up in the commentator roster (`0x005688C0`, filled by server commentator packets) and writes its guid to `+0x30` (`0x00569C9B`). |
| `0x005684B0` | set position/yaw/pitch (`CommentatorSetCamera` uses it) |
| `0x0056B5C0` | movement-flag input in commentator mode -> `+0x18` = +1 / -1 (forward / back) |
| `0x00568560` | mouse delta scaling by FOV (mouse look itself writes `+0x00` / `+0x04` at `0x005FB2BB` / `0x005FBEC7`) |
| `0x00568770` | `CommentatorZoomIn`: FOV zoom (`+0x40`), not distance |

`CGCommentator` fields:

| offset | field |
|---|---|
| `+0x00` | yaw (radians) |
| `+0x04` | pitch (radians, > 0 looks down) |
| `+0x08` | free-fly turn rate |
| `+0x0C..+0x14` | free-fly position |
| `+0x18..+0x20` | move input (`+0x18` forward/back) |
| `+0x28/+0x2C` | look-at guid |
| `+0x30/+0x34` | follow guid |
| `+0x38` | follow distance, clamped each frame to 1 .. 75 (`[0x00A0FF58]`) |
| `+0x3C` / `+0x40` | current / target FOV, eased; written to `camera+0x40` |
| `+0x44` | move speed (follow: distance change per second on W/S) |
| `+0x48` | camera collision on (`CWorld::Intersect` flags `0x00100171` on the eye) |
| `+0x4C` | height offset |

`Update` with the follow guid's object found (branch `0x00568BBA`): target =
the unit's position (vtable `+0x2C`) plus `0.75 * [unit+0x854]` plus `+0x4C`;
eye = target - forward(yaw, pitch) * distance; collision pulls the eye in; the
camera is placed by `0x006038A0`(pos, yaw, pitch) and oriented by
`0x00607BD0`(yaw, pitch, 0). The unit's facing is never read. With no follow
object it free-flies at `+0x0C` and, with a look-at object, aims at it.

## Euler convention

`0x004C56D0` builds the camera matrix from (yaw, pitch, roll); `0x004C5630`
(thiscall(matrix, &yaw, &pitch, &roll)) reads it back: row 0 is forward,
`yaw = atan2(m[1], m[0])`, `pitch = asin(-m[2])`. `CGCamera`: `+0x08` position,
`+0x14` matrix, `+0x38` near clip, `+0x40` FOV; the active camera is
`[[0x00B7436C] + 0x7E20]` (`0x004F5960`).

## What spectate.cpp does

- Hooks `0x006DE980`. While a follow is on it answers "yes" for the local
  player only to the camera's own questions, told apart by return address:
  `0x004FA6A2` (world frame: who places the camera), `0x005FB2B3` /
  `0x005FBEBF` / `0x005FBACC` (mouse look: yaw, pitch, delta). Every other
  caller gets the client's own answer, so selecting, clicking and keys work.
- `0x005FBBC0` thiscall(input control, time, start) is where a movement key
  lands (`MoveForwardStart` 0x005FC200 -> `0x005FA170` sets bit 0x10 -> here;
  strafe 0x40/0x80, turn 0x100/0x200, back 0x20, autorun 0x1000, both mouse
  buttons 0x3; the bits are at `[[0x00C24954]+4]`). Its IsCommentator call
  returns to `0x005FBC07`: with `start` ([ebp+0Ch]) set and a movement bit on,
  the hook ends the follow and gives the client's answer, so the key moves the
  player as usual.
- `0x005277B0` interact(guidLo, guidHi), cdecl: a right-click on a unit in the
  world (`0x004F78EA`; a left-click goes to `0x005274F0`) and `InteractUnit`.
  Starts `55 8B EC E8 D8 BF FA FF`. While armed, a right-click on another
  player is kept for `CenturionSpectate("click")` and not acted on.
- On a fresh follow, seeds the commentator from the current camera so nothing
  jumps: position = camera position, yaw/pitch = the direction from the camera
  to the unit's aim point, distance = that distance, FOV = the camera's. Then
  writes the follow guid, collision on, height offset 0.
- "off" (or a movement key) clears the answer; the world frame re-targets the
  player.

Not verified in game at the time of writing.
