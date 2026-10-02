// sndguard.h - keeps a NaN sound position from crashing the client.
//
// FMOD's 3D channel update turns a sound's listener-relative position into a
// speaker-pan angle with an integer atan2. A NaN/inf position converts to
// 0x80000000, wraps to 0 under a shift, and the angle code then divides by zero
// (ERROR #132, INT_DIVIDE_BY_ZERO at 0x0094BF6B). This module makes that one
// channel pan centre instead. See sndguard.cpp and docs\FMOD_3D_PAN_RE.md.
#pragma once

// Read [SoundGuard] from AnimSpeedFix.ini (dir = folder of this DLL, with the
// trailing backslash). No ini = defaults: ON.
void SndGuard_LoadSettings(const char* dir);

// Verify the client bytes, then detour both divides. Fails safe (logs and
// leaves the client untouched) on any signature mismatch.
void SndGuard_Install();
