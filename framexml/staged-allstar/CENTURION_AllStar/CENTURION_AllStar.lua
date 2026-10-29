-- CENTURION_AllStar: the client side of an All-Star class swap.
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
--
-- The swap also brings the character's keybindings: the server sends them as
--
--   ALLSTAR:BIND:BEGIN, ALLSTAR:BIND:<KEY> <COMMAND> ..., ALLSTAR:BIND:END
--
-- (the character keeps a set of its own), ALLSTAR:BIND:ACCOUNT (it plays on
-- the account's set) or ALLSTAR:BIND:RESET (the player is themselves again,
-- or wears a starter template). None of it is ever saved: SaveBindings is not
-- called, and RESET loads the player's real set back from the client's files.
-- Without the server's lines this addon does nothing.

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

-- Keybindings ---------------------------------------------------------------
-- GetCurrentBindingSet's numbers (Blizzard_BindingUI names them, but it loads on demand).
local DEFAULT_SET, ACCOUNT_SET = 0, 1
local realSet           -- the player's own set, while they wear somebody else's
local incoming          -- the lines of a BEGIN .. END on its way
local pending           -- what to put on once combat lockdown lifts

local function ApplyBindings(what)
	if ( InCombatLockdown() ) then
		pending = what
		return
	end
	pending = nil

	if ( what == "RESET" ) then
		if ( realSet ) then
			LoadBindings(realSet)
			realSet = nil
		end
		return
	end

	if ( not realSet ) then
		realSet = GetCurrentBindingSet()
	end
	if ( what == "ACCOUNT" ) then
		LoadBindings(ACCOUNT_SET)
		return
	end
	-- A set is stored as what it changes over the defaults.
	LoadBindings(DEFAULT_SET)
	for _, line in ipairs(what) do
		local key, command = string.match(line, "^(%S+) (.+)$")
		if ( key ) then
			if ( command == "NONE" ) then
				SetBinding(key)
			else
				SetBinding(key, command)
			end
		end
	end
end

local function OnBindLine(line)
	if ( line == "BEGIN" ) then
		incoming = {}
	elseif ( line == "END" ) then
		if ( incoming ) then
			local lines = incoming
			incoming = nil
			ApplyBindings(lines)
		end
	elseif ( line == "ACCOUNT" or line == "RESET" ) then
		incoming = nil
		ApplyBindings(line)
	elseif ( incoming ) then
		table.insert(incoming, line)
	end
end

-- While a borrowed set is on, the Key Bindings window stays shut: its Okay
-- button would save that set over the player's own.
local function GuardBindingWindow()
	if ( not KeyBindingFrame or KeyBindingFrame.allStarGuarded ) then
		return
	end
	KeyBindingFrame.allStarGuarded = true
	KeyBindingFrame:HookScript("OnShow", function(self)
		if ( realSet ) then
			self:Hide()
			UIErrorsFrame:AddMessage("Key bindings can be changed once the All-Star match is over.", 1.0, 0.1, 0.1, 1.0)
		end
	end)
end
GuardBindingWindow()

local listener = CreateFrame("Frame")
listener:RegisterEvent("CHAT_MSG_ADDON")
listener:RegisterEvent("PLAYER_REGEN_ENABLED")
listener:RegisterEvent("ADDON_LOADED")
listener:SetScript("OnEvent", function(self, event, prefix, message)
	if ( event == "ADDON_LOADED" ) then
		if ( prefix == "Blizzard_BindingUI" ) then
			GuardBindingWindow()
		end
		return
	end
	if ( event == "PLAYER_REGEN_ENABLED" ) then
		if ( pending ) then
			ApplyBindings(pending)
		end
		return
	end
	if ( prefix ~= "CCGAME" or type(message) ~= "string" ) then
		return
	end
	if ( message == "ALLSTAR:QUIET" ) then
		quietUntil = GetTime() + QUIET_FOR
	elseif ( string.sub(message, 1, 13) == "ALLSTAR:BIND:" ) then
		OnBindLine(string.sub(message, 14))
	end
end)
