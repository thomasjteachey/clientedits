# FMOD 3D pan crash (INT_DIVIDE_BY_ZERO at 0x94BF6B)

Wow.exe 3.3.5a 12340. Investigated 2026-10-01 from a player crash in Arathi
Basin (`Errors\2026-10-01 21.26.26 Crash.dmp`, player Stonedsoul).

## The function (verified)

`0x94BD50` — FMOD Ex channel 3D update (`__thiscall`, `esi = ecx = channel`).
No direct callers; it is slot 1 of two FMOD class vtables:

| vtable | sits after string |
|---|---|
| `0xA98E30` (ctor store at `0x933EC1`) | `..\..\src\fmod_codec_wav_riff.cpp` |
| `0xA99EE0` (ctor store at `0x94BD40`) | `..\src\fmod_sample_openal.cpp` |

Layout used:

- `[esi+0x64]` mode flags: `0x10` = 3D (else returns 0), `0x40000` = head-relative.
- `[esi+0x54]` → real channel; its 3D position x/y/z at `+0x120/+0x124/+0x128`.
- `[esi+4]` → system; listener position `+0x79C8/+0x79CC/+0x79D0`, listener
  axes around `+0x7A10..+0x7A30`; `[sys+0x7B88] == 1` gates the speaker-pan path;
  `[sys+0x14] & 2` = left-handed (negates z).

It subtracts listener from source, projects onto the listener axes, then
computes a speaker-pan angle with a **fixed-point atan2**:

```
0x94BF1A  fld [0x9E2A4C]        ; 256.0
0x94BF24  call 0x88B9C0         ; ftol (cvttsd2si) -> esi
0x94BF2D  call 0x88B9C0         ;                  -> edi
0x94BF32  if (esi==0 && edi==0) angle = 0
0x94BF44  edi <<= 10 ; ecx = |edi| + 1 ; esi <<= 10
0x94BF5A  esi >= 0: divisor = (ecx + esi) >> 10 ; idiv @ 0x94BF6B
0x94BF81  esi <  0: divisor = (ecx - esi) >> 10 ; idiv @ 0x94BF92
0x94BFAC  * 180 / pi-ish, wrapped to [0,360) -> [ebp-4] -> fild @ 0x94BFDD
          -> stored to [channel+0x98]
```

Constants: `0x324` = 804 ≈ (π/4)·1024, `0x96C` = 2412 ≈ (3π/4)·1024.

## Why it divides by zero

`cvttsd2si` returns `0x80000000` for NaN/±inf/out-of-range. That passes the
"both zero" test, then `<< 10` wraps it to 0, so the divisor becomes
`(1 + 0) >> 10 = 0`. Any finite position closer than ~8 million yards is safe.
**The crash therefore means a 3D sound source (or the listener) had a NaN/inf
position.**

Crash-dump evidence: `ESI = EDI = 0` after the shifts; `[ebp-0x34..-0x2C]` (source
x/y/z) and the real channel's `+0x11C..+0x128` are all `0xFFC00000` (the default
NaN from 0/0, inf-inf, sqrt(-1)); listener axes were normal (0.262 / -0.965).
The faulting thread is FMOD's mixer thread (`0x47F2E1` → `0x87C50A` → `0x8D146F`
→ `0x8E0C4A`), so the stack does not show which game object owned the emitter,
and the minidump does not capture the owner's memory.

## Guard — `src/sndguard.cpp` (client-tweaks 1.00018)

Both divides are the 6-byte sequence `8B 4D FC 99 F7 F9`
(`mov ecx,[ebp-4]; cdq; idiv ecx`) at `0x94BF67` and `0x94BF8E` — room for a
5-byte jmp. The stubs reload the divisor and, when it is `<= 0`, do
`[ebp-4] = 0; jmp 0x94BFDD` — the same exit the stock "no direction" path takes
at `0x94BF3C`, with the same x87 state. `<= 0` rather than `== 0` because a
wrapped sum can give -1, and `0x80000000 / -1` faults too. A legitimate divisor
is always >= 1, so real sounds never take the guard. The NaN channel pans
centre; the NaN in FMOD's float volume math does not fault.

Signature-checked at install (both sites, the `DB 45 FC` tail, and the stock
zero path at `0x94BF3C`); any mismatch logs and leaves the client untouched.
Logs the first 3 trips as `[sndguard pid…] blocked a divide by zero…`.
`[SoundGuard] Enabled=0` in AnimSpeedFix.ini turns it off.
