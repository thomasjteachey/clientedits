# CM2 / M2 model rendering pipeline — client reverse-engineering reference

Target: **Wow.exe 3.3.5a build 12340** (`clients\centurion\Wow.exe`, imagebase
0x400000, x86, `__thiscall` = `ecx` is `this`, callee-saved `ebx esi edi ebp`).
All addresses verified statically with `reversing/peexplore.py`
(`disasm <addr> <count>`, `xref <addr>`) against this exact binary, and many
cross-checked live via the `[SummonPreview]` module in `src/goeditor.cpp`
(logs to `clients\centurion\goeditor_probe.log`). **Every hook is byte-signature
guarded at load** (see `SigOk`) so a different build fails safe.

This documents everything needed to **render a CM2Model client-side at a world
position** (the summon-preview ghost), which turned out to require understanding
the whole M2 draw pipeline. Read alongside `PLAYER_COLLISION_DESIGN.md` (movement/
objmgr) and `ANIMATION_FIXES.md` (M2 animation).

---

## 0. TL;DR — how an M2 gets to the screen each frame

The engine draws every M2 in **two per-object phases**, then a deferred **collect →
flush** into per-view buckets:

1. **PREPARE** `0x832EA0` (`__thiscall(model)`, ret 0): builds the per-instance GPU
   buffer chain and animation-output arrays into the model; sets `m+0x10 |= 0x1000 | 0x400000`.
2. **COLLECT/draw** `0x834660` (`__thiscall(model, bucketA, bucketB)`, ret 8): poses
   the model (calls `0x831990` internally), runs the batch loop, and **enqueues**
   surviving batches via `0x823D50` into the passed buckets. Calls PREPARE itself at
   `0x83468F` **only when `m+0x10` bit0 is CLEAR**.
3. **FLUSH/drain**: a per-view stage drains the bucket → `0x829E40` → `0x829BA0` →
   `CGxDevice` (`*0xC5DF88`) `DrawIndexedPrimitive`.

There are **separate collect walks + flushes per view**. The SHADOW view's flush
**projects** geometry onto the ground (a shadow); the MAIN COLOR view's flush draws
the lit body. Feeding a model into the wrong walk draws it as a shadow only.

---

## 0.5 CORRECTION (2026-08-15) — you do NOT hand-drive the draw; the engine renders a scene object

Two multi-agent RE passes (runs `wf_a7dea289`, `wf_eb0e0bff`) overturned the
hand-injection approach above. **`CM2Scene::DrawPass 0x823CB0` is a SCENE-level call
driven with the global CM2 manager `[0x00CD754C]` as `ecx`** (proven at `0x4F911D`:
`mov edi,[0xCD754C]; mov ecx,edi; call 0x823CB0`), **never an individual model**. It
walks the manager's per-pass buckets `[mgr+0x58+pass*0x10]` and emits each linked
model via `0x823130`. Hand-calling `0x823CB0(ourModel)` reads `[model+4]+4` — and on a
per-unit model **`model+4` is a FLAG BYTE (legitimately 0)**, not a pointer — so it
crashes near 0. (That was our "model+4 sub-object" crash: self-inflicted.)

**How a model actually reaches the screen:** `CreateModel 0x81F8F0` links the new
model at the head of the manager's master list `[manager+8]` **at create time**
(`0x834810 -> 0x834540`) — no objmgr involved (doodads render this way with no objmgr
entry). The manager's own frame walk (`0x81DF10` + the `0x823CB0` DrawPass calls in
`0x4F8EA0`) buckets and draws it **once it is render-complete + placed + passes cull**.

### The engine's drawable object lifecycle (the correct client-side path)

`ClntObjMgr::CreateObject 0x4D6C00` builds a creature as:
`alloc 0x4D4930(typeid)` → `descriptor-init 0x4D45B0` → `base ctor 0x743130`
(sets `obj+0x14`=typeid, `[obj+8]+8`=typemask) → `ReadValuesUpdate 0x4D53C0`
(applies UNIT fields from the update packet — **this is NOT the objmgr register**, a
prior pass mislabeled it) → typeid dispatch `0x4D3FF0` → **unit ctor `0x73F660` +
create-appearance `0x73FCC0`** → **finalize `0x743760`** → **visible-list link
`0x6DED60`** (into `[container+0xA4]`).

- **`finalize 0x743760` is the "make drawable" call.** If `unit+0xB4==0` it calls the
  `GetDisplayId` virtual `[vtable+0x60]` (CGUnit = `0x717B20` → `0x717A20`, which reads
  the display id from **`[unit+0xD0]+0xF4`** — a display sub-object; falls back to a
  default model string `0xA34B60` if absent), does `CreateModel(mgr,path,0)`, and
  **stores the model at `unit+0xB4`** (`0x7437AD mov [esi+0xB4],edi`). Then, gated on
  `unit+0xB4!=0`, it inserts a **world visibility/cull node via `0x781A10` → stored at
  `unit+0xB8`**. So `unit+0xB4` = the active display MODEL; `unit+0xB8` = the world cull
  node. (`unit+0x98C` is the *separately-attached* model slot used by `SetModel`/
  `0x73D5D0`; a normal creature's `0x73D5D0` reads `[unit+0xB4]+0xB4` as the parent
  world matrix and child-links its model under `unit+0xB4` via `0x831630`.)
