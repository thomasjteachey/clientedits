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

## Jump flip (Acrobatics, `src\jumpflip.cpp`)

The night elf forward flip for every race, for units with the Acrobatics aura
(spell 94301, from shirt enchant 3901 / "Enchant Shirt - Acrobatics" 94302).

**How the stock flip works.** NightElfMale/Female.m2 carry two JumpStart (37)
sequences; variation 1 spins the Root bone (key bone 26, the hips) a full turn
forward. Weights (`M2Sequence.frequency`, sums to 32767): male 24576/8191 (25%
flip), female 16383/16383 (50%). Every other playable model has only
variation 0 (blood elves have a non-flip variation 1).

**Verified in Wow.exe 12340 (2026-10-25):**

| address | what |
|---|---|
| `0x832AB0` | `CM2Model::PlayAnimation`, thiscall, `ret 0x1C`: arg0 key bone (-1 = whole model), arg1 animation id, **arg2 variation (-1 = roll)**, arg4 float rate. Unit animation code passes -1 (e.g. `0x71559A`). Not loaded (`model+0x10` bit 0 clear) = request queued with the same args (`0x832AE4`). |
| `0x826350` | resolves the animation id through AnimationData fallbacks (out param). |
| `0x8260C0` | `(m2hdr, animId, variation)` -> sequence index: head from the id hash, then walks `variationNext` (+0x3C) **variation steps**; past the end returns 0xFFFF. |
| `0x832BE3` | 0xFFFF (variation not found) falls back to the head and **the roll**, so asking an unpatched model for a variation it lacks is harmless. |
| `0x826E60` | the roll: `rand()` (`0x88B867`, 0..0x7FFF), walk the chain, pick the first with `r < freq` (freq read as a **dword** at +0x10), else `r -= freq`. **A zero weight never wins.** |
| `model+0x2C` -> `+0x150` | CM2Shared -> in-memory M2 header (offsets fixed up); header +0x1C/+0x20 = sequence count/array, stride 0x40. |
| `unit+0xB4` | the unit's own CM2Model (also used by goeditor). |
| unit descriptors `+0x5C` | UNIT_FIELD_BYTES_0; byte 2 = gender (1 = female). |

**Design.** `tools\mpqpy\add_jump_flip.py` appends a flip to every non-night-elf
model as an extra JumpStart variation **with weight 0**, at the end of the chain
(position 1; position 2 on blood elves). Stock roll never picks it, so models
behave exactly like stock for anyone without the DLL rule. `JumpFlipAnim`
(called from `AnimHook` before SwingGuard) handles `animId == 37 && variation ==
-1`: finds the zero-weight variation's chain position in the model, resolves the
unit owning the model (active player first, then `EnumVisibleObjects`), and if
that unit has aura 94301 rolls 25% (male) / 50% (female) and on a hit rewrites
arg2. One decision per model per 300 ms, so a jump that reaches PlayAnimation
twice never half-flips.

**Night elves get a double flip** (2026-10-25): their models carry a zero-weight
variation at chain position 2 built on their own flip (variation 1) plus one
more turn (`add_jump_flip.py --base-var 1`, donor = the same model; 737 deg male,
716 deg female). With the aura, a hit asks for the double and a miss asks for
variation 0 (plain jump), so the stock single flip does not also appear - race
from UNIT_FIELD_BYTES_0 byte 0 (4 = night elf). Without the aura nothing changes.

**Druid forms flip too** (2026-10-28, client-tweaks 1.00030, patch-4 1.00006,
hd-creatures 1.00008 once published). While shapeshifted, `unit+0xB4` is the form
model, so nothing in the hook is form-specific: the form models just carry the same
zero-weight flip at chain position 1, built from the stock male night elf flip by
`add_jump_flip.py` (all front flips, like the night elf's; checked with
`render_m2_anim.py`). The models, per CreatureDisplayInfo -> CreatureModelData:

