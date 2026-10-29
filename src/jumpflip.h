// jumpflip.h - Acrobatics: the night elf forward flip for every race, for units
// that carry the Acrobatics aura (94301, from the "Enchant Shirt - Acrobatics"
// shirt enchant 3901).
//
// The models do the animation work. tools\mpqpy\add_jump_flip.py appends a flip
// to every non-night-elf character model as an extra JumpStart (37) variation
// with frequency 0. The client rolls JumpStart variations with rand() against
// those weights (0x826E60), and a weight of 0 never wins - so without this
// module every model jumps exactly like stock. CM2Model::PlayAnimation (0x832AB0)
// still plays a zero-weight variation when asked for its chain position
// explicitly (0x8260C0 walks variationNext that many steps; a position past the
// end falls back to the normal roll, so an unpatched model is safe).
//
// This module hooks that request: for a JumpStart the engine asked to roll
// (variation -1) on a unit that has the aura and whose model carries a
// zero-weight JumpStart variation, it rolls the night elf odds itself (25% male,
// 50% female by default) and on a hit asks for that variation.
//
// Night elf models get a zero-weight DOUBLE flip (their own flip plus one more
// turn). A night elf with the aura therefore double-flips on a hit, and on a miss
// is asked for variation 0 - a plain jump - so the stock single flip (which
// already rolls at those odds) does not also appear: Acrobatics turns their flips
// into double flips. Everyone without the aura is left alone, night elves
// included (stock single flip).
//
// Druid forms flip too (2026-10-28): the cat, bear, moonkin, travel and tree of
// life models carry the same zero-weight flip (both the stock models in patch-4
// and the HD Creatures pack's own in patch-F). Nothing here is form-specific -
// while shapeshifted the form model is the unit's model - except that a MOUNTED
// unit is never flipped: Tiger.m2 is the HD-off travel form and also a Beast
// Rider mount, and the rider would stay put while the tiger spun.
//
// Moonkin leap (2026-10-28): while a unit in Moonkin Form is in the air from a
// launch faster than 20 yd/s up (Feral Charge - Moonkin sends 24.1; a jump is 8),
// its JumpStart / Jump / Fall are played as Fly (135) - wings flapping - until it
// lands. The HD moonkin has Blizzard's Fly; the stock one got one in patch-4.
//
// Each client rolls for itself, exactly like the stock night elf flip: two
// people watching the same jump can see different results.
#pragma once
#define WIN32_LEAN_AND_MEAN
#include <windows.h>

// Read [JumpFlip] from AnimSpeedFix.ini (dir = folder of this DLL, with the
// trailing backslash). Safe with no ini: ON, aura 94301, 25% / 50%.
void JumpFlip_LoadSettings(const char* dir);

// Verify the client functions it calls. Must run before the PlayAnimation hook
// is placed. Fails safe: any mismatch logs and disables the feature.
void JumpFlip_Install();

// True when the PlayAnimation hook (trace.h AnimHook) must be installed for us.
bool JumpFlip_WantsAnimHook();

// Called from AnimHook at PlayAnimation entry, before the prologue.
// args points at the stack arguments: args[1] = animation id, args[2] =
// variation (-1 = let the engine roll). May rewrite args[2]. No floating point.
extern "C" void __cdecl JumpFlipAnim(void* model, DWORD* args);
