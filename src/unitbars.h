// unitbars.h - where every unit's head is on screen, for an addon's health bars.
//
// WoW 3.3.5 gives an addon no way to place a frame over a unit in the world:
// the only frames that follow units are nameplates, and those exist only while
// the player has them switched on. CENTURION_Rift draws League-style health bars
// over minions, monsters and turrets, so the client hands Lua the positions:
//
//   n = CenturionUnitBarsScan()
//       Walks the visible units once and keeps up to 96 of them: alive,
//       selectable, creatures and other players, within [UnitBars] Range yards of the player,
//       and in front of the camera. Returns how many.
//   fx, fy, health, reaction, scale, guid, isPlayer = CenturionUnitBar(i)   (1 <= i <= n)
//       fx, fy   where the unit's name sits (its name attachment), as a
//                fraction of WorldFrame (0,0 = bottom left), so
//                x = fx * WorldFrame:GetWidth()
//       scale    one yard there, as a fraction of WorldFrame's height - the
//                bar is sized by it, so it shrinks with distance as the name does
//       health   current / max, 0..1
//       reaction the player's reaction to it, as UnitReaction() reports it (1..8)
//       guid     the unit's GUID as UnitGUID() writes it ("0xF130...")
//       isPlayer 1 for another player, 0 for a creature
//   CenturionTargetGUID(guid)
//       targets that unit, as clicking it in the world would (TargetUnit takes
//       only a unit token or a name, and minions share names)
//
// The functions are registered with the world UI's own (through the name tag's
// world-Lua hook, nametag.cpp), so they exist from the first frame of FrameXML.
#pragma once

// Read [UnitBars] from AnimSpeedFix.ini (dir = folder of this DLL, with the
// trailing backslash). Safe with no ini present - ON, 80 yards.
void UnitBars_LoadSettings(const char* dir);

// Verify the client signatures and write the two Lua thunks. Patches bytes
// only; nothing here calls into the game.
void UnitBars_Install();

// Called each time the world UI registers its Lua functions (every world load
// and /reload, before FrameXML is read).
void UnitBars_WorldLuaReady();
