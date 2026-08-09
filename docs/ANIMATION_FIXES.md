# AnimSpeedFix

Client-side fixes for Wow.exe 3.3.5a, shipped as `client-tweaks`:

1. **Vanilla stealth animation** — stealth movement plays at a constant 1.0x
   instead of speeding up with your movement speed. Mounts, running, swimming
   and everything else keep stock behaviour.
2. **SwingGuard** — a spell animation landing on the same frame as a melee swing
   can no longer destroy it before it renders.
3. **DropSlot3Replay** — the client re-issues a swing ~200 ms after placing it on
   the overlay track while moving; that duplicate "flush" swing is discarded.

Target: **Wow.exe 3.3.5a build 12340** (`clients\centurion\Wow.exe`), x86.
All three are on by default and configurable via an optional `AnimSpeedFix.ini`.

## Background: the stealth animation fix

WotLK scales movement-animation playback rate by the unit's real speed. In
`CGUnit_C`'s animation update at `0x7385C0`:

```
0x7388B4  fldz
0x7388B6  fcomp dword ptr [ebp-0x164]      ; sequence.movespeed
0x7388BC  fnstsw ax
0x7388BE  test  ah, 0x44
0x7388C1  jnp   0x73898F                   ; movespeed == 0 -> skip scaling
...                                        ; else:
0x7388EE  call  0x987570                   ; GetCurrentSpeed(unit), sprint included
0x738902  fdivp                            ; rate = speed / |movespeed|
0x738904  fstp  dword ptr [ebp-0x14]
...
0x73898F  (carry on; rate stays the 1.0 set by fld1 @ 0x73883C)
```

`M2Sequence.movespeed` is "the speed this animation was authored for", and `0`
is the engine's "don't scale" sentinel. Blizzard populated it inconsistently,
which is why stock 3.3.5 glides in some cases and not others — audited across
all 20 playable models:

| Animation | Models with a non-zero movespeed (i.e. that scale) |
|---|---|
| Run | 19/20 — HumanFemale is 0.0 |
| StealthRun | 6/20 |
| StealthWalk | 13/20 |
| Walkbackwards | 1/20 (DraeneiFemale) |
| Swim | 3/20 |
| ShuffleLeft / ShuffleRight | 0/20 |
| Sprint | 3/20 |

## What this does

Replaces the conditional jump at `0x7388C1` with a jump to `GateHook` in the
DLL, which forces the skip path for the listed animation IDs and otherwise
reproduces the original `jnp` exactly:

```
0F 8B C8 00 00 00   jnp 0x73898F   ->   E9 <rel32> 90   jmp GateHook ; nop
```

```asm
GateHook:
    pushfd                          ; PF from `test ah, 0x44` must survive
    push eax
    mov  eax, esi                   ; animation id
    cmp  eax, 1024
    jae  passthru
    cmp  byte ptr g_lock[eax], 0
    jne  locked
passthru:
    pop eax / popfd
    jnp  <skip>                     ; exactly the original instruction
    push <cont> / ret
locked:
    pop eax / popfd
    push <skip> / ret               ; force rate 1.0
```

`esi` holds the animation ID at that point — the client itself relies on that at
`0x7388C7` (`mov eax, esi` feeding the is-movement-animation lookup), and `esi`
is callee-saved across the intervening calls.

Forcing the skip is exactly the path stock models with `movespeed == 0` already
take, so nothing novel happens downstream — the skipped block only computes an
inter-animation phase offset that is already left at `0` on that path.

Both jump destinations are read out of the original instruction rather than
hardcoded, and the original bytes are verified first: on any mismatch nothing is
written and a message box explains why, so it cannot corrupt a different client
build.

## Build

```bash
tools\animspeedfix\build.bat test
```

