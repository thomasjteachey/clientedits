// rulerelax.h - aura-gated relaxation of client-side spell usability rules.
//
// The client decides some cast rules entirely on its own, out of its copy of
// Spell.dbc, so a server-side "this set bonus lets you do X" is invisible to it
// and the button never lights up. DBC edits can relax those rules, but DBC is
// global and auras are per-player, so a DBC edit relaxes the rule for everyone
// and also destroys the tooltip line and the grey-out that other players should
// keep. This module closes that gap: the rule stays intact in the data, and is
// waived at runtime only for a player carrying a configured aura.
//
// See rulerelax.cpp for the reversing notes and the open measurement.
#pragma once

// Read [RuleRelax] from AnimSpeedFix.ini (dir = folder of this DLL, with the
// trailing backslash). Safe to call with no ini present - feature stays OFF.
void RuleRelax_LoadSettings(const char* dir);

// Verify the client signature, then install the usability-predicate detour.
// No-op unless [RuleRelax] Enabled=1 and at least one rule aura is set.
// Fails safe (logs + disables) on any signature mismatch.
void RuleRelax_Install();
