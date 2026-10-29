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
--
-- ALLSTAR:LIVES:<left>:<total> puts the player's own lives on screen, and
-- ALLSTAR:MAC:* lines bring the borrowed character's own macros (see Macros).
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

-- Lives ---------------------------------------------------------------------
-- ALLSTAR:LIVES:<left>:<total>:<class>:<name>: the player's own lives and the
-- life being played, under the arena's team counters (which count each
-- team's lives):
--
--     [class icon]  9/9  Tijuana          (the name in its class colour)
--                   ■■■■■■■■■             (a pip a life; spent ones go dark)
--
-- A life lost flashes it red. Put away outside an arena.
local CLASS_TOKEN = { "WARRIOR", "PALADIN", "HUNTER", "ROGUE", "PRIEST", "DEATHKNIGHT", "SHAMAN", "MAGE", "WARLOCK", nil, "DRUID" }
local PIP_W, PIP_H, PIP_GAP = 14, 5, 3
local SOLID = "Interface\\Buttons\\WHITE8X8"

local livesFrame = CreateFrame("Frame", "CenturionAllStarLives", UIParent)
livesFrame:SetHeight(50)
livesFrame:SetWidth(200)
livesFrame:SetPoint("TOP", UIParent, "TOP", 0, -78)
livesFrame:SetBackdrop({
	bgFile = "Interface\\Tooltips\\UI-Tooltip-Background",
	edgeFile = "Interface\\Tooltips\\UI-Tooltip-Border",
	tile = true, tileSize = 16, edgeSize = 14,
	insets = { left = 4, right = 4, top = 4, bottom = 4 },
})
livesFrame:SetBackdropColor(0.03, 0.03, 0.05, 0.82)
livesFrame:SetClampedToScreen(true)
livesFrame:Hide()

-- Where it sits. Left alone, under the arena's counters (WorldStateFrame's
-- AlwaysUpFrame1..n), centred on what they show - the icon to the end of the
-- text, not the frames themselves, which hang 28 px left of the middle. Dragged,
-- where it was put (CENTURION_AllStarSettings.pos, for every character); a
-- right-click puts it back under the counters.
local function LivesPlaced()
	local settings = CENTURION_AllStarSettings
	return type(settings) == "table" and type(settings.pos) == "table" and settings.pos.x and settings.pos.y
end

local function PlaceLives()
	if ( LivesPlaced() ) then
		local pos = CENTURION_AllStarSettings.pos
		livesFrame:ClearAllPoints()
		livesFrame:SetPoint("TOP", UIParent, "BOTTOMLEFT", pos.x, pos.y)
		return
	end

	local left, right, bottom
	for i = 1, NUM_ALWAYS_UP_UI_FRAMES or 0 do
		local counter = _G["AlwaysUpFrame"..i]
		if ( counter and counter:IsShown() ) then
			local icon, text = _G["AlwaysUpFrame"..i.."Icon"], _G["AlwaysUpFrame"..i.."Text"]
			local l = (icon and icon:GetLeft()) or counter:GetLeft()
			local r = (text and text:GetText() and text:GetText() ~= "" and text:GetRight()) or counter:GetRight()
			local b = counter:GetBottom()
			if ( l and r and b ) then
				left = left and math.min(left, l) or l
				right = right and math.max(right, r) or r
				bottom = bottom and math.min(bottom, b) or b
			end
		end
	end

	livesFrame:ClearAllPoints()
	if ( left ) then
		livesFrame:SetPoint("TOP", UIParent, "BOTTOMLEFT", (left + right) / 2, bottom - 2)
	else
		livesFrame:SetPoint("TOP", UIParent, "TOP", 0, -78)
	end
end

livesFrame:SetMovable(true)
livesFrame:EnableMouse(true)
livesFrame:RegisterForDrag("LeftButton")
livesFrame:SetScript("OnDragStart", function(self)
	self:StartMoving()
end)
livesFrame:SetScript("OnDragStop", function(self)
	self:StopMovingOrSizing()
	self:SetUserPlaced(false)      -- kept here, not in the client's layout cache
	if ( type(CENTURION_AllStarSettings) ~= "table" ) then
		CENTURION_AllStarSettings = {}
	end
	local x = self:GetCenter()
	CENTURION_AllStarSettings.pos = { x = x, y = self:GetTop() }
	PlaceLives()
end)
livesFrame:SetScript("OnMouseUp", function(self, button)
	if ( button == "RightButton" and type(CENTURION_AllStarSettings) == "table" ) then
		CENTURION_AllStarSettings.pos = nil
		PlaceLives()
	end
end)
livesFrame:SetScript("OnEnter", function(self)
	GameTooltip:SetOwner(self, "ANCHOR_BOTTOM")
	GameTooltip:SetText("All-Star stocks")
	GameTooltip:AddLine("Drag to move. Right-click to put it back under the scores.", 1, 1, 1, 1)
	GameTooltip:Show()
end)
livesFrame:SetScript("OnLeave", GameTooltip_Hide)