Needs the VS x86 C++ toolset. Output lands in `out\`. The `test` argument runs
`AnimSpeedSelfTest.exe`, which rebuilds the gate in scratch memory, proves the
harness reproduces both original outcomes, applies the real patch code, then
checks that listed animations are forced onto the skip path *and* that
unlisted ones (Run, Walk, Sprint, Swim, StealthStand, out-of-range IDs) still
reach the scaling path, plus SwingGuard's suppression rules and the stack
balance of its skip path. 26 checks, all must pass.

(The `'vswhere.exe' is not recognized` line during the build comes from
Microsoft's own `vcvars32.bat`, not from this script. It is harmless.)

## Install

Copy **`dinput8.dll`** and **`AnimSpeedFix.ini`** next to `Wow.exe`. That's it —
no injector, no launcher changes. Delete `dinput8.dll` to uninstall.

**Close the client before copying**, or the file will be locked.

`dinput8.dll` is the same code as `AnimSpeedFix.dll` plus a proxy export. It
works because `Wow.exe` statically imports `DirectInput8Create` from
`DINPUT8.dll`, and `dinput8` is **not** a KnownDLL, so a copy in the application
directory is loaded ahead of the system one. Being a static import, it loads
during process init — before any game code runs. The single export is forwarded
to the real `%SystemRoot%\SysWOW64\dinput8.dll`, resolved lazily on first call so
`LoadLibrary` never runs under the loader lock.

`DivxDecoder.dll` would also have worked (likewise statically imported and
local), but that would mean renaming a shipped file; `dinput8.dll` is purely
additive. The Centurion launcher only manages addons and MPQ patches — it does
not touch root DLLs — so neither file is clobbered on update.

### Alternative: manual injection

If you would rather inject, use `AnimSpeedFix.dll` (identical minus the proxy
export) with any injector. `AnimSpeedLoader.exe` is included for that: put it
plus `AnimSpeedFix.dll` and the ini next to `Wow.exe` and run the loader instead
of the client. Don't use both methods at once.

### Verifying it applied

With the client running, read the patched byte out of live memory:

```bash
python tools\animspeedfix\checkpatch.py
```

`0x7388C1` should read `E9 <rel32> 90` jumping into the DLL. Unpatched it reads
`0F 8B C8 00 00 00`. The script also distinguishes a hook jump from a
jump straight to `0x73898F` (which would mean *everything* is locked).

## Settings

`AnimSpeedFix.ini`:

```ini
[AnimSpeed]
Enabled = 1
LockAnimations = 119, 223     ; StealthWalk, StealthRun
```

Only animations the engine treats as movement animations can scale at all, so
only these are worth listing — anything else in the list is inert:

```
  4 Walk          5 Run          11 ShuffleLeft    12 ShuffleRight
 13 Walkbackwards 37 JumpStart   38 Jump           39 JumpEnd
 42 Swim          43 SwimLeft    44 SwimRight      45 SwimBackwards
119 StealthWalk  135 Fly        143 Sprint        187 JumpLandRun
223 StealthRun
```

To go back to locking everything, list all 17. To disable entirely, set
`Enabled = 0` or delete `dinput8.dll`.

## Distributing it

The Centurion launcher's patch system already does everything needed — per-patch
download and version polling.

**1. Package** (writes to `dist\`):

```bash
powershell -ExecutionPolicy Bypass -File tools\animspeedfix\package.ps1
```

Produces `client-tweaks.zip` (containing just `dinput8.dll` at the archive root)
and `client-tweaks.version`. Re-running auto-increments the version; pass
`-Version 1.00003` to set it explicitly.

`AnimSpeedFix.ini` is deliberately **not** shipped — the defaults are compiled
in, so including it would clobber a player's local edits on every patch update.
Anyone who wants to tune it drops their own copy alongside, and the launcher
leaves it alone.

**2. Upload** both files to the launcher update URL
(`launcherUpdateUrl`, default `https://centurionpvp.com/downloads/`). The
launcher fetches `<name>.version` to decide whether to re-download `<name>.zip`.

**3. Ship a launcher build.** The `FileMap` entry lives in
`src/common/constants.ts` and is compiled into the app, so a new patch needs a
launcher release — it can't be added server-side alone. The entry is
already added:

```ts
['client-tweaks']: {
    extractPath: '.'      // client root, next to Wow.exe; no `optional` = mandatory
}
```

The entry has no `optional` flag, so it installs for every player rather than
sitting behind a checkbox. (When a patch *is* optional and gets unchecked,
`updater.ts` moves every file it recorded in `file-cache.json` out of
the client and into `.launcher/cached/client-tweaks/<version>/`, so
`dinput8.dll` genuinely leaves the client folder and the mod is off. Re-checking
restores from that cache without re-downloading.)

