-- CENTURION: the engine now and then fires UNIT_AURA with no unit, and the stock
-- Dungeon Finder cooldown handler (LFDFrame.lua:945) strsub()s that unit blindly:
--   bad argument #1 to 'strsub' (string expected, got nil)
-- LFDFrame.xml binds the handler with <OnEvent function=...>, which captures the
-- function when the frame loads, so redefining the global would change nothing;
-- the frame's script is replaced instead. A unit-less aura event just refreshes.
LFDQueueFrameCooldownFrame:SetScript("OnEvent", function(self, event, unit, ...)
	if ( event == "UNIT_AURA" and type(unit) ~= "string" ) then
		return LFDQueueFrameRandomCooldownFrame_Update();
	end
	return LFDQueueFrameRandomCooldownFrame_OnEvent(self, event, unit, ...);
end);
