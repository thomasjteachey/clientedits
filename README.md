# client-edits

Client-side gameplay fixes for **World of Warcraft 3.3.5a build 12340**, shipped
as a single drop-in `dinput8.dll`.

Three independent features live in one DLL, because a client can only load one
`dinput8.dll`:

| Feature | What it does |
|---|---|
| **Vanilla stealth animation** | stealth movement plays at a constant 1.0x instead of speeding up with your movement speed |
| **Swing fixes** | a spell animation landing on the same frame as a melee swing can no longer destroy it; the client's duplicate "flush" swing ~200 ms later is discarded |
| **Player collision** | buff-gated player-vs-player collision — players can be made solid to each other |

Everything is off or inert unless configured, and every patch site is verified
against a byte signature at load: on a mismatch the feature disables itself and
writes a line to `AnimSpeedFix.log` rather than corrupting a different build.

## How it loads

`Wow.exe` statically imports `DirectInput8Create` from `DINPUT8.dll`, and
`dinput8` is not a KnownDLL, so a copy next to `Wow.exe` is loaded ahead of the
system one — during process init, before any game code runs. No injector. The
single export is forwarded to the real `%SystemRoot%\SysWOW64\dinput8.dll`,
resolved lazily so `LoadLibrary` never runs under the loader lock.

Install: copy `dinput8.dll` next to `Wow.exe`. Uninstall: delete it.

## Layout

```
src/          the DLL
  AnimSpeedFix.cpp    host: stealth gate, swing fixes, tracer, dinput8 proxy, DllMain
  playercollide.cpp   player collision (self-contained module)
  trace.h             opcode/animation tracer used to diagnose the swing bugs
  selftest.cpp        offline test harness - no game required
  loader.cpp          optional manual injector
scripts/      checkpatch.py (verify a patch applied), tracefmt.py (read a trace)
reversing/    peexplore.py (disassembler/xref tool) + the collision design doc
sql/          the four collision auras (server-side data)
```

## Build

```
build.bat test
```

Needs the VS x86 C++ toolset. Output lands in `out\`. The `test` argument runs
`AnimSpeedSelfTest.exe`, which rebuilds the patch sites in scratch memory and
proves both the original and patched behaviour without the game.

`package.ps1` produces the distributable zip; `deploy.ps1` copies the built DLL
into a client (and archives the log rather than deleting it).

## Player collision

Movement in 3.3.5 is client-authoritative: each client runs collision every frame
and reports the result in `MSG_MOVE_*` heartbeats — units are simply never in the
collision set. So a client refusing to move its own player into a solid player
*is* the whole mechanic, and it propagates to everyone through normal heartbeats.

It works by clipping the movement delta **before** the client applies it, so an
illegal position is never produced. (An earlier design corrected the position
afterwards; that could never be made reliable, because the client keeps several
copies of your position and re-derives movement from them. The design doc records
that in detail — it is the most useful thing in this repo if you are extending
it.)

Four auras drive it, spanning two axes — who is affected, and one-way vs mutual:

| Aura | Effect |
|---|---|
| Obstruction | enemies cannot pass through you (you still pass them) |
| Immovable | nobody can pass through you (you still pass them) |
| Bodycheck | mutual collision with enemies |
| Solid Form | mutual collision with everyone |

Hostility uses the client's own reaction test — the one behind the `UnitIsEnemy`
Lua API — so it matches nameplate colouring exactly.

Tunables (shape, radius, height, slide friction) are compiled in and overridable
via an optional `AnimSpeedFix.ini`; see that file for the full annotated list.

**Collision is enforced by each client on itself**, so it is only as reliable as
client coverage: a player without this DLL walks through everyone. If it ever
becomes competitively load-bearing, pair it with a server-side check.

## Scope and caveats

- **Build-specific.** Every address targets `Wow.exe` 3.3.5a 12340. A different
  build fails the signature check and disables the feature.
- **The collision auras need server-side data.** `sql/` creates them; without
  those spell ids the collision code is inert. The authoritative copy of that
  script lives in the server repo — this is a mirror so the repo stands alone.
- **Antivirus.** This is an unsigned DLL that rewrites another process's code
  under a system-sounding filename — a textbook heuristic hit. Expect some
  scanners to quarantine it.
