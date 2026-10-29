// spectate.h - a camera that follows a unit without turning with it.
//
// The stock way to watch someone is Bind Sight / Mind Vision / farsight: the
// camera's target becomes their unit, and the 3.3.5 camera keeps its yaw
// relative to its target's facing, so it swings round every time they turn
// (Eyes of the Beast, AzerothCore's arena spectator). Wow.exe already carries
// the camera that does not: Blizzard's tournament "commentator" camera
// (CommentatorFrame.cpp). In commentator mode the world frame lets go of the
// camera's target and the commentator object (0x00ACE4A8) places the camera
// every frame - with a unit to follow, at a distance behind it along the
// commentator's OWN yaw and pitch, which only the mouse turns. Commentator mode
// normally needs player flags only a Blizzard tournament realm set; this module
// answers "yes" for the local player while a follow is on, and sets the
// commentator's follow target directly.
//
//   CenturionSpectate("0xGUID" [, distance])  follow that unit (any unit the
//                                              client has: a replay's actors too)
//   CenturionSpectate("off")                  back to the normal camera
//   CenturionSpectate("zoom", yards)          closer (negative) or further
//   guid, present = CenturionSpectate("?")    what is followed (nil when off),
//                                              and whether that unit is here now
//
// While following: the mouse turns the camera round the unit, W / S bring it
// closer or further, the player's own character stays where it is. See
// docs/SPECTATE_CAMERA_RE.md for the addresses.
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
