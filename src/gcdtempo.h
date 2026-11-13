// gcdtempo.h - Violet Hold "Boon of Tempo": an aura-gated cut to the client's
// own global cooldown, matching the server's VioletHoldBoons::
// ApplyGlobalCooldownReduction.
//
// See gcdtempo.cpp for the reversing notes.
#pragma once

// Read [GcdTempo] from AnimSpeedFix.ini (dir = folder of this DLL, with the
// trailing backslash). Safe with no ini present - compiled-in defaults.
void GcdTempo_LoadSettings(const char* dir);

// Verify the client signatures, then install the detour in the client's GCD
// function. Fails safe (logs + disables) on any signature mismatch.
void GcdTempo_Install();
