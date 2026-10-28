-- CENTURION_AllStar: the quiet side of an All-Star class swap.
--
-- Every death in an All-Star arena swaps the whole spellbook, the skills and
-- the bars of the character in place for the next character of the lineup, and
-- the client prints a line for every spell it is told about: "You have learned
-- a new spell", "You have unlearned", "Your skill in ... has increased". A swap
-- is a few hundred of them. The server says
--
--   CCGAME ALLSTAR:QUIET
--
-- just before it starts one, and those lines are hidden for a few seconds.
-- Nothing else is touched; without the server line this addon does nothing.

local QUIET_FOR = 5            -- seconds after the server's line
local quietUntil = 0

local patterns = {}
local function AddPattern(name, specs)
	local fmt = _G[name]
	if ( type(fmt) ~= "string" ) then
		return
	end
	local pattern = string.gsub(fmt, "([%(%)%.%%%+%-%*%?%[%]%^%$])", "%%%1")
	pattern = string.gsub(pattern, specs, ".+")
	table.insert(patterns, "^" .. pattern .. "$")
end
-- By name: this client lacks some of them, and a missing one must not stop the rest.
for _, name in ipairs({ "ERR_LEARN_SPELL_S", "ERR_LEARN_ABILITY_S", "ERR_LEARN_PASSIVE_S", "ERR_SPELL_UNLEARNED_S",
		"ERR_LEARN_RECIPE_S", "ERR_LEARN_COMPANION_S", "ERR_LEARN_MOUNT_S" }) do
	AddPattern(name, "%%%%s")
end
for _, name in ipairs({ "SKILL_RANK_UP", "ERR_SKILL_GAINED_S", "ERR_SKILL_UP_SI" }) do
	AddPattern(name, "%%%%[sd]")
end

local function Quiet()
	return GetTime() < quietUntil
end

ChatFrame_AddMessageEventFilter("CHAT_MSG_SYSTEM", function(self, event, message, ...)
	if ( not Quiet() or type(message) ~= "string" ) then
		return false
	end
	for _, pattern in ipairs(patterns) do
		if ( string.find(message, pattern) ) then
			return true
		end
	end
	return false
end)

ChatFrame_AddMessageEventFilter("CHAT_MSG_SKILL", function()
	return Quiet()
end)

local listener = CreateFrame("Frame")
listener:RegisterEvent("CHAT_MSG_ADDON")
listener:SetScript("OnEvent", function(self, event, prefix, message)
	if ( prefix == "CCGAME" and message == "ALLSTAR:QUIET" ) then
		quietUntil = GetTime() + QUIET_FOR
	end
end)
