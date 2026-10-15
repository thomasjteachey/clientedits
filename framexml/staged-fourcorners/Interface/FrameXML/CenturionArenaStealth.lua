-- CENTURION: an arena opponent in stealth reads "Stealthed", not "Dead".
--
-- The stock enemy frames (Blizzard_ArenaUI, patch-enUS-2) label a health bar
-- at zero with DEAD: Blizzard_ArenaUI.xml gives each one
-- SetTextStatusBarTextZeroText(healthBar, DEAD). An opponent who stealths
-- goes "unseen" (ARENA_OPPONENT_UPDATE): the client drops the unit, its
-- health reads 0, and the frame said "Dead" for as long as they stayed in
-- stealth. An arena's visibility range covers the whole map, so "unseen" there
-- means stealth or invisibility.
--
-- So the zero text follows the opponent: "Stealthed" while unseen, DEAD again
-- once they are seen (or the slot is cleared). The stock handler is left alone
-- and runs first; this is a secure post-hook on it, so nothing the secure unit
-- buttons do is tainted.

CENTURION_ARENA_STEALTHED = "Stealthed";

local function CenturionArenaStealth_OnEvent(self, event, unit, state)
	if ( event ~= "ARENA_OPPONENT_UPDATE" or unit ~= self.unit or not self.healthbar ) then
		return;
	end

	if ( state == "unseen" ) then
		SetTextStatusBarTextZeroText(self.healthbar, CENTURION_ARENA_STEALTHED);
	elseif ( state == "seen" or state == "destroyed" or state == "cleared" ) then
		SetTextStatusBarTextZeroText(self.healthbar, DEAD);
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
local loader = CreateFrame("Frame");
loader:RegisterEvent("ADDON_LOADED");
loader:SetScript("OnEvent", function(self, event, addon)
	if ( addon == "Blizzard_ArenaUI" ) then
		CenturionArenaStealth_Hook();
		self:UnregisterEvent("ADDON_LOADED");
	end
end);
if ( IsAddOnLoaded("Blizzard_ArenaUI") ) then
	CenturionArenaStealth_Hook();
	loader:UnregisterEvent("ADDON_LOADED");
end
