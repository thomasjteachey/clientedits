# Buff-gated player collision (Tier A) — reverse-engineering findings + design

Target: **Wow.exe 3.3.5a build 12340** (`clients\centurion\Wow.exe`, imagebase
0x400000, x86). Goal: make players physically block each other's movement, but
only while a chosen buff (aura) is present — shipped in the same drop-in
`dinput8.dll` as AnimSpeedFix.

Everything below was located **in this exact binary** with
`tools\clientre\peexplore.py` (assert-string anchors → xrefs → call-graph),
not from remembered addresses. Each address should still be re-confirmed by the
DLL's own byte-signature guard at load time (AnimSpeedFix pattern) so a different
client build fails safe instead of crashing.

## Why Tier A works at all

Player movement in 3.3.5 is client-authoritative: each client runs full collision
physics every step and reports the result in `MSG_MOVE_*` heartbeats. Units are
simply never in the collision set. If each client refuses to move its own player
into a "solid" buffed player, that is the whole feature — it propagates to
everyone through normal heartbeats. Symmetric as long as both run the DLL, which
`client-tweaks` already guarantees (mandatory launcher patch).

## Verified addresses (this binary)

### World collision (shared — do NOT hook these directly for movement)
| Addr | What | Evidence |
|---|---|---|
| `0x0077F310` | `CWorld::Intersect(...)` low-level ray/sweep | 21 callers (camera, LoS, spells, movement); result validated by the `terrDist` assert |
| `0x004F9930` | collide sweep `__thiscall(this, Vec3* start /*+8*/, Vec3* end /*+0xC*/, int flags /*+0x10*/)` — normalizes `end-start`, picks collision mask `0x1000124`/`0x1020124`, calls Intersect | prologue + `push 0x9F9928` (terrDist assert) at 0x4F9A4C/0x4F9BBF |
| `0x004F9DA0` | collide-**and-slide** resolver (iterates sweep, `cmp ebx,2/jge` slide cap) | sole caller of 0x4F9930 at 0x4F9F00 |

These are shared by camera and line-of-sight, so hooking them would make the
camera collide with players and spell-LoS get blocked by them. Wrong altitude.
Kept here because the player-cylinder test should mimic what 0x4F9930 returns.

### Unit movement / physics
| Addr | What | Evidence |
|---|---|---|
| `0x0073A890` | CGUnit_C per-unit physics tick, `__thiscall` (`this`=ecx→edi), 1 stack arg; gates on `[unit+0x7CC] & 0x1000000` | calls ground-check + CMovement methods |
| `0x00714B60` | CGUnit_C ground/height query — downward ray (mask `0x100111`) via Intersect, returns distance | called from tick at 0x73AA87/0x73AAF2 |
| `~0x00686500–0x00687180` | CMovement position **get/set** cluster — reads/writes `[reg+0x798]` as floats (`fcom [ecx+0x798]`, `mov [edx+0x798], eax`) | direct byte scan for `98 07 00 00` FP loads |

**CMovement is embedded at `unit+0x788`** (tick does `lea ecx,[edi+0x788]` then
calls CMovement methods at 0x6EFxxx). **Position = movement+0x10 = unit+0x798.**

Confirmed unit field offsets (12340):
- `+0x798` float X, `+0x79C` float Y, `+0x7A0` float Z, `+0x7A4` float facing
- `+0x788` CMovement sub-object
- `+0x7CC` movement/state flag dword (bit `0x1000000` gates the physics tick)

### Object manager (verified)
| Addr | What | Evidence |
|---|---|---|
| `0x004D3790` | `ClntObjMgrGetActivePlayerGuid()` cdecl → 64-bit in edx:eax | TLS `fs:[0x2C]` + `[0xD439BC]*4` → objmgr `[+8]`; active GUID at `objmgr+0xC0` |
| `0x004D4DB0` | `ClntObjMgrObjectPtr(guidLo /*+8*/, guidHi /*+0xC*/, typeMask /*+0x10*/)` cdecl → object* or 0 | hash lookup 0x4D4BB0, type check `[obj+8]→[+8] & mask` |
| `0x004D4B30` | `ClntObjMgrEnumVisibleObjects(cb, arg)` cdecl — walks visible list at `MGR+0xA8`, calls `cb(guidLo, guidHi, arg)` cdecl, stops when cb returns 0 | disasm: reads GUID at node+0x30/0x34, `call [ebp+8]` |
| `0x00D439BC` | objmgr TLS slot index (global) | used by all above |

