// playercollide.h - buff-gated client-side player-vs-player collision.
// See playercollide.cpp and tools\clientre\PLAYER_COLLISION_DESIGN.md.
#pragma once

// Read [PlayerCollide] from AnimSpeedFix.ini (dir = folder of this DLL, with the
// trailing backslash). Safe to call with no ini present - feature stays OFF.
void PlayerCollide_LoadSettings(const char* dir);

// Verify every client signature, then install the physics-tick detour. No-op
// unless [PlayerCollide] Enabled=1 and SpellId!=0. Fails safe (logs + disables)
// on any signature mismatch.
void PlayerCollide_Install();
