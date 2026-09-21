// nametag.h - a mode marker above a player's name in the world.
//
// Centurion characters come in three kinds - world, tournament and bot - and
// nothing in the 3D world says which one you are looking at. The character
// select screen shows it (blue name for a world character, an orange
// "Tournament" where the zone would be), but in the world everybody looks the
// same.
//
// The marker has to come from the client: the name over a unit is drawn by the
// engine, not by a frame, so no addon can reach it.
//
// STAGE 1 (this file today): a read-only probe on the per-unit name tag, to
// find the two things static reversing could not - where the glyphs are drawn,
// and where the screen position comes from. It draws nothing. See
// docs/NAME_TAG_RENDER_RE.md for what is known and what the probe is looking
// for.
//
// Players who do not want it must be able to switch it off, so the toggle is
// built in from the start: `centurionNameTags` is registered as a real client
// CVar, which means `/console centurionNameTags 0`, persistence in Config.WTF
// and an interface checkbox later, all for free.
#pragma once

// Read [NameTag] from AnimSpeedFix.ini (dir = folder of this DLL, with the
// trailing backslash). Safe with no ini present - the feature stays OFF.
void NameTag_LoadSettings(const char* dir);

// Verify the client signatures, register the CVar, install the probe.
// No-op unless [NameTag] Enabled=1. Fails safe: any mismatch logs and disables.
void NameTag_Install();

// Whether the marker should be drawn at all: the CVar, or the ini default
// before the CVar exists. Read by the draw path once there is one.
bool NameTag_Enabled();