Object GUID at **obj+0x30 (low) / +0x34 (high)** — consistent across the hash
lookup and the enumerator. Type mask at `[obj+8]→[+8]`; standard values
TYPEMASK_UNIT `0x08`, TYPEMASK_PLAYER `0x10` (pass `0x18` to accept both). The
0x10 value is the one remaining thing worth a glance live, but `ObjectPtr`
returning non-null for a known player already proves it.

### Aura / buff gate (verified — call the client's own functions)
| Addr | What | Evidence |
|---|---|---|
| `0x004F8850` | `CGUnit_C::GetAuraCount()` thiscall → `[unit+0xDD0]`, or `[unit+0xC54]` when that is -1 | disasm |
| `0x004F8870` | `CGUnit_C::GetAuraInfo(index)` thiscall → **spellId** | indexes slot, returns `[slot+8]`; result fed to spell-DB lookup |
| `0x00556E10` | `CGUnit_C::GetAura(index)` thiscall → slot pointer | same small-buffer logic |
| `0x0072C9B0` | filtered buff/debuff *display* search — **too complex, not used** | partitioned structs at +0xDD4 stride 0x84 |

Aura slots are 0x18 bytes; **spellId at slot+8**; array is inline at unit+0xC50 or
heap via `[unit+0xC58]`, selected by the `unit+0xDD0` == -1 sentinel. The DLL does
**not** reimplement that — `UnitHasAura(unit, spellId)` = loop `GetAuraInfo(unit,i)`
for `i < GetAuraCount(unit)`, calling the client's own thiscall functions, so the
small-buffer layout is the client's problem, not ours.

## Hook design

Add the player-cylinder collision **alongside** the game's own world collision,
scoped to the local player only — never inside the shared CWorld routines.

Chosen point: **post-process the active player after its movement tick.**
1. Detour `0x0073A890` (physics tick) with a signature-guarded `jmp rel32` to a
   naked stub (AnimSpeedFix `InstallAt`/`PlaceDetour` pattern).
2. Stub saves `this` (ecx), calls the original via trampoline (replayed stolen
   bytes + jmp back).
3. On return, if `this == ClntObjMgrObjectPtr(GetActivePlayerGuid())` **and**
   collision enabled:
   - read new pos `P` at `this+0x798`, cached previous pos `Pprev`
   - enumerate nearby player units; for each **other** player within radius whose
     `UnitHasAura(gate spellId)` (and, in both-need-buff mode, the local player
     also has it): test 2-D cylinder overlap `dist(P.xy, O.xy) < rSelf+rOther`
   - on overlap, push `P.xy` out along the contact normal; project the frame's
     motion `(P-Pprev)` onto the contact plane to slide (one iteration is enough
     at frame rate). Leave Z alone (2-D wall, don't make players climbable).
   - write clipped `P.xy` back to `this+0x798`; update `Pprev`.

**Implemented** in `tools\animspeedfix\playercollide.cpp` (ships in the same
`dinput8.dll`; `[PlayerCollide]` ini section; OFF by default). Builds clean and
the push-out math + divide-by-zero guard pass in `AnimSpeedSelfTest.exe`. All six
client signatures were verified byte-for-byte against `clients\centurion\Wow.exe`.

As built, the eject runs in the tick's **pre-hook** (before the frame's movement
integrates), which is the simplest safe stub: `pushad`/`pushfd` + `fnsave`, call
the C worker, `frstor` + `popfd`/`popad`, then replay the stolen 9-byte prologue
and jmp to `0x73A899`. It prevents *sustained* penetration, at the cost of a
sub-frame shimmer at the contact because the integration re-enters that frame.
**Polish upgrade** (deferred): a wrapping detour that ejects *after* the tick
returns holds the player exactly at the contact with no shimmer.