-- The counters come and go and change width with their text: followed a
-- couple of times a second while the panel is up and has not been moved.
local livesPlaceElapsed = 0
livesFrame:SetScript("OnUpdate", function(self, elapsed)
	livesPlaceElapsed = livesPlaceElapsed + elapsed
	if ( livesPlaceElapsed < 0.5 ) then
		return
	end
	livesPlaceElapsed = 0
	if ( not LivesPlaced() ) then
		PlaceLives()
	end
end)

local livesIcon = livesFrame:CreateTexture(nil, "ARTWORK")
livesIcon:SetWidth(32)
livesIcon:SetHeight(32)
livesIcon:SetPoint("LEFT", livesFrame, "LEFT", 10, 0)
livesIcon:SetTexture("Interface\\Glues\\CharacterCreate\\UI-CharacterCreate-Classes")

local livesCount = livesFrame:CreateFontString(nil, "OVERLAY", "GameFontNormalHuge")
livesCount:SetPoint("TOPLEFT", livesIcon, "TOPRIGHT", 9, 2)
local livesName = livesFrame:CreateFontString(nil, "OVERLAY", "GameFontHighlightLarge")
livesName:SetPoint("BOTTOMLEFT", livesCount, "BOTTOMRIGHT", 8, 1)

local livesPips = {}
local function LivesPip(i)
	local pip = livesPips[i]
	if ( not pip ) then
		pip = livesFrame:CreateTexture(nil, "ARTWORK")
		pip:SetTexture(SOLID)
		pip:SetWidth(PIP_W)
		pip:SetHeight(PIP_H)
		pip:SetPoint("BOTTOMLEFT", livesIcon, "BOTTOMRIGHT", 9 + (i - 1) * (PIP_W + PIP_GAP), -1)
		livesPips[i] = pip
	end
	return pip
end

-- The red wash a lost life flashes.
local livesFlash = livesFrame:CreateTexture(nil, "OVERLAY")
livesFlash:SetTexture(SOLID)
livesFlash:SetPoint("TOPLEFT", livesFrame, "TOPLEFT", 4, -4)
livesFlash:SetPoint("BOTTOMRIGHT", livesFrame, "BOTTOMRIGHT", -4, 4)
livesFlash:SetVertexColor(1.0, 0.1, 0.1)
livesFlash:SetBlendMode("ADD")
livesFlash:SetAlpha(0)
local livesFlashAnim = livesFlash:CreateAnimationGroup()
do
	local up = livesFlashAnim:CreateAnimation("Alpha")
	up:SetChange(0.55)
	up:SetDuration(0.12)
	up:SetOrder(1)
	local down = livesFlashAnim:CreateAnimation("Alpha")
	down:SetChange(-0.55)
	down:SetDuration(0.6)
	down:SetOrder(2)
end

local livesShownLeft

