// reactive.h - forget a reactive ability's alert once the spell is unlearned.
//
// The client keeps its own list of "trigger-activate" spells (SPELL_ATTR4 0x200:
// Execute, Overpower, Revenge, Hammer of Wrath, Riposte, Counterattack, Mongoose
// Bite, Victory Rush, Kill Shot, ...) and raises COMBAT_TEXT_UPDATE SPELL_ACTIVE
// ("Hammer of Wrath!") whenever one of them becomes usable. A spell joins that
// list when it is learned; SMSG_REMOVED_SPELL never takes it off. Only a fresh
// spellbook at world entry clears it. The realm's All-Star arena unlearns one
// class and teaches the next in the same session, so every reactive ability of
// every earlier life kept announcing itself (a druid told to Execute and
// Overpower, a rogue told to Hammer of Wrath). This module prunes the list
// after each unlearn. See reactive.cpp for the addresses.
#pragma once

// Read [ReactiveList] from AnimSpeedFix.ini (dir = folder of this DLL, with the
// trailing backslash). No ini = defaults: ON.
void Reactive_LoadSettings(const char* dir);

// Verify the client bytes, then detour RemoveKnownSpell. Fails safe (logs and
// leaves the client untouched) on any signature mismatch.
void Reactive_Install();
