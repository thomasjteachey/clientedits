// spectate.h - the normal camera, centred on another unit, without turning with it.
//
// The stock way to watch someone is Bind Sight / Mind Vision / farsight: the
// camera's target becomes their unit, and the 3.3.5 camera adds its target's
// facing to its yaw, so it swings round every time they turn (Eyes of the
// Beast, AzerothCore's arena spectator). The camera has a lock for exactly that
// (camera+0xAC, held by mouse look): with it held the facing stays out. This
// module puts the normal camera on the unit (CGWorldFrame::SetCameraTarget, as
// farsight does) and holds one lock - zoom, left/right drag and everything else
// are the camera's own controls - and hands the camera back to the player when
// it ends. (The commentator camera, tried first, took the controls over.)
//
//   CenturionSpectate("0xGUID")               follow that unit (any unit the
//                                              client has: a replay's actors too)
//   CenturionSpectate("off")                  back to the player
//   guid, present, moved = CenturionSpectate("?")
//                                              what is followed (nil when off),
//                                              whether that unit is here now, and
//                                              1 once after a movement key ended it
//   CenturionSpectate("arm" | "disarm")       right-click on another player is
//                                              "spectate them" (not interact)
//   guid = CenturionSpectate("click")         the player last right-clicked while
//                                              armed, once, else nil
//   v = CenturionSpectate("version")          5 (older DLLs answer nil: they took
//                                              any other word for "off"; 3 has
//                                              the focus, 4 the damage numbers,
//                                              5 the target)
//   guid = CenturionSpectate("target", "0xGUID" | "none" | "off")
//                                              your target set to that unit (or
//                                              none), or the one before back;
//                                              returns the target now (v5)
//   guid = CenturionSpectate("focus", "0xGUID" | "off")
//                                              make that unit the focus (FocusUnit
//                                              is protected in Lua), or put back
//                                              the focus there before; returns the
//                                              focus now
//
// While following, the numbers over units are drawn for the watched unit's
// hits and heals (and its pets') instead of yours, and Blizzard_CombatText
// shows what it takes (docs/DAMAGE_TEXT_RE.md).
//
// A movement key (W/A/S/D, strafe, autorun, both mouse buttons) ends the follow
// and moves the player's own character as usual. The camera stays on the unit
// until the player stands within 4 yards of it (the replay server puts them
// there) or 1.5 s pass, so it does not jump back to where the player was first.
// See docs/SPECTATE_CAMERA_RE.md for the addresses.
#pragma once

// Read [Spectate] from AnimSpeedFix.ini (dir = folder of this DLL, with the
// trailing backslash). Safe with no ini present - ON.
void Spectate_LoadSettings(const char* dir);

// Verify the client signatures, write the Lua thunk and the commentator-check
// hook. Patches bytes only; nothing here calls into the game.
void Spectate_Install();

// Called each time the world UI registers its Lua functions (every world load
// and /reload, before FrameXML is read). Ends any follow: a new world has none
// of the old one's units.
void Spectate_WorldLuaReady();
