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

## What spectate.cpp does (2026-10-29, second version)

The commentator camera (above) follows without spinning but takes the camera
controls over: the owner could not zoom or drag. spectate.cpp now uses the
NORMAL camera:

- The normal camera's world yaw is `relative yaw + camera+0x12C + target facing`
  (0x00604580..: the facing is cached at camera+0x1D4) - **unless camera+0xAC
  > 0**. That is a lock count: `0x006019B0` (thiscall(camera)) increments it and
  on 0 -> 1 adds the target's facing into the stored yaw so nothing jumps
  (0x005FE5F0); `0x00601A70` decrements and on 1 -> 0 takes it back out. Mouse
  look uses it. Pitch has the same at +0xB0 (not used).
- Start: `CGWorldFrame::SetCameraTarget` (0x004F6F50, thiscall(worldFrame,
  object, 0), the call farsight makes at 0x006E2A01) on the unit, then one yaw
  lock. Zoom, left/right drag, collision are the camera's own.
- Every frame (IsCommentator hooked only as a call site: return address
  0x004FA6A2 in the world frame's camera block; the answer is always the
  client's): the camera is put back on the unit if it lost it (a replay restart
  re-creates the unit) and the lock re-taken if a view change (0x00606970
  zeroes +0xAC) dropped it.
- Stop: the lock released while the camera is still on the unit, then
  SetCameraTarget(active player).
- `0x005FBBC0` thiscall(input control, time, start) is where a movement key
  lands (MoveForwardStart 0x005FC200 -> 0x005FA170 sets bit 0x10 -> here;
  strafe 0x40/0x80, turn 0x100/0x200, back 0x20, autorun 0x1000, both mouse
  buttons 0x3; the bits are at `[[0x00C24954]+4]`). Its IsCommentator call
  returns to 0x005FBC07: with `start` ([ebp+0Ch]) set and a movement bit on, the
  follow ends and the camera goes back to the player on the next frame.
- `0x005277B0` interact(guidLo, guidHi), cdecl: a right-click on a unit in the
  world (0x004F78EA; a left-click goes to 0x005274F0) and InteractUnit. Starts
  `55 8B EC E8 D8 BF FA FF`. While armed, a right-click on another player is
  kept for CenturionSpectate("click") and not acted on.

Not verified in game at the time of writing.
