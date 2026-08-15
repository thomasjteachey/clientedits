# In-client GameObject editor — client reverse-engineering findings

Target: **Wow.exe 3.3.5a build 12340** (`clients\centurion\Wow.exe`, imagebase
0x400000, x86). Goal: a **hover-following, transparent gameobject ghost** — the
model tracks the mouse cursor over the world, mousewheel rotates, left-click
places. Server design lives in the tc-lplus repo
(`src/server/scripts/Custom/GOMove/EDITOR_DESIGN.md`); this file is the client
half.

All addresses located statically with `reversing/peexplore.py` (assert-string
anchors → xrefs → callers) against this exact binary. Every hook must still be
byte-signature guarded at load so a different client build fails safe.

## Architecture: render-hijack hybrid

The server spawns **one invisible ghost GameObject** (GM-only phase) and never
moves it. The client DLL, every frame, overrides that GUID's **render transform**
(to the cursor's world point + wheel rotation) and **alpha** (~0.4). The model
loads and draws for free because the object is real; we only hijack where it draws
and how solid it looks. This is still 100% client-driven visually — the server
object is a placeholder the player never sees.

## Reuse from the player-collision project (already verified — do not re-derive)

| Need | Address / fact | Source |
|---|---|---|
| Frame tick (local player, every frame) | `0x6DEB30` `__thiscall(this,arg)` ret4, prologue `55 8B EC 83 EC 08` | PLAYER_COLLISION_DESIGN |
| Object by GUID | `ClntObjMgrObjectPtr 0x4D4DB0(guidLo,guidHi,typeMask)` cdecl | ” |
| Active player GUID | `ClntObjMgrGetActivePlayerGuid 0x4D3790` | ” |
| Enumerate visible objects | `0x4D4B30(cb,arg)`; GUID at node+0x30/0x34 | ” |
| Low-level world ray | `CWorld::Intersect` thunk `0x77F310` → real `0x7A3B70` | ” + here |
| DLL framework | `PlaceDetour`/`InstallAt`/`IsReadable`, ini, `build.bat`, one-proxy rule | AnimSpeedFix |

Client editor code folds into the same `dinput8.dll` (only one proxy can exist).
**Dev tip:** during bring-up, inject a separate probe/editor DLL via `loader.cpp`
(CreateRemoteThread) so the shipping collision proxy is never at risk.

## Target A — cursor → world point  (needs one LIVE confirmation)

The pick uses `CWorld::Intersect` (real fn `0x7A3B70`; `0x77F310` is a 5-byte
thunk that jmps to it). Confirmed **cdecl** signature from the call sites:

```
char Intersect(Vec3* start /*+8*/, Vec3* end /*+0xC*/, void* hitOut /*+0x10*/,
               float* pFrac /*+0x14, in=1.0 out=hit t*/, int flags /*+0x18*/, int /*+0x1C*/)
```
Return `al` = hit. **World hit point = start + (*pFrac)·(end − start).** So any
caller that raycasts the cursor gives us the world point for free — no camera math.

All 21 callers of `0x77F310`:
```
4F99F8 4F9B71 568B5B 568C8E 603B3B 603BB2 605F00 6060FE 606256
6FD37C 6FD4F7 6FD5DD 700127 700211 714BBD 759709 75F04B 7FBA35 7FC3D8 7FC46C 9ABD18
```
The collision project **runtime-probed** which fire when the cursor is over the
world (walking into terrain/buildings) and got five: `4F99FD 605F05 606103 60625B
75F050` (return addresses). **But some of those are the CAMERA colliding with the
world, not the mouse pick** — e.g. the function around `606103` has an `esi` object
with near/far/fov fields (a camera). Static reading can't tell camera-collision
from cursor-pick apart; that's a live question.

**Resolution = the `[GOEditorProbe]` module** (`src/goeditor.cpp`, built into
`dinput8.dll`). It detours `0x7A3B70`, and for
each call logs `caller, start, end, flags` (raw hex, integer-only — no FP in the
hook). Run it, move the mouse slowly over flat ground: **the cursor-pick caller is
the one whose `end` vector sweeps with the cursor** while camera callers stay put.
That caller (plus the hit-point formula above) is our `GetCursorWorldPos`.

`0x7A3B70` prologue for the guard: `55 8B EC 83 EC 18` (steal 6, resume `0x7A3B76`).

## Target B — CGGameObject_C layout  (position write + alpha)

GO class code region **~0x70B000–0x713000** (`.\GameObject_C.cpp` assert `0xA33604`
pushed from `0x70BE4A … 0x712167`). RTTI name `CGGameObject_C` at `0x9F3964`.
Still to extract (next static pass, then live-confirm):
- **Position offset** — the GO analog of a unit's `+0x798`. Find a GO position
  accessor in the region (float triple read/write), or the vtable `GetPosition`.
- **Cached transform** — 3.3.5 caches a GO's world matrix (why server moves need
  destroy/recreate). The hijack must write the position **and** invalidate/rebuild
  that matrix, or override the matrix at render time. This is the render-hijack
  crux and wants live confirmation.
- **Alpha** — GOs fade in on spawn, so a per-instance alpha ramp exists; pin it to
  ~0.4. **Deferred to milestone 4** — an opaque hover-follow ghost ships first.
- **Type mask** for `ObjectPtr` (GO), analogous to UNIT 0x08 / PLAYER 0x10.

## Milestones

1. **Cursor probe (`[GOEditorProbe]` in `src/goeditor.cpp`)** — identify the cursor-pick
   caller live. *Only step that needs the client running; de-risks the #1 unknown.*
2. **Server ghost** — spawn one invisible ghost GO in a GM phase, send its GUID to
   the client (addon channel). (Server code already ~90% there in `GOEditor.cpp`.)
3. **Opaque hover-follow** — client writes the ghost's position to the cursor point
   each frame (Target B position + transform invalidate). Usable editor.
4. **Alpha** — render the ghost translucent.
5. **Input** — mousewheel rotate, click-commit, esc-cancel, ground snap.

## How to run the probe

The probe is a **built-in feature of `dinput8.dll`**, gated by `[GOEditorProbe]`
in `AnimSpeedFix.ini` (module `src/goeditor.cpp`, OFF by default, read-only).

1. Build: `build.bat` (compiles it into both `AnimSpeedFix.dll` and `dinput8.dll`).
2. Deploy the new `dinput8.dll` next to `Wow.exe` (`deploy.ps1`), and in the
   client's `AnimSpeedFix.ini` set `[GOEditorProbe] Enabled = 1`.
3. Launch, hover the mouse slowly over flat ground while panning the camera
   separately, then **exit the client normally** (flushes the log tail).
4. Send back `goeditor_probe.log` (next to the DLL). The caller whose `end` vector
   tracks the cursor is `GetCursorWorldPos` — milestone 1 done. Set `Enabled = 0`
   again afterwards.
