-- CENTURION: an arena opponent in stealth reads "Stealthed", not "Dead".
--
-- The stock enemy frames (Blizzard_ArenaUI, patch-enUS-2) label a health bar
-- at zero with DEAD: Blizzard_ArenaUI.xml gives each one
-- SetTextStatusBarTextZeroText(healthBar, DEAD). An opponent who stealths
-- leaves the client's object manager, its health reads 0, and the frame said
-- "Dead" for as long as they stayed in stealth.
--
-- How the client hears of it (Wow.exe 12340, docs/ARENA_UNITS_RE.md):
--   "destroyed"  SMSG_ARENA_UNIT_DESTROYED (handler 0x0054AD10). The server
--                sends it in Object::DestroyForPlayer, ahead of the
--                SMSG_DESTROY_OBJECT, whenever an arena unit drops out of a
--                player's sight - which is what stealth does. The stock
--                handler treats it like "seen" and redraws the frame from a
--                unit that is no longer there.
--   "unseen"     0x0054B840, a slot assigned to a player not in the object
--                manager (already gone when the slot was handed out).
-- Both also happen to an opponent who is dead (and kept out of sight) or who
-- has left. So the frame remembers whether the opponent was last seen alive:
-- alive and gone is "Stealthed", greyed the way the stock "unseen" greys it;
-- dead stays DEAD. Seen again, or the slot cleared, puts DEAD back.
--
-- The stock handler is left alone and runs first; this is a secure post-hook,
-- and FrameXML is secure code, so nothing the unit buttons do is tainted.

CENTURION_ARENA_STEALTHED = "Stealthed";

-- [arenaN] = true when the opponent was dead the last time the client saw them.
local lastDead = {};

local function Remember(unit)
	if ( UnitExists(unit) ) then
		lastDead[unit] = UnitIsDeadOrGhost(unit) and true or nil;
	end
end

local function CenturionArenaStealth_OnEvent(self, event, unit, state)
	if ( event ~= "ARENA_OPPONENT_UPDATE" or unit ~= self.unit or not self.healthbar ) then
		return;
	end

	if ( state == "seen" ) then
		Remember(unit);
		SetTextStatusBarTextZeroText(self.healthbar, DEAD);
	elseif ( state == "cleared" ) then
		lastDead[unit] = nil;
		SetTextStatusBarTextZeroText(self.healthbar, DEAD);
	elseif ( state == "destroyed" or state == "unseen" ) then
		if ( lastDead[unit] ) then
			SetTextStatusBarTextZeroText(self.healthbar, DEAD);
		else
			SetTextStatusBarTextZeroText(self.healthbar, CENTURION_ARENA_STEALTHED);
			ArenaEnemyFrame_Lock(self);
		end
	else
		return;
	end
	TextStatusBar_UpdateTextString(self.healthbar);
end

local hooked = false;
local function CenturionArenaStealth_Hook()
	if ( hooked or type(ArenaEnemyFrame_OnEvent) ~= "function" ) then
		return;
	end
	hooked = true;
	hooksecurefunc("ArenaEnemyFrame_OnEvent", CenturionArenaStealth_OnEvent);
end

-- Blizzard_ArenaUI loads on demand, the first time the player is in an arena.
-- Health is watched from the start, so a death before that is not missed.
local driver = CreateFrame("Frame");
driver:RegisterEvent("ADDON_LOADED");
driver:RegisterEvent("UNIT_HEALTH");
driver:RegisterEvent("PLAYER_ENTERING_WORLD");
driver:SetScript("OnEvent", function(self, event, arg1)
	if ( event == "UNIT_HEALTH" ) then
		if ( type(arg1) == "string" and strmatch(arg1, "^arena%d$") ) then
			Remember(arg1);
		end
	elseif ( event == "PLAYER_ENTERING_WORLD" ) then
		lastDead = {};
	elseif ( arg1 == "Blizzard_ArenaUI" ) then
		CenturionArenaStealth_Hook();
	end
end);
if ( IsAddOnLoaded("Blizzard_ArenaUI") ) then
	CenturionArenaStealth_Hook();
end