**Eject is speed-based, not a fixed snap.** The first version resolved the whole
overlap in one frame, which teleported you across a deep overlap (fast contact,
blink-in, two stacked players) — "zip to the other side." Now the per-frame step
is capped to `(GetCurrentSpeed(0x987570) + PushMargin) * dt`, where `dt` comes
from QPC so it's framerate-independent. Eject speed always slightly exceeds your
movement, so you can't tunnel at any speed (mounts included), but it stays
proportional to your motion so it never reads as a teleport; idle speed is 0, so
a stacked overlap separates at just `PushMargin` (default 6 yd/s). Near-concentric
bodies eject along `-facing` rather than a noisy radial normal. The geometry and
cap are split into pure `ComputeEjection` + `CapStep`, selftested.

Notes / decisions:
- **FPU state is saved/restored** in the stub (`fnsave`/`frstor`) — unlike the
  anim/opcode hooks, this path does FP math and the tick's caller is x87-heavy.
  This is the one place the "no FP in hooks" rule in the AnimSpeedFix README is
  deliberately broken, so it is done explicitly.
- Knockbacks collide (client simulates them through physics — desired). Charge
  splines and Blink teleports bypass the tick (pass through) — acceptable.
- Latency: you collide with the remote player's interpolated displayed position,
  so blocking is slightly soft — expected for an online game.
- Config (ini `[PlayerCollide]`): `Enabled`, `SpellId` (gate buff),
  `Radius` (yards), `Mode` (both-need-buff | either-has-buff),
  `ShowErrors`. Ship OFF by default until validated.
- Cheating: deleting `dinput8.dll` lets that client walk through buffed players
  (others still can't walk through them). If it becomes competitively load-
  bearing, add a server-side watchdog for sustained overlap between two buffed
  players. Out of scope for the client DLL.
- **Playerbots have no client** — their movement is server-side (motion master +
  mmaps), so this DLL does not affect them. Bot collision is a separate
  server-side problem if a mode needs it.

## Live-client verification (single confirmation pass, not a hunt)

Every address/offset is now pinned statically and byte-signature-guarded at load,
so a wrong client build disables the feature instead of crashing. One behavioural
assumption still wants a live confirmation, made self-announcing in code:
1. **The active player passes through tick `0x73A890`.** The DLL logs once, the
   first time it sees the active player's `this` enter the hook — so a single
   in-game step confirms it. If it never logs, the local player uses a different
   update path and we re-point the hook (candidates already mapped: CMovement
   position set cluster ~0x686500).
2. Nice-to-confirm but already implied: TYPEMASK_PLAYER `0x10` (proven the moment
   `ObjectPtr` returns non-null for a real player), and `+0x798` position (proven
   the moment the player visibly moves when we write it).

Ship `Enabled = 0`; flip a debug build against a live character, watch the log
confirm #1, then enable.

## Tooling

`tools\clientre\peexplore.py` — capstone/pefile PE explorer:
`strings PAT` · `xrefs VA` · `dataref VA` · `callers VA` · `entry VA` ·
`disasm VA [N]` · `around VA [N]` · `func VA [MAX]`. Validated against the known
AnimSpeedFix gate site (`0x7388C1`).

## Wanted, not built: scale collision by actual model size

Radius (and optionally Height) are single global values today, so a Gnome and a
Tauren are the same obstacle. Wanted: derive them per unit, so race bulk and
anything that visually enlarges a player grow its collision to match. Height
scaling should be separately switchable from radius scaling.

Sources for the size: the object descriptor's scale field covers scale auras
cheaply but not race (a Tauren's bulk is in its model, not its scale); race bulk
needs the model bounding radius or a race->factor table.

Two things to respect when implementing:

- The block distance is a **Minkowski sum** and must become `rA + rB`. The code
  currently assumes one shared radius and uses `2 * Radius` in several places
  (ClipDelta, FindNearestBlocker, the shape primitives, StandCb), so this is the
  change with the widest blast radius.
- **The server has to agree.** PlayerCollisionServer.cpp hardcodes BLOCK_DISTANCE
  and BLOCK_HEIGHT to mirror the DLL's defaults; if per-unit scaling only lands
  client-side, bots and humans will disagree about where the walls are.

Keep it opt-in (`ScaleByModel = 0`), like every other addition here.