Renamed from `stealth-glide` in launcher 1.1.8, once it covered more than
stealth. The old key is still served so 1.1.7 clients keep working; both
deliver the same `dinput8.dll`. Retire `stealth-glide.*` from the server once
1.1.7 is gone.

### Things to watch

- **Antivirus.** This is an unsigned DLL that rewrites another process's code,
  under a system-sounding filename. That is a textbook heuristic hit — expect
  some players to see Defender or a third-party scanner quarantine it. Test
  against Defender before announcing, and consider code-signing if you have a
  certificate. Have a "if it vanished, it was your AV" line ready for support.
- **Toggling while the game is open fails.** A running client holds
  `dinput8.dll` open, so the launcher's move will error out. The launcher
  verifies before launching, so the normal flow is fine.
- **Failures are quiet by design.** If the patch can't apply, the reason is
  appended to `AnimSpeedFix.log` next to the DLL and the client runs unmodified.
  Set `ShowErrors = 1` in the ini to get a pop-up as well — useful when you're
  testing, wrong for players.
- **Build coupling.** The patch address is specific to Wow.exe 3.3.5a 12340. If
  you ever ship a different `Wow.exe`, re-verify: the DLL will refuse and log
  rather than misbehave, but the feature silently stops working.

### Without the launcher

`dist\client-tweaks.zip` is a plain archive — players can unzip `dinput8.dll`
next to `Wow.exe` by hand, and delete it to uninstall. No installer, no registry,
no other files touched.

## Combat / packet tracer

Off by default. Built into the same DLL because you can only have one
`dinput8.dll`, and because tracing is independent of the gameplay patch — set
`[AnimSpeed] Enabled = 0` and `[Trace] Enabled = 1` to trace with this DLL
otherwise inert.

It exists to answer one question: **was the attack packet handled late, or was
the swing animation held?**

Two read-only hooks on a shared high-resolution clock:

| record | hook | notes |
|---|---|---|
| `OP` — every dispatched opcode | `0x632013` | `esi` = opcode, `edi` = NetClient. Handler table is `[net + opcode*4 + 0x53C]`, param at `+0x19B8`; registered via `0x631FA0` (wrapped by `0x6B0B80`). |
| `ANIM` — every `PlayAnimation` | `0x832AB0` | logs `this`, animation id, and the rate argument |

**No floating point in the hook path.** `PlayAnimation` is called from x87-heavy
code and `pushad`/`pushfd` don't save FPU state, so timestamps are raw QPC ticks
and the rate is logged as its raw bit pattern. `tracefmt.py` converts both.

```ini
[Trace]
Enabled    = 1
Opcodes    = 1
Animations = 1
File       = AnimTrace.csv
```

Then:

```bash
python tracefmt.py AnimTrace.csv
```

which resolves opcode names from the server's `Opcodes.h`, animation names from
`AnimationData.dbc`, and reports for each `SMSG_ATTACKERSTATEUPDATE (0x14A)` how
long until the swing animation started and whether a proc opcode landed in the
same dispatch batch:

```
t(ms)        swing after    same-batch opcodes                note
     100.000 3.0 ms
    2600.000 2600.0 ms      SMSG_SPELLNONMELEEDAMAGELOG       PROC IN SAME BATCH
```

Reading it:

- delays small and uniform → the packet was handled on time; the visible stutter
  is downstream (animation slot occupancy / priority / blending)
- delays spike to roughly a swing timer *only* when a proc shares the batch →
  the same-frame hypothesis holds

Records flush every 32 KB, so exit the client normally to get the tail; killing
it loses the last block.

## Related tooling

`tools\mpqpy\anim_movespeed.py` reports the `movespeed` of every movement
animation in a model, straight out of the MPQs — useful for seeing which
animations stock 3.3.5 would have scaled:

```bash
python tools\mpqpy\anim_movespeed.py report --mpq "Character\NightElf\Male\NightElfMale.m2"
```

Note it applies real 3.3.5 archive priority
(`patch-Z > … > patch-2 > patch > lichking > expansion > common-2 > common`);
sorting archive names alphabetically gets this backwards, because `patch-*` sorts
before `patch.MPQ`.