local function ShowLives(left, total, classId, name)
	local token = CLASS_TOKEN[classId or 0]
	local colour = token and RAID_CLASS_COLORS and RAID_CLASS_COLORS[token]
	local r, g, b = 1.0, 0.82, 0.0
	if ( colour ) then
		r, g, b = colour.r, colour.g, colour.b
	end

	local coords = token and CLASS_ICON_TCOORDS and CLASS_ICON_TCOORDS[token]
	if ( coords ) then
		livesIcon:SetTexCoord(coords[1], coords[2], coords[3], coords[4])
		livesIcon:Show()
	else
		livesIcon:Hide()
	end

	if ( left > 0 ) then
		livesCount:SetText(string.format("|cffffffff%d|r|cff8a8a8a/%d|r", left, total))
		livesName:SetText(name ~= "" and name or UnitName("player"))
		livesName:SetTextColor(r, g, b)
		livesFrame:SetBackdropBorderColor(r, g, b, 0.9)
		livesIcon:SetDesaturated(nil)
	else
		livesCount:SetText(string.format("|cffff4040%d|r|cff8a8a8a/%d|r", left, total))
		livesName:SetText("Out of stocks")
		livesName:SetTextColor(0.6, 0.6, 0.6)
		livesFrame:SetBackdropBorderColor(0.45, 0.45, 0.45, 0.9)
		livesIcon:SetDesaturated(1)
	end

	for i = 1, math.max(total, #livesPips) do
		local pip = LivesPip(i)
		if ( i > total ) then
			pip:Hide()
		else
			if ( i <= left ) then
				pip:SetVertexColor(r, g, b, 1)
			else
				pip:SetVertexColor(0.22, 0.22, 0.22, 0.9)
			end
			pip:Show()
		end
	end

	local text = livesCount:GetStringWidth() + 8 + livesName:GetStringWidth()
	local pips = total * (PIP_W + PIP_GAP) - PIP_GAP
	livesFrame:SetWidth(10 + 32 + 9 + math.max(text, pips) + 14)

	if ( livesShownLeft and left < livesShownLeft ) then
		livesFlashAnim:Stop()
		livesFlashAnim:Play()
	end
	livesShownLeft = left
	PlaceLives()
	livesFrame:Show()
end

local function OutsideArena()
	local _, instanceType = IsInInstance()
	return instanceType ~= "arena"
end

-- Macros --------------------------------------------------------------------
-- A bar button can only point at a macro of the character the client plays.
-- So for one life a borrowed character's own macros take over this
-- character's macro slots - the ones it has, then new ones up to the game's
-- 18 - and go on the buttons they sat on. At the next MAC:SWAP (a swap to a
-- character with macros) or MAC:RESET (one without, and the restore) every
-- slot gets its own macro back and the new ones are deleted; after a RESET
-- the server is told (CCGAMEREQ ALLSTAR:MACROS:RESTORED).
-- A slot keeps its macro id all along, so this character's own bars never
-- notice. While the macros are borrowed the server keeps this character's
-- real ones and refuses the client's macro uploads, so a client that goes
-- away holding the borrowed ones downloads the real ones at its next login.
--   MAC:B:<buttons>:<icon>:<name>, MAC:L:<line> and MAC:C:<more of the line>,
--   MAC:E per macro, then MAC:DONE.
-- What was written is remembered per character (CENTURION_AllStarMade) and
-- put back at the next login if a logout came first.
local MAX_ACCOUNT = MAX_ACCOUNT_MACROS or 36
local MAX_CHARACTER = MAX_CHARACTER_MACROS or 18
local macroIncoming = {}
local macroCurrent
local macroResetPending = false
local macroConfirmPending = false
local macroMakePending
local iconByTexture

local function MacroIcon(texture)
	if ( not iconByTexture ) then
		iconByTexture = {}
		for i = 1, GetNumMacroIcons() do
			local path = GetMacroIconInfo(i)
			if ( path ) then
				local short = string.match(path, "([^\\]+)$") or path
				iconByTexture[string.lower(short)] = iconByTexture[string.lower(short)] or i
			end
		end
	end
	return iconByTexture[string.lower(texture or "")] or 1
end

-- The index of this character's macro with that name and text, if any.
local function CharacterMacroIndex(name, body)
	local _, numCharacter = GetNumMacros()
	for index = MAX_ACCOUNT + 1, MAX_ACCOUNT + numCharacter do
		local n, _, b = GetMacroInfo(index)
		if ( n == name and b == body ) then
			return index
		end
	end
end

-- { written = { { own = {name, icon, body}, now = {name, body} } ... },
--   created = { {name, body} ... } }
local function Borrowed()
	local made = CENTURION_AllStarMade
	if ( type(made) == "table" and type(made.written) == "table" and type(made.created) == "table" ) then
		return made
	end
	-- addons 1.00131 kept a plain list of the macros it made: delete those.
	if ( type(made) == "table" ) then
		for k = #made, 1, -1 do
			local entry = made[k]
			local index = type(entry) == "table" and CharacterMacroIndex(entry.name, entry.body)
			if ( index ) then
				DeleteMacro(index)
			end
		end
	end
	made = { written = {}, created = {} }
	CENTURION_AllStarMade = made
	return made
end

local function TellServerRestored()
	local playerName = UnitName("player")
	if ( SendAddonMessage and playerName ) then
		SendAddonMessage("CCGAMEREQ", "ALLSTAR:MACROS:RESTORED", "WHISPER", playerName)
	end
end

-- Every slot back to its own macro, the new ones gone. Only a bare RESET is
-- confirmed to the server: after a SWAP the next character's macros follow,
-- and a confirmation arriving after the server marked them borrowed would
-- undo the protection of this character's own.
local function RestoreMacros(confirm)
	local made = Borrowed()
	for k = #made.written, 1, -1 do
		local entry = made.written[k]
		local index = CharacterMacroIndex(entry.now.name, entry.now.body)
		if ( index ) then
			EditMacro(index, entry.own.name, MacroIcon(entry.own.icon), entry.own.body)
		end
	end
	for k = #made.created, 1, -1 do
		local entry = made.created[k]
		local index = CharacterMacroIndex(entry.name, entry.body)
		if ( index ) then
			DeleteMacro(index)
		end
	end
	wipe(made.written)
	wipe(made.created)
	if ( confirm ) then
		TellServerRestored()
	end
end

local function ShortIcon(texture)
	return string.match(texture or "", "([^\\]+)$") or texture
end

local function ApplyMacros(list)
	local made = Borrowed()
	-- This character's own, as they are now (RestoreMacros ran first).
	local own = {}
	local _, numCharacter = GetNumMacros()
	for index = MAX_ACCOUNT + 1, MAX_ACCOUNT + numCharacter do
		local name, texture, body = GetMacroInfo(index)
		table.insert(own, { name = name, icon = ShortIcon(texture), body = body })
	end

	local nextOwn = 1
	for _, macro in ipairs(list) do
		local index
		-- Over one of this character's own while it has any left...
		while ( not index and nextOwn <= #own ) do
			local slot = own[nextOwn]
			nextOwn = nextOwn + 1
			local at = CharacterMacroIndex(slot.name, slot.body)
			if ( at ) then
				index = EditMacro(at, macro.name, MacroIcon(macro.icon), macro.body)
				if ( index ) then
					local name, _, body = GetMacroInfo(index)
					table.insert(made.written, { own = slot, now = { name = name, body = body } })
				end
			end
		end
		-- ...then in a new one.
		if ( not index ) then
			local _, count = GetNumMacros()
			if ( count < MAX_CHARACTER ) then
				index = CreateMacro(macro.name, MacroIcon(macro.icon), macro.body, 1)
				if ( index ) then
					local name, _, body = GetMacroInfo(index)
					table.insert(made.created, { name = name, body = body })
				end
			end
		end
		if ( index and index > 0 ) then
			for _, button in ipairs(macro.buttons) do
				PickupMacro(index)
				PlaceAction(button + 1)
				ClearCursor()
			end
		end
	end
end

-- Making, deleting and placing macros is refused in combat: it waits.
local function RunMacroWork()
	if ( InCombatLockdown() ) then
		return
	end
	if ( macroResetPending ) then
		macroResetPending = false
		RestoreMacros(macroConfirmPending)
		macroConfirmPending = false
	end
	if ( macroMakePending ) then
		local list = macroMakePending
		macroMakePending = nil
		ApplyMacros(list)
	end
end

local function OnMacroLine(line)
	if ( line == "RESET" or line == "SWAP" ) then
		macroIncoming = {}
		macroCurrent = nil
		macroMakePending = nil
		macroResetPending = true
		macroConfirmPending = ( line == "RESET" )
		RunMacroWork()
	elseif ( line == "DONE" ) then
		macroMakePending = macroIncoming
		macroIncoming = {}
		RunMacroWork()
	elseif ( line == "E" ) then
		if ( macroCurrent ) then
			table.insert(macroIncoming, macroCurrent)
			macroCurrent = nil
		end
	else
		local kind, rest = string.match(line, "^(%u):(.*)$")
		if ( kind == "B" ) then
			local buttons, icon, name = string.match(rest, "^([%d,]+):([^:]*):(.*)$")
			if ( buttons ) then
				macroCurrent = { buttons = {}, icon = icon, name = name, body = "" }
				for button in string.gmatch(buttons, "%d+") do
					table.insert(macroCurrent.buttons, tonumber(button))
				end
			end
		elseif ( kind == "L" and macroCurrent ) then
			if ( macroCurrent.body ~= "" ) then
				macroCurrent.body = macroCurrent.body .. "\n"
			end
			macroCurrent.body = macroCurrent.body .. rest
		elseif ( kind == "C" and macroCurrent ) then
			macroCurrent.body = macroCurrent.body .. rest
		end
	end
end

-- Class on the unit frames ----------------------------------------------------
-- A swap keeps the same character in view and changes its class byte, which
-- the client announces only as UNIT_DISPLAYPOWER (the event of
-- UNIT_FIELD_BYTES_0, where race, class, gender and power type live). The
-- stock enemy frames (Blizzard_ArenaUI) set an opponent's class circle only
-- when the client first sees it (ARENA_OPPONENT_UPDATE "seen") or on
-- UNIT_NAME_UPDATE - and the name the server sends again at a swap gets here
-- before the class byte does, while UnitClass reads the unit in view (the name
-- cache only for one out of sight). So the circle stays on an earlier life.
-- It is set again whenever an opponent's display power changes. Health bars
-- that an addon colours by class and locks (PlayerFrame, TargetFrame and
-- FocusFrame under UnitFramesImproved, through its global UnitColor) are
-- coloured again the same way. Only textures and colours are touched: the
-- frames are secure, and showing or updating them is not ours to do in combat.
local function RefreshArenaClass(unit)
	local id = string.match(unit, "^arena(%d)$")
	local frame = id and _G["ArenaEnemyFrame" .. id]
	if ( not frame or not frame.classPortrait ) then
		return
	end
	local _, class = UnitClass(unit)
	local coords = class and CLASS_ICON_TCOORDS[class]
	if ( coords ) then
		frame.classPortrait:SetTexture("Interface\\TargetingFrame\\UI-Classes-Circles")
		frame.classPortrait:SetTexCoord(unpack(coords))
	end
end

local LOCKED_CLASS_BARS = {
	player = "PlayerFrameHealthBar",
	target = "TargetFrameHealthBar",
	focus = "FocusFrameHealthBar",
}

local function RefreshClassColour(unit)
	local bar = LOCKED_CLASS_BARS[unit] and _G[LOCKED_CLASS_BARS[unit]]
	if ( bar and bar.lockColor and type(UnitColor) == "function" and UnitIsPlayer(unit) ) then
		bar:SetStatusBarColor(UnitColor(unit))
	end
end

local function OnClassMaybeChanged(unit)
	if ( type(unit) ~= "string" ) then
		return
	end
	RefreshArenaClass(unit)
	RefreshClassColour(unit)
end

local cleanedAtLogin = false

local listener = CreateFrame("Frame")
listener:RegisterEvent("CHAT_MSG_ADDON")
listener:RegisterEvent("PLAYER_REGEN_ENABLED")
listener:RegisterEvent("ADDON_LOADED")
listener:RegisterEvent("PLAYER_ENTERING_WORLD")
listener:RegisterEvent("ZONE_CHANGED_NEW_AREA")
listener:RegisterEvent("UNIT_DISPLAYPOWER")
listener:RegisterEvent("UNIT_NAME_UPDATE")
listener:SetScript("OnEvent", function(self, event, prefix, message)
	if ( event == "UNIT_DISPLAYPOWER" or event == "UNIT_NAME_UPDATE" ) then
		OnClassMaybeChanged(prefix)
		return
	end
	if ( event == "PLAYER_ENTERING_WORLD" or event == "ZONE_CHANGED_NEW_AREA" ) then
		if ( OutsideArena() ) then
			livesFrame:Hide()
			-- Macros a match borrowed and never got to put back.
			if ( not cleanedAtLogin ) then
				cleanedAtLogin = true
				macroResetPending = true
				macroConfirmPending = true
				RunMacroWork()
			end
		end
		return
	end
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
		RunMacroWork()
		return
	end
	if ( prefix ~= "CCGAME" or type(message) ~= "string" ) then
		return
	end
	if ( message == "ALLSTAR:QUIET" ) then
		quietUntil = GetTime() + QUIET_FOR
	elseif ( string.sub(message, 1, 13) == "ALLSTAR:BIND:" ) then
		OnBindLine(string.sub(message, 14))
	elseif ( string.sub(message, 1, 12) == "ALLSTAR:MAC:" ) then
		OnMacroLine(string.sub(message, 13))
	else
		-- An older server sends the two numbers alone.
		local left, total, rest = string.match(message, "^ALLSTAR:LIVES:(%d+):(%d+):?(.*)$")
		if ( left and not OutsideArena() ) then
			local classId, name = string.match(rest or "", "^(%d+):(.*)$")
			ShowLives(tonumber(left), tonumber(total), tonumber(classId), name or "")
		end
	end
end)