- **`0x73D5D0` (build-active-model) is one-shot, not per-frame** — called from the
  create-appearance handler `0x73FCC0` (`0x73FF74`) and the SetDisplayId field handler
  (`0x7404DD`, on `unit+0x9C0` change). It requires `unit+0xB4` to already be a valid
  node (it derefs it with no null check → crash if 0) and `unit+0x98C==0` (build gate).

### Objmgr registration is NOT required to DRAW (only for lookups/owner-follow)

`EnumVisibleObjects 0x4D4B30` walks only the visible list `[container+0xAC]` (linkoffset
`+0xA4`); ~28 callers span both the update tick and render-collect. The two owner
callbacks the model registers (`0x823FE0`→`0x73C140`, `0x824060`→`0x734A40`) re-resolve
the owner via `ObjectPtr 0x4D4DB0(guid)` and **fail safe (no-op) when the guid doesn't
resolve** — so an unregistered fake unit's model still draws and idle-animates
generically. Registering it (visible link `0x6DED60(container+0xA4, obj)` + GUID-hash
insert `0x4D6A70`) only adds unit-state-driven animation and makes selection/nameplate
resolve it — and runs the full per-frame unit code against the fake object (crash risk).
**For a static preview: do NOT register** (safest). Manager resolve is thread-local:
`mgr = *(u32*)(fs:[0x2C] + [0xD439BC]*4); container = *(u32*)(mgr+8)`.

### Client-only preview recipe (target design)

1. Build the fake CGUnit: `alloc 0x4D4930(typeid 3)` → `0x4D45B0` → full ctor
   `0x73F660` with a **real captured create-descriptor** (position at desc `+0x28/2C/30`
   overridden to the cursor). `unit+8` (identity block; `[+0][+4]`=guid) must be valid —
   the pool/descriptor path provides it; the base ctor `0x745E60` derefs `[unit+8]+0x10`.
2. Populate the display sub-object so `GetDisplayId` resolves the workshop path:
   ensure `[unit+0xD0]+0xF4` = the workshop **display id** (CreatureDisplayInfo id;
   DBC range `[0xAD34C8]..[0xAD34C4]`, table `[0xAD34D8]`). **OPEN: whether our
   hand-built unit has a valid `unit+0xD0` sub-object — being measured by the
   `localunit DIAG` log line.** If null, we must build/point it.
3. Set the unit's world position/orientation to the cursor (for placement + cull).
4. Call **`finalize 0x743760(ecx=unit)`** → builds the model into `unit+0xB4` +
   inserts the world cull node `unit+0xB8`. Verify both become non-zero.
5. Do NOT objmgr-register. Per aim: update position + re-place (re-`finalize` is heavy;
   prefer re-running just the placement/transform tail, or move the unit and let the
   world node follow).
6. Translucency ~0.4 / non-target / no-collision afterwards.
7. **Teardown (mandatory, reverse order):** the model self-links into `[manager+8]` and
   the per-asset list `[asset+0x14]`, and finalize's node into world scene structures —
   free via the engine model/object destroy path (unit virtual dtor), never a raw free,
   or the next manager/scene walk derefs a dangling node (the exit crash we saw at
   `0x8327B0`). Unlink on zone change / loading screen (the client rebuilds the manager).

### Frustum cull uses `model+0xF4`, NOT `model+0xB4` (2026-08-15, run wf_91d52062)

A manager-list model is frustum-culled (and bucketed for DrawPass) in **`0x81CFF0`**, driven by
manager render `0x81DF10(ecx=[0xCD754C])`. The per-model loop walks the RENDER list
`[manager+0x114]` (next-link `model+0x2dc`) — NOT the `[manager+8]` master/creation list. Survivors
are appended to the pass-12 bucket `[manager+0x118]` (== `0x58 + 12*0x10`) that DrawPass reads.

The visibility sphere is rebuilt every pass from the **shared M2 asset local bbox**
`*([model+0x2c]+0x150)+0xBC` (min@+0x00, max@+0x0c, radius@+0x18) **transformed by the cached world
matrix `model+0xF4`** (16 floats, immediately after the render matrix `model+0xB4`). The engine
recomputes `model+0xF4` from `model+0xB4` inside this loop **only** when a gate passes
(`[view+0x14] != model+0x3c` && `model+0x2d4==3` && `model+0x48==0`); otherwise it reuses the cached
value, and for scene-enrolled units the per-frame update loop refreshes it. A hand-driven preview
that only writes `model+0xB4` leaves `model+0xF4` frozen at build time → the render follows the
cursor but the **cull sphere is stale**, so rotating the camera off the build spot culls the model
(vanishes). **Fix: mirror `model+0xB4` → `model+0xF4` every frame** (both are plain world matrices;
scene root `[0xCD754C]+0x84` is identity for a top-level preview). Other gates that must already hold
(finalize sets them): `model+0x10 & 1`, and `model+0x3c != -1` (−1 makes `0x81CFF0` skip the model).
No per-model "cull-exempt" flag exists on this path.

