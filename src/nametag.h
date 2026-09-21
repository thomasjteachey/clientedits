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
// The word goes in as an extra line of the unit's own name-tag text, picked
// from the hidden aura the realm hands out (92010 / 92011 / 92012). See
// docs/NAME_TAG_RENDER_RE.md.
//
// Players who do not want it switch it off with the `centurionNameTags` CVar -
// `/console centurionNameTags 0`, or the checkbox under Interface > AddOns >
// Centurion (FrameXML CenturionNameTags.lua). The choice is written back to
// [NameTag] DefaultOn, so it survives a restart.
#pragma once

// Read [NameTag] from AnimSpeedFix.ini (dir = folder of this DLL, with the
// trailing backslash). Safe with no ini present - the marker is ON, the probe
// log OFF.
void NameTag_LoadSettings(const char* dir);

// Verify the client signatures, register the CVar, install the probe.
// [NameTag] Enabled=0 leaves the client untouched. Fails safe: any mismatch
// logs and disables.
void NameTag_Install();

// Register the centurionNameTags CVar if it is not yet. Called from the char
// select screen's Lua registration (gluebridge.cpp), so the CVar exists before
// the interface loads and its options checkbox can find it; the first name tag
// is the fallback. Never from DllMain.
void NameTag_RegisterCVar();

// Whether the marker should be drawn at all: the CVar, or the ini default
// before the CVar exists. Read by the draw path once there is one.
bool NameTag_Enabled();