| form | HD Creatures off (patch-4) | HD Creatures on (patch-F) |
|---|---|---|
| cat | `DRUIDCAT\DruidCat`, `DruidCatTauren\DruidCatTauren` | same paths, HD copies |
| bear / dire bear | `DruidBear\DruidBear`, `DruidBear\DruidBearTauren` | same paths, HD copies |
| moonkin | `DruidOwlbear\DruidOwlBear`, `DruidOwlbear\DruidOwlBearTauren` | same paths, HD copies |
| travel (918) | `Tiger\Tiger` (patch-4's own copy, with the Beast Rider seat fix) | `DruidTravel\DruidTravel` |
| tree of life (864) | `Ent\Ent` | `DruidEnt\DruidEnt` |

Flight forms have no Root key bone and aquatic form never jumps, so they are left
out. Every non-night-elf race shares the night elf (Alliance) or tauren (Horde) form
models (`Unit::GetModelForForm`). A night elf druid in form gets a single flip on a
hit and a plain jump on a miss (the form model has no stock flip to suppress).

**Mounted units never flip.** `Tiger.m2` and `Ent.m2` are shared with NPCs (harmless:
no aura), and Tiger is also a Beast Rider mount - the hunter rides the pet's own
model, and the rider would stay put while the tiger spun. So the hook leaves any
unit with `UNIT_FIELD_MOUNTDISPLAYID` (descriptor `+0x114`) set to the engine.

**Moonkin leap flap** (2026-10-28, client-tweaks 1.00031, patch-4 1.00007). Feral
Charge - Moonkin (83111) is a knockback with a fixed 2.5 s arc: `EffectJumpDest`
sends speedZ = 0.5 * g * 2.5 = **24.1 yd/s** up, against 7.96 for a jump. In the same
PlayAnimation hook, before the flip, `MoonkinFlap` turns JumpStart / Jump / Fall
(37 / 38 / 40) into **Fly (135)** for a unit in Moonkin Form (`UNIT_FIELD_BYTES_2`
byte 3 = 31, descriptor `+0x1E8`) that is falling and was launched faster than 20
yd/s, until it is seen on the ground again. JumpEnd (39) plays as usual. The HD
moonkin already has Blizzard's Fly (667 ms, two beats); the stock DruidOwlBear /
DruidOwlBearTauren got one from `tools\mpqpy\add_wing_flap.py`: their own Hover
(193) with the ArmL/ArmR shoulders swung +35 +/- 40 deg about the model's forward
axis (converted into the parent bone's frame each key) and the elbows trailing,
8 beats per 2667 ms. A model without Fly is never asked for it.

Verified in Wow.exe 12340 for this (unit's embedded CMovement = `unit+0x788`):

| address | what |
|---|---|
| `0x4F4D40` | movement info reader: flags `+0x10`, flags2 `+0x14`, time `+0x00`, pos `+0x28`, facing `+0x34`, pitch `+0x38`, fall time `+0x3C`; if flags & 0x1000: jump z speed `+0x40`, sin `+0x44`, cos `+0x48`, xy speed `+0x4C` |
| `0x988370` | start falling: CMovement flags `+0x44` \|= 0x1000, fall timer `+0x80` = 0, start z `+0x84`, **launch z speed `+0xB8`** (negative = up) |
| `0x986E10` | current vertical speed = `[+0xB8]` + `[+0x80]` ms * gravity (`0xA32F74` = 19.2911), capped by terminal speed |
| `0x987410` | re-bases a fall mid-air: `+0xB8` := current speed, timer := 0 (so `+0xB8` is the launch speed only near the start; the DLL remembers the leap per unit) |
| `0x987570` | `GetCurrentSpeed` (run/walk/swim/fly speeds at `+0x90..+0xA8`) |

`[JumpFlip]` in AnimSpeedFix.ini also takes `MoonkinFlap` (1) and `MoonkinLaunch`
(20, yd/s upward; strictly faster counts). Any launch that fast while in Moonkin
Form flaps, not only the charge. Centurion Leap (81271) launches at exactly
`Centurion.LeapZSpeed` = 20, so it does not - raise MoonkinLaunch's compiled default
if that config ever goes above 20.

`[JumpFlip]` in AnimSpeedFix.ini: `Enabled` (1), `Aura` (94301), `ChanceMale`
(25), `ChanceFemale` (50), `Debug` (0; 1 logs every JumpStart decision to
AnimSpeedFix.log).

The aura must be **non-passive** (passive auras never reach the client) and is
hidden with AttributesEx 0x12000000, like the name markers.

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