### The REAL per-camera-angle gate: the world-scene visibility node `unit+0xB8` (2026-08-15, run wf_4b4b95c6)

The frustum cull above (`0x81CFF0`, `model+0xF4`) is **downstream** and was NOT what hid our preview
by camera angle. The actual gate is the **world-scene spatial visibility node** at `unit+0xB8`:

- Finalize `0x743760` builds it via `0x781A10` (stored `unit+0xB8`), seeding its world AABB
  `node+0x60..0x68` (min) / `node+0x6C..0x74` (max) to the sentinel `[0xA3E864]=1e7`, then calls the
  positioner **`0x7370D0`** (CGUnit vtable+0x14, invoked as `(unit, 0)`, `ret 4`) which reads the unit
  world matrix (vtable+0xC4 `0x722B50`, from `[unit+0xF5C]+0x10`, world XYZ at `+0x40/44/48`),
  transforms the model bbox, and calls bounds-writer/refiler **`0x780240`** → writes the node AABB and
  refiles the node into the scene grid `[0xCE04A8]`.
- Each frame the world-scene enumeration of `[0xCE04A8]` frustum-tests `node+0x60/0x6C`; if in-frustum
  it fires the node callback (`node+0x90`=`0x4F9F70` → `0x4F8D10`) which enqueues the model into the CM2
  render list `[mgr+0x114]`. **If the node's fixed cell is behind/outside the frustum, the model is
  never enqueued** — DrawPass never sees it. Camera-yaw split, cursor-independent.
- A model-matrix-only preview updates `model+0xB4` (render, follows cursor) but never the unit position,
  so the node stays frozen at the build point → visible over ~180° of yaw only.

**Fix (what a moving creature does):** each frame write the cursor into the **CGUnit world position**
`unit+0x798/0x79C/0x7A0` (`= [unit+0xD8]+0x10/14/18`; also `[unit+0xF5C]+0x40/44/48` if present), then
call the positioner `0x7370D0(ecx=unit, push 0)` — it rebuilds the node AABB and refiles it, so the
visibility cell tracks the cursor. Verified: fixes the camera-angle culling completely.

**Facing the character:** the model's world matrix (`model+0xB4`) 3×3 is a yaw built from
`normalize(playerPos − cursor)`; local +X → toward the player. Player pos via objmgr: `objmgr =
fs:[0x2c][ [0xD439BC] ]`, `container=[objmgr+8]`, active-player GUID `[container+0xC0/0xC4]`,
`ObjectPtr 0x4D4DB0(guidLo,guidHi,typemask=8,0,0)` (__cdecl), pos at `[player+0xD8]+0x10/14`.

### M2 skin streaming — the render-node submesh (`[unit+0x8c]+0x54`) and its per-frame re-bind (2026-08-15, run wf_1aea52f7)

