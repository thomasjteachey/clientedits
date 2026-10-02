-- Centurion chat filters.
--
-- The Violet Hold wave countdown is sent as a raid-boss emote (TrinityCore
-- string 20100, "Next wave in %u...") because that is the only message type the
-- 3.3.5 client draws as large centre-screen text: RaidBossEmoteFrame registers
-- CHAT_MSG_RAID_BOSS_EMOTE itself in RaidWarning.lua. The chat windows are ALSO
-- registered for that event through the MONSTER_BOSS_EMOTE message group, so
-- every tick of every countdown was written into the chat log as well - five
-- lines a wave, thirty waves a run (owner, 2026-09-20: "i don't want to see
-- 'Next wave in X...' in my chatlog. only at the top of the screen").
--
-- A chat message filter is the surgical place to cut that: filters are consulted
-- in ChatFrame_MessageEventHandler only, and RaidBossEmoteFrame_OnEvent does not
-- go through it, so the centre-screen countdown (and its warning sound) is
-- untouched. Nothing else is lost either - the pattern is anchored to the whole
-- line, so a real boss emote that merely mentions a wave still prints.
--
-- If the countdown string is ever reworded in trinity_string, reword the pattern
-- with it, or the chat lines come back.
local function CenturionChatFilters_WaveCountdown(_, _, message)
	if ( message and strfind(message, "^Next wave in %d+%.%.%.$") ) then
		return true;
	end

	return false;
end

-- Guarded because this file loads on whatever client the launcher last left in
-- place: a build without the filter API should lose the fix, not the UI.
if ( ChatFrame_AddMessageEventFilter ) then
	ChatFrame_AddMessageEventFilter("CHAT_MSG_RAID_BOSS_EMOTE", CenturionChatFilters_WaveCountdown);
end