The render-node submesh index is resolved by `0x831630` (invoked via the virtual bind `0x744460`,
`this=unit`, ret 0): `group = [unit+0xB4]` (the model); geometry chain `[model+0x2c]` (shared M2 asset)
→ `+0x150` (skin) → `+0xf8` (submesh→batch remap `{count@+0, u16*@+4}`) → `remap[idx]`, stored to
`[unit+0x8c]+0x54`. It returns **`0xFFFF` when the skin is unloaded** (remap empty). The skin is on the
**shared, refcounted asset** (`[asset+8]` bits 0&1 = skin resident), so re-creating the per-unit model
wrapper does NOT re-stream it. The streaming system evicts the skin of a hidden/distant model → the
submesh sticks at `0xFFFF` → invisible; re-finalize can't fix it (it re-reads the same empty remap and
early-bails on the still-set `unit+0xB8`). **Fix (what the stock per-frame unit updater `~0x7439c0`
does, which a hand-rolled loop must replicate):** each frame, if `[asset+8]&1==0` kick the async
re-stream `0x823ED0(ecx=model, push 0)` (ret 4); and re-run the bind `0x744460(ecx=unit)` to re-resolve
the submesh once `[asset+8]&3==3`. No rebuild needed. (Keeping a ref on the model is required so the
shared asset object isn't freed — only its bytes stream.)

### Clean teardown of a hand-built fake CGUnit (not in objmgr) — run wf_1aea52f7, high-confidence

To full-rebuild without leaving rendering copies or leaking, tear the old unit down in this order
(inverse of the build; verified against `~CGObject 0x745F90`):
1. **Remove the scene node** `0x7826E0(node=[unit+0xB8])` (cdecl, 1 stack arg); then `[unit+0xB8]=0`.
   It detaches the model from the render hookup (`0x8274F0`) but does NOT touch the model refcount.
2. **Release the model** `0x7431E0(ecx=unit)` — detaches (`0x8251B0/0x823FE0/0x824060`) then releases
   `[unit+0xB4]` via `0x824ED0` (unlinks from `[manager+8]`, pool-frees), and zeroes `unit+0xB4`.
3. **Free the unit block** `0x4D4090(esi=unit)` — the dispatcher counterpart of alloc `0x4D4930`;
   routes to pool-free `0x4D3100` or `SMemFree 0x76E5A0` by how the block was backed. **`esi` = this
   (save/restore esi).** NEVER free via the vtable deleting dtor `0x737BA0` (hardcodes `SMemFree` →
   heap corruption on a pool-backed block), and NEVER zero the GUID before freeing (the dispatcher
   routes the pool by it). This path never walks the objmgr visible list and never asserts.

**Field cheatsheet:** `unit+8`=identity(guidLo/Hi at `[+0][+4]`), `unit+0xB4`=active
display model (finalize), `unit+0xB8`=world cull node, `unit+0xD0`=display sub-object
(`+0xF4`=displayId, `+0xFC`=count), `unit+0x98C`=SetModel/attached model slot,
`unit+0x9C0`=UNIT_FIELD_DISPLAYID, `unit+0x9D4`=displayId override flag (0x717A20).
Model: `+4`=flag byte (NOT a pointer), `+0x2C`=shared M2 asset, `+0x8/+0xC`=manager
master-list links, `+0x24`=owner, `+0x48`=render-node handle, `+0xB4`=world matrix,
`+0x98`=matrix palette, `+0x10` bit0=loaded / bit `0x400000`=has-owner-callback.

---

## 1. CM2Model instance struct (verified offsets)

| Off | Meaning | Notes |
|---|---|---|
| `+0x04` | byte flags | bit5 `0x20` = "has skinned M2" (drain/emit gate on the bit0-clear path only) |
| `+0x10` | dword flags | bit0 `0x1`=loaded; `0x2`,`0x20`,`0x8000`=set as after-effects of a real draw; `0x1000`+`0x400000` set by PREPARE (`0x400000`=poseable/skeleton-dirty) |
| `+0x14` | word | concat start-index; `0xFFFF` makes concat skip the hierarchy walk |
| `+0x28` | manager ptr | `== *0xCD754C` for a model created with the manager as owner |
| `+0x2c` | **rd** (renderData / shared M2 asset) | shared across all instances of the same model file |
| `+0x3c` | frame stamp / pose dedup | pose `0x831990` skips if `== [manager+0x14]` |
| `+0x48` | (gate) | pose skips if `!= 0` |
| `+0x64` | concat gate | must be `0` |
| `+0x90` | **bone count (bonec)** | **only set on the animate/finalize path** (`=1` store at `0x828F7B`); NEVER set by PREPARE. Skinning loop iterates this many bones. |
| `+0x94` | bone-runtime array ptr | stride **`0xAC`** per bone; per-instance; realloc'd by PREPARE |
| `+0x98` | **render-matrix buffer ptr** | NOT inline — a pointer. `[m+0x98][0]` (16 floats) = the model→view skinning matrix for bone 0 |
| `+0x9c`,`+0xa4`,`+0xac` | per-instance GPU stream/decl handle arrays | built by PREPARE; non-null when built |
| `+0xa0` | **color-anim output array** | ptr; entries stride `0x20`, RGBA at `+0x10/14/18/1c`; only used if `colorCount>0` |
| `+0xa8` | **transparency-anim output array** | ptr; entries stride `0x0c`, value at `+8`; **zero-init by PREPARE, only made 1.0 by the animate pass** |
| `+0xB4` | **world matrix** (4x4, row-major) | `M[0/5/10]`=rot diag, `M[12/13/14]`=world XYZ, `M[15]=1.0`. A real static workshop holds identity-rot + translation (no scale). |
| `+0x100..0x2BC` | posed bone-matrix block (inline) | intermediate; NOT the skinning palette (that's `*(m+0x98)`) |
| `+0x140` | dynamic vertex-buffer pool | `0` for both preview and real workshop (not the discriminator) |
| `+0x170`,`+0x178`,`+0x17c`,`+0x180`,`+0x190` | floats/handles | `+0x178`&`+0x17c` are the alpha sources (see §4); measured `1.0` on both preview and real |
| `+0x19c` | **base alpha (float)** | the alpha the cull reads; `= m+0x17c * m+0x178`, written by pose `0x831990` |
| `+0x2d0` | per-instance batch/render-context override | `0` for an ownerless model → draw takes the shared `[rd+0x170]` skin path |

`rd` (`m+0x2c`) offsets: `+0x08` loaded flags (**`& 3 == 3` = geometry+data ready** —
the authoritative "loaded" predicate, NOT any count field); `+0x150` = **geo/md**
(shared geometry+material defs); `+0x170` = **skin** (batch defs); `+0x198` word (pose gate).

`skin` (`[rd+0x170]`) offsets: `+0x24` = **batch count** (e.g. workshop = `0x23` = 35);
`+0x28` = batch-entry array.

`geo/md` (`[rd+0x150]`) offsets: `+0x48` = **color-track count**; `+0x58` =
**transparency-track count**; `+0x74` = material-flags array; `+0x94` = transparency
remap table (word per track).

---

## 2. Manager singleton & per-frame pipeline

- **CM2 manager singleton** = `*0xCD754C`. `+0x14` = current frame stamp (pose dedup).
  `+0x84` = **world→view matrix** (4x4) — updated per view; used by the pose. `+0x28` =
  head of the **active/pose list** (intrusive, `next` at node `+0x44`).
- World render `~0x79ABDF` runs, over the active list: **PoseAll `0x81C9C0`** (→
  `CM2Model::pose 0x832450` → concat `0x832260`) then **AnimateAll `0x821A20`** (→
  PREPARE `0x823ED0` → `0x832EA0`, plus the color/transparency animate that fills
  `m+0xa0/0xa8`). **The manager list poses+animates; it does NOT itself draw.**
- **SetListed `0x823F10`** (`__thiscall(model, enable)`) enrolls/removes a model in the
  active list. **DANGER: enrolling an ownerless model HANGS** — PoseAll's concat walks
  the bone parent-chain and our uninitialized chain is self-referential → infinite loop.
- **SetAnimCallback `0x823FE0`**, anim-notify `0x6F7480` (handles anim event `0x7F`; not a fader).

---

## 3. PREPARE `0x832EA0` and COLLECT `0x834660`

### PREPARE `0x832EA0` (`__thiscall(model)`, ret 0)
Builds the per-instance GPU buffers (`m+0x94/0x9c/0xa0/0xa4/0xa8/0xac` via device-buffer-lock
`0x76E540`; string "M2Model.cpp" line `0x341`) and reallocates `m+0x98`. Sets
`m+0x10 |= 0x1000` (`@0x83425C`) and `|= 0x400000` (`@0x834313`). **Does NOT touch
`m+0x90` (bonec).** Leaves `m+0xa8` transparency **zero** (only the animate pass fills it).
Calling it repeatedly reallocates (leaks) — call once/frame if you drive it yourself.

### COLLECT/draw `0x834660` (`__thiscall(model, bucketA, bucketB)`, ret 8)
```
0x834667  mov esi, ecx                 ; esi = model (this) for the whole fn
0x834669  test byte [esi+0x10], 1      ; bit0 (loaded)?
0x83466D  jne  0x834694                ;   set  -> skip prep, go pose
0x83466F  test byte [esi+4], 0x20      ; (bit0-clear path) skinned-M2 gate
0x83468F  call 0x832EA0                ; (bit0-clear path) PREPARE
0x834694  mov ecx, esi; call 0x831990  ; internal pose (matrix + base alpha)
0x83469C.. batch-loop setup: edx=[rd+0x170]=skin; [ebp-0x10]=skin;
           [ebp-4] = (m+0x2d0 ? [m+0x2d0+4] : [skin+0x24])   ; loop bound (35)
0x8346DA  test eax,eax; jbe 0x8347e7   ; bail if 0 batches
... per-batch loop (see §4) ...
0x8347CA  call 0x823D50                ; ENQUEUE surviving batch
```
**Key trap:** matching a real model's `m+0x10` bit0=1 makes `0x834660` skip its own
PREPARE. An ownerless model then never gets buffers unless you call `0x832EA0` yourself
(with bit0 temporarily cleared, since prep mirrors the same gate).

### Internal pose `0x831990` (`__thiscall(model)`)
```
0x83199F  cmp [esi+0x3c], [manager+0x14]; je 0x831ad8   ; dedup: skip if already posed this frame
0x8319AB  cmp [esi+0x48], 0;  jne skip
0x8319B7  cmp word [rd+0x198], 0; jne skip
0x8319C5  eax = manager+0x84                            ; VIEW matrix
0x8319D1  push m+0xB4 (WORLD) , push scratch(dst)
0x8319D6  call 0x4C1F00                                 ; scratch = WORLD * VIEW
0x8319DB  mov ecx, [esi+0x98]                            ; dst = *(m+0x98) render buffer
0x8319E5  call 0x407F80                                 ; copy 16 floats -> *(m+0x98)[0]
0x8319EA  fld [esi+0x17c]; fmul [esi+0x178]; fstp [esi+0x19c]  ; base alpha = 0x17c*0x178
0x831A08  cmp [geo+0x48], 0; jbe skip-color              ; skip color loop when colorCount==0
```
So `*(m+0x98)[0] = m+0xB4 · (manager+0x84)` is the model→view skinning matrix, and
`m+0x19c` is the base alpha — both written here **only if the dedup gate passes**.

---

## 4. The per-batch alpha cull (why batches silently vanish)

Inside `0x834660`'s loop, before a batch enqueues:
```
0x834715  cmp [ [esi+0x9c] + visIdx*4 ], 0 ; je skip   ; per-instance VISIBILITY (all 1s normally)
0x83471F  cmp word [batch+2], 0x8000       ; je skip   ; shared batch flag
0x83472B  test byte [batch], 4             ; jne skip  ; shared
0x834734  cmp word [batch+0xc], 0          ; ja  skip  ; shared
0x834755  test material_flags, 0x40        ; jne skip  ; shared (geo+0x74)
; --- alpha cull ---
0x834771  fld  [esi+0x19c]                 ; base alpha (=1.0 when pose ran)
0x834777  cmp edx,[geo+0x48]; jae +        ;   colorCount gate: skip color mul if idx>=count
0x834785  fmul [ [esi+0xa0] + colorIdx*0x20 + 0x1c ]     ; * color.alpha
0x83478C  cmp word [batch+0xe], 0; je +     ;   skip transparency mul if track idx 0
0x83479D  movzx eax, word [ [geo+0x94] + transpIdx*2 ]   ; remap
0x8347A4  fmul [ [esi+0xa8] + remap*0x0c + 8 ]           ; * transparency value
0x8347AE  fcomp [0x9EDCE0]                  ; threshold = 0.55 (0x3F0CCCCD)
0x8347B9  jnp  0x8347cf                     ; alpha < 0.55 -> CULL (skip enqueue)
0x8347CA  call 0x823D50                     ; else ENQUEUE
```
A **second identical cull** exists in the CPU batch-emit at `0x821EF0` (threshold
`0x9E8CD0`), reading the same caches — both must pass.

**The gotcha that cost the most time:** `m+0xa8` (transparency output) is zero-init by
PREPARE and only filled to `1.0` by the animate pass, which an ownerless model never
runs. So `alpha = 1.0 * <skipped> * 0 = 0 < 0.55` → **all 35 batches culled before
enqueue** (measured: 0 enqueues). Fix: after PREPARE, pin
`*(m+0xa8)[j*0x0c+8] = 1.0` for `j in 0..geo+0x58`, and pin `m+0x19c/0x17c/0x178 = 1.0`.

---

## 5. Enqueue, buckets, drain, draw

- **Enqueue `0x823D50`** (`__thiscall(vector)`): appends a 12-byte record `{esi=model,
  ebx=batchIdx, 1=runlen}`. **Fixed capacity** — `cmp [ecx+4],[ecx+8]; jae ret` drops
  silently if full (no grow). Called only from `0x8347CA`.
- **Bucket array** = `0xD25320`, **stride `0x24` per view/node**. Record layout:
  `+0x00` indexed queue; `+0x0c` **bucketA (opaque)** `{data, count@+0x10, cap}`;
  `+0x18` **bucketB (blended)**. So for view `v`: opaque bucket = `0xD2532C + v*0x24`,
  blended = `0xD25338 + v*0x24`. A batch routes to bucketB if `word[batch+2] != 0`.
- **Drain `0x829E40`**: iterates the bucket; per record `edi = model`, `[rd]` shared.
  Gates `0x8360A0` (`@0x829EA0`) and `0x8362B0` (`@0x829EAE`) both take `ecx = rd`
  (**shared** — cannot differ from a real model); `je 0x829F21` drops on 0.
  Reaches `0x829BA0` → `CGxDevice *0xC5DF88` `DrawIndexedPrimitive`.
- Opaque body batches drain **flag==1** path (`jne 0x829c0c @0x829BB0`) which **skips
  the `[model+0xa4]` SetTexture block**; only the single blended batch uses `[model+0xa4]`.

---

## 6. Two separate subsystems: SHADOW (bucketed) vs BODY (instanced)

**The `0x834660` + `0xD25320` bucket pipeline is the planar-SHADOW subsystem, not the
body.** The whole `0x7BD200` collect subtree (`0x7BB9D0`, `0x7BC490`, `0x7BCC00`,
`0x7BC890`) is one registered 4-slot descriptor at `0xD43158` (setup `0x7BAC10`, cull
`0x7BAFD0`, collect `0x7BD200`, flush `0x7BBC50`); the flush **unconditionally folds the
projector `*0xD43180` into the world matrix at `0x7BBEDE`**. `0x823D50` (bucket append)
has exactly one caller (`0x834660`); the drain `0x82DA40 → 0x829E40 → 0x829BA0 →
DrawIndexedPrimitive` has one entry inside that projecting flush. So **anything fed into
`0x834660`/`0xD25320` renders as a projected ground shadow** — proven by the `+50y`
test (output slides along the ground instead of lifting).

- Our shadow hook: **`0x7BBC10`** (after `0x7BB9D0`'s `0x834660` call; `[ebp-0x10]`=this
  view's bucket, `[ebp-4]`=object). Draws the preview's shadow. Fine to keep for a shadow.
- Dead end: `0x7BD180`/`0x7BD1AD` (pushes `0xD2532C/0xD25338`) — a WMO/indoor path;
  a hook at `0x7BD1B2` fires **0×** outdoors.

**The LIT BODY renders through a separate IMMEDIATE, INSTANCED loop:**
- Draw loops **`0x7E3D20`** (and `0x7E4233`) iterate `model = [0xD38014 + idx*4]`,
  `idx` in `0..[0xD38054]` (array base `0xD38014`, count `0xD38054`; loop bound
  `cmp ebx,[0xD38054]` @`0x7E3E0B`). List built @`0x7E3605`/`0x7E3678` (count),
  array pushed @`0x7E3661`.
- Per object (`0x7E3D80..0x7E3E03`): copies the model's 4×4 matrix (from `[eax]`,
  16 floats) into a **device matrix-array slot** `[arrayBase + idx*0x40 + 8]`; calls
  setup `0x872B00` (@`0x7E3DE6`) and `0x873900` (@`0x7E3DF8`, arg via `0x984C90`);
  `mov ecx,[ebp-0x18]` (=model); **`call 0x829AA0`** (@`0x7E3E03`).
- **`0x829AA0`** (`__thiscall(model)`, ret 0): the per-object body draw. `@0x829AA9
  test byte[esi+0x10],1; jne 0x829AE3` → a loaded model (bit0 set) jumps to the draw at
  `0x829AE3`; uses the model's own rd (`m+0x2c`) and palette (`m+0x98`).

**DEAD END — `0x7E3D20`/`0x829AA0` never runs outdoors.** A probe on `0x829AA0` measured
**total=0** calls: that whole instanced loop is a dead path for outdoor creatures (like
`0x7BD180` before it). Static RE cannot tell which M2 path runs for a given scene — four
guessed paths (`0x7BD180`, `0x7E3D20`, `0x7E3AC3`, `0x829AA0`) all fired 0×.

### The REAL body draw — found empirically (call-stack capture)
Probe the low-level vertex-buffer bind **`0x8362B0`** (`__thiscall(rd)`, prologue
`55 8B EC 83 EC 14`), filter to the workshop rd (`[model+0x2c]`), and walk the ebp chain.
The workshop binds its geometry from **two** distinct stacks:
- **SHADOW:** `…→0x7BC3BF→0x876297→0x7BB65B→0x79AC26→0x77F003` (the `0x7BB`/`0x7BC` planar-shadow subsystem, via drain `0x82DA40`/`0x829E40`).
- **LIT BODY:** `0x494F67 → 0x485128 → 0x4FB042 → 0x4F9122 → 0x823D24 → 0x823A88 → 0x8205AF → [0x81F700] → 0x8362B0`. This goes through the **object-render region** (`0x4F`/`0x49`) into the **CM2 draw** (`0x823`/`0x820`) — NOT the shadow subsystem. `0x81F700` is a tiny `__thiscall(x)` `ret 4` helper (gated on `[0xD43020]`) that does `mov ecx,[ecx+0x68]; call 0x8362B0` (so its `this+0x68` = rd).

**Method to find any object draw path:** hook a shared low-level draw (`0x8362B0` vtx bind,
or `0x8360A0` idx), filter by the target rd, and walk the ebp return-address chain
(`fp[1]`=return, `fp[0]`=next frame). This reveals the *actual* runtime path — the only
reliable way, since static xrefs can't distinguish which of many M2 paths a scene uses.

**Injection point (per-model level in the body stack):** under investigation — mapping the
`0x8205AF`/`0x823A88`/`0x823D24` (CM2) and `0x4F9122`/`0x4FB042` (object-render) frames to
find where to invoke the per-model draw for our ownerless model, or a model list to append
to (workflow `wt20pg121`).

---

## 7. Bones: concat hang and the safe path

- **CM2Model::pose `0x832450` → concat `0x832260`** walk the bone parent-chain by
  `word[boneRt[i]+0x96]`, terminating on `0xFFFF`. **`bonec` (`m+0x90`) is NOT read by
  concat** — it can't bound the loop. An ownerless model's `+0x96` is garbage
  (self-referential) → infinite loop → **client freeze**. Never SetListed/PoseAll/
  concat an ownerless model without first sanitizing the chain.
- **Per-bone-runtime ctor `0x82BD60`** (`__thiscall(rec)`): zero-inits the record,
  identity SRT, sets `+0x96 = 0xFFFF`, `+0x4a = 1`. Use to sanitize `boneRt[0]`.
- For the summon preview we **avoid the concat entirely** — we write `*(m+0x98)` and
  poke `m+0x90=1` by hand, no enrollment.

---

## 8. Matrix / helper functions

| Addr | Signature | Effect |
|---|---|---|
| `0x4C1F00` | `__cdecl(float* dst, float* a, float* b)` | `dst = a * b` (4x4 row-major) |
| `0x407F80` | `__thiscall(float* dst, const float* src)` | copy 16 floats (0x40 bytes), ret 4 |
| `0x832AB0` | PlayAnim (`m, a0, seq, a2, a3, spd, a5, a6`) | seq `0x7F` gives a compact static pose via the emit's internal pose |
| `0x4F20C0` | BindModel `__cdecl(model, displayRec, modelDataRec)` | attach display skin + geometry |
| `0x76E540` | device buffer lock | used by PREPARE |

---

## 9. Cursor → world point (reused from collision project)

`CWorld::Intersect` real `0x7A3B70` (thunk `0x77F310`), prologue `55 8B EC 83 EC 18`
(steal 6, resume `0x7A3B76`). Signature:
`char Intersect(Vec3* start+8, Vec3* end+0xC, void* hitOut+0x10, float* pFrac+0x14, int flags+0x18, int +0x1C)`.
**World hit = start + (*pFrac)·(end − start).** The summon mod wraps it
(`IntersectWrapper`) and captures the hit on **flags `0x01020124`** = the aiming
cursor→terrain pick (`g_hitA`). The reticle world point is also captured directly from
the ground-target targeting render via a hook at `0x6FD72C` (resolver `0x6FCD60`,
out-vec `[ebp-0x34]`).

---

## 10. The summon-preview mod (`src/goeditor.cpp`, `[SummonPreview]`)

Ownerless model created with `CreateModel(*0xCD754C, path, 0)` + `BindModel 0x4F20C0`.
Model resolve is **find-only** (`GetRecord` arg4=0) → the creature must already be
rendered this session (summon one workshop first, then aim). Per frame:

- **`SummonPreview_Tick`** (called every `Intersect`): creates/keeps the model; on
  `rd+8 & 3 == 3` does one-time setup (`m+0x90=1`).
- **`PreviewPlace`** (called from the collect hook, once/frame guarded on
  `manager+0x14`): writes `m+0xB4` = identity-rot + `g_hitA`; calls PREPARE `0x832EA0`
  (bit0 cleared around it); writes `*(m+0x98)[0] = 0x4C1F00(scratch, m+0xB4, manager+0x84)`
  then `0x407F80`; pins `m+0x178/0x17c/0x19c = 1.0` and `*(m+0xa8)[j*0x0c+8] = 1.0`.
- Hooks: `0x7BBC10` (shadow-walk, draws the shadow), `0x7BD1B2` (main-view — currently
  dead, being re-sited), `0x6FD72C` (reticle world point), `0x7A3B70` (Intersect wrapper).

**Build/deploy:** `clientedits\build.bat` (compiles into `dinput8.dll` +
`AnimSpeedFix.dll`), then `clientedits\deploy.ps1` (only when `Wow.exe` is closed).
Enable via `[SummonPreview] Enabled=1` in the client `AnimSpeedFix.ini`.

---

## 11. Status (2026-08-14) — the ownerless-model wall & the local-object pivot

The real lit-body path is **`CM2Model::DrawPass 0x823CB0(model, passIndex)`**, called by
orchestrator `0x4F8EA0` (hook the pass-0 return `0x4F9122`). But an **ownerless** model
cannot go through it: `DrawPass` does `mov ecx,[esi+4]; mov eax,[ecx+4]` — **`model+4` is
a pointer to the model's OWNER / render-node** sub-object (reads `*(model+4)+4` for flags,
`& 0xE000`). A real workshop has it (its game object built the node); a `CreateModel`+
`BindModel` model has `model+4=null` → deref `[null+4]` → **crash (ERROR #132 at 0x823CDF
reading 0x24)**. NOTE: the old `m+4 |= 0x20` flag-hack (for the emit/shadow path, which
reads `m+4` as a flag *byte*) forced null→0x20 and caused the crash; `m+4` is a POINTER for
DrawPass, a flag-byte for the emit — do not set it. Real `m+4` is a pointer whose low byte
is 0 (so a BYTE dump reads 0, misleadingly).

**This is the crux of "ownerless":** the real body draw assumes a scene-object render-node
exists. Chasing per-instance fields one at a time (color, transparency, bonec, buffers,
`m+0x144`, `m+4`, render-op arrays `m+0x3c`/`0x58`/`0x48`) is whack-a-mole — each fix reveals
the next. **DECISION (user-directed): create a CLIENT-ONLY local game object to OWN the
model**, so the engine builds the render-node and draws it (body+shadow) through its own
path. Constraints: client-side only (no SMSG_UPDATE_OBJECT), zero server traffic, invisible
to others, no collision, removable when aiming stops, translucent (`0x3ECCCCCD`=0.4).
Mapping the client object-create/register/destroy path in workflow `wtl4yxpet`
(anchors: objmgr `0x4D4DB0`/`0x4D4B30`, CGGameObject_C `~0x70B000-0x713000`, RTTI `0x9F3964`).

**Reusable technique proven this session:** to find any object's *real* runtime draw path,
hook a shared low-level draw (`0x8362B0` vtx bind / `0x8360A0` idx), filter by the target's
rd (`[model+0x2c]`), and walk the ebp return-address chain. Static xrefs cannot tell which
of many M2 paths a given scene uses; this shows what actually executes.
