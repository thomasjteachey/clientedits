-- Extra mocks the replay addon needs on top of wowmock.lua / wowmock_ext.lua.
local Region = getmetatable(UIParent)

function Region:SetStatusBarTexture(t)
	self._bartex = self._bartex or self:CreateTexture(nil, "ARTWORK")
	self._bartex:SetTexture(t)
end
function Region:GetStatusBarTexture()
	self._bartex = self._bartex or self:CreateTexture(nil, "ARTWORK")
	return self._bartex
end
function Region:SetStatusBarColor(r, g, b, a) self._barColour = { r, g, b, a or 1 } end
function Region:GetMinMaxValues() return self._min or 0, self._max or 1 end
function Region:SetBackdropBorderColor(r, g, b, a) self._border = { r, g, b, a or 1 } end
function Region:SetScale(s) self._scale = s end
function Region:GetScale() return self._scale or 1 end
function Region:GetEffectiveScale() return self._scale or 1 end
function Region:SetCooldown(start, dur) self._cd = { start, dur } end
function Region:SetReverse(r) self._reverse = r end
function Region:SetParent(p) self._parent = p end
function Region:SetVertTile() end
function Region:SetHorizTile() end
function Region:SetTextHeight(h) self._textHeight = h end
function Region:GetFont() return self._fontFile, self._fontSize end
function Region:SetFormattedText(fmt, ...) self:SetText(string.format(fmt, ...)) end
function Region:SetID(i) self._fid = i end
function Region:HasScript() return true end
function Region:GetTop() return 0 end
function Region:GetBottom() return 0 end
function Region:GetLeft() return 0 end
function Region:GetRight() return 0 end
function Region:SetSpellByID() end
function Region:AddMessage(m) MOCK.chat = MOCK.chat or {}; table.insert(MOCK.chat, m) end
function Region:SetBackdropColor() end

WorldFrame = CreateFrame("Frame", "WorldFrame")
WorldFrame._w, WorldFrame._h = 1366, 768
DEFAULT_CHAT_FRAME = CreateFrame("Frame", "ChatFrame1")
STANDARD_TEXT_FONT = "Fonts\\FRIZQT__.TTF"
DAMAGE_TEXT_FONT = "Fonts\\FRIZQT__.TTF"
SlashCmdList = {}
RAID_CLASS_COLORS = {
	WARRIOR = { r = 0.78, g = 0.61, b = 0.43 }, PALADIN = { r = 0.96, g = 0.55, b = 0.73 },
	HUNTER = { r = 0.67, g = 0.83, b = 0.45 }, ROGUE = { r = 1, g = 0.96, b = 0.41 },
	PRIEST = { r = 1, g = 1, b = 1 }, DEATHKNIGHT = { r = 0.77, g = 0.12, b = 0.23 },
	SHAMAN = { r = 0, g = 0.44, b = 0.87 }, MAGE = { r = 0.41, g = 0.8, b = 0.94 },
	WARLOCK = { r = 0.58, g = 0.51, b = 0.79 }, DRUID = { r = 1, g = 0.49, b = 0.04 },
}

strsub = string.sub
strfind = string.find
strlen = string.len
strlower = string.lower
strupper = string.upper
format = string.format
tremove = table.remove
function strsplit(sep, s, limit)
	local out = {}
	local start = 1
	while true do
		local i = string.find(s, sep, start, true)
		if not i or (limit and #out == limit - 1) then
			table.insert(out, string.sub(s, start))
			break
		end
		table.insert(out, string.sub(s, start, i - 1))
		start = i + #sep
	end
	return unpack(out)
end
function hooksecurefunc(a, b, c)
	local tbl, name, fn = _G, a, b
	if type(a) == "table" then tbl, name, fn = a, b, c end
	local orig = tbl[name]
	tbl[name] = function(...) local r = { orig(...) }; fn(...); return unpack(r) end
end
function CameraZoomIn(n) MOCK.zoomIn = (MOCK.zoomIn or 0) + 1 end
function CameraZoomOut(n) MOCK.zoomOut = (MOCK.zoomOut or 0) + 1 end
function RegisterAddonMessagePrefix() end
function CombatLogClearEntries() end
function GetCursorPosition() return 0, 0 end
function MouseIsOver() return false end
function IsInInstance() return 1, "arena" end
function IsActiveBattlefieldArena() return 1 end
function UnitExists() return nil end
function GetLocale() return "enUS" end
function GetBuildInfo() return "3.3.5", "12340", "Jun 24 2010", 30300 end
function SendChatMessage(m) MOCK.chatSent = m end
function LeaveBattlefield() MOCK.left = true end
function StaticPopup_Show() end
StaticPopupDialogs = {}
GameTooltip.SetSpellByID = function() end
function GameTooltip:SetOwner() end
function GameTooltip:ClearLines() end
function GameTooltip:Show() end
function GameTooltip:Hide() end

-- The DLL's follow camera.
MOCK.spectateLog = {}
function CenturionSpectate(arg, n)
	if arg ~= "?" and arg ~= "arm" and arg ~= "click" and arg ~= "version" and arg ~= "focus" and arg ~= "target" then
		table.insert(MOCK.spectateLog, tostring(arg) .. (n and (" " .. tostring(n)) or ""))
	end
	if arg == "?" then
		local moved = MOCK.moved and 1 or 0
		MOCK.moved = nil
		if MOCK.following then return MOCK.following, 1, moved end
		return nil, 0, moved
	elseif arg == "arm" then
		MOCK.armed = true
		return 1
	elseif arg == "disarm" then
		MOCK.armed = false
		return 0
	elseif arg == "version" then
		return MOCK.dllVersion or 5
	elseif arg == "target" then
		if type(n) == "string" and (n:sub(1, 2) == "0x" or n == "none") then
			if MOCK.savedTarget == nil then MOCK.savedTarget = MOCK.target or "none" end
			MOCK.target = (n ~= "none") and n or nil
		elseif MOCK.savedTarget ~= nil then
			MOCK.target = MOCK.savedTarget ~= "none" and MOCK.savedTarget or nil
			MOCK.savedTarget = nil
		end
		return MOCK.target
	elseif arg == "focus" then
		if type(n) == "string" and n:sub(1, 2) == "0x" then
			MOCK.savedFocus = MOCK.savedFocus or MOCK.focus or "none"
			MOCK.focus = n
		elseif MOCK.savedFocus then
			MOCK.focus = MOCK.savedFocus ~= "none" and MOCK.savedFocus or nil
			MOCK.savedFocus = nil
		end
		return MOCK.focus
	elseif arg == "click" then
		local c = MOCK.click
		MOCK.click = nil
		return c
	elseif arg == "zoom" then
		MOCK.zoom = (MOCK.zoom or 0) + n
		return MOCK.following, 1
	elseif type(arg) == "string" and arg:sub(1, 2) == "0x" then
		MOCK.following = arg
		return arg, 1
	else
		MOCK.following = nil
		return nil
	end
end

-- The default action bars: ActionButton1 holds whatever MOCK.actions[1] says.
MOCK.actions = {}
function GetActionInfo(slot)
	local a = MOCK.actions[slot]
	if not a then return nil end
	return "spell", 1, "spell", a
end
function Region:LockHighlight() self._lockedHighlight = true end
function Region:UnlockHighlight() self._lockedHighlight = false end
for i = 1, 12 do
	local b = CreateFrame("CheckButton", "ActionButton" .. i, UIParent)
	b.action = i
end
-- The default player cast bar (CastingBarFrame.xml/.lua's names and fields).
CastingBarFrame = CreateFrame("StatusBar", "CastingBarFrame", UIParent)
CastingBarFrame.showCastbar = true
CastingBarFrame:Hide()
for _, part in ipairs({ "Spark", "Text", "Flash" }) do
	CastingBarFrame:CreateTexture("CastingBarFrame" .. part, "OVERLAY")
end
CastingBarFrameText = CastingBarFrame:CreateFontString("CastingBarFrameText", "OVERLAY")
CastingBarFrameIcon = CastingBarFrame:CreateTexture("CastingBarFrameIcon", "OVERLAY")
INTERRUPTED = "Interrupted"
FAILED = "Failed"
CASTING_BAR_HOLD_TIME = 1
function CastingBarFrame_FinishSpell(self, barSpark, barFlash)
	self:SetStatusBarColor(0.0, 1.0, 0.0)
	if barSpark then barSpark:Hide() end
	if barFlash then barFlash:SetAlpha(0.0); barFlash:Show() end
	self.flash = 1
	self.fadeOut = 1
	self.casting = nil
	self.channeling = nil
end

MOCK.cvars = { Sound_ListenerAtCharacter = "1" }
function GetCVar(name) return MOCK.cvars[name] end
function SetCVar(name, value) MOCK.cvars[name] = tostring(value) end
ChatFrame_AddMessageEventFilter = function(event, fn) MOCK.chatFilters = MOCK.chatFilters or {}; MOCK.chatFilters[event] = fn end
ERR_LEARN_SPELL_S = "You have learned a new spell: %s."
ERR_SPELL_UNLEARNED_S = "You have unlearned %s."

-- The scoreboard (WorldStateFrame.lua): shut in an arena until it is over.
WorldStateScoreFrame = CreateFrame("Frame", "WorldStateScoreFrame", UIParent)
WorldStateScoreFrame:Hide()
function ShowUIPanel(f) f:Show() end
function HideUIPanel(f) f:Hide() end
-- The scoreboard's columns and rows (WorldStateFrame.xml names), for the replay layout.
MAX_WORLDSTATE_SCORE_BUTTONS = 3
WORLDSTATESCOREFRAME_PADDING = 35
WORLDSTATESCOREFRAME_COLUMN_SPACING = 76
for _, n in ipairs({ "Tab1", "Tab2", "Tab3", "Team", "TeamSkill", "Deaths", "HK", "KB", "DamageDone", "HealingDone", "HonorGained", "Name", "Class" }) do
	local f = CreateFrame("Button", "WorldStateScoreFrame" .. n, WorldStateScoreFrame)
	f:Show()
	f._w = (n == "Name" and 88) or (n == "Class" and 24) or 50
end
WorldStateScoreFrameHonorGainedText = WorldStateScoreFrameHonorGained:CreateFontString("WorldStateScoreFrameHonorGainedText", "OVERLAY")
function Region:GetWidth() return self._w or 0 end
WorldStateScorePlayerCount = WorldStateScoreFrame:CreateFontString("WorldStateScorePlayerCount", "OVERLAY")
WorldStateScoreScrollFrame = CreateFrame("Frame", "WorldStateScoreScrollFrame", WorldStateScoreFrame)
WorldStateScoreScrollFrame:Hide()
for i = 1, 3 do
	local row = CreateFrame("Button", "WorldStateScoreButton" .. i, WorldStateScoreFrame)
	CreateFrame("Button", "WorldStateScoreButton" .. i .. "Name", row)
	for _, part in ipairs({ "NameText", "HonorableKills", "Deaths", "Team", "TeamSkill", "HonorGained" }) do
		local f = row:CreateFontString("WorldStateScoreButton" .. i .. part, "OVERLAY")
		f:Show()
	end
	for _, part in ipairs({ "FactionLeft", "FactionRight" }) do
		row:CreateTexture("WorldStateScoreButton" .. i .. part, "ARTWORK")
	end
end
function Region:SetVertexColor(r, g, b) self._vc = { r, g, b } end
MOCK.scores = {}
function GetNumBattlefieldScores() return #MOCK.scores end
function GetBattlefieldScore(i) local r = MOCK.scores[i]; return r.name, 0, 0, 0, 0, r.faction end
function GetNumBattlefieldStats() return 0 end
function FauxScrollFrame_GetOffset() return 0 end
function IsActiveBattlefieldArena() return nil end
function WorldStateScoreFrame_Update()
	for i = 1, #MOCK.scores do _G["WorldStateScoreButton" .. i .. "Name"].name = MOCK.scores[i].name end
	WorldStateScorePlayerCount:Show()
end
function WorldStateScoreFrame_Resize(width) MOCK.boardWidth = width or 999 end
MOCK.scoreRequests = 0
function RequestBattlefieldScoreData() MOCK.scoreRequests = MOCK.scoreRequests + 1 end
function ToggleWorldStateScoreFrame()
	if WorldStateScoreFrame:IsShown() then HideUIPanel(WorldStateScoreFrame) return end
	MOCK.stockToggle = (MOCK.stockToggle or 0) + 1   -- an arena: stays shut
end

-- The player frame, the focus frame and the squad frames' feed.
PlayerFrame = CreateFrame("Button", "PlayerFrame", UIParent)
PlayerFrame.unit = "player"
PlayerFrameHealthBar = CreateFrame("StatusBar", "PlayerFrameHealthBar", PlayerFrame)
PlayerFrameManaBar = CreateFrame("StatusBar", "PlayerFrameManaBar", PlayerFrame)
PlayerLevelText = PlayerFrame:CreateFontString("PlayerLevelText", "OVERLAY")
FocusFrame = CreateFrame("Button", "FocusFrame", UIParent)
function Region:EnableMouse(on) self._mouse = on end
function Region:SetAlpha(a) self._alpha = a end
function UnitFrame_SetUnit(frame, unit, hb, mb) frame.unit = unit; hb.unit = unit; if mb then mb.unit = unit end end
function UnitLevel(unit) if unit == "focus" then return 80 end return 1 end
MOCK.party = {}
function UnitGUID(unit) if unit == "focus" then return MOCK.focus end
	if unit == "focustarget" then return MOCK.focusTarget end
	if unit == "target" then return MOCK.target end
	if unit == "targettarget" then return MOCK.targetTarget end
	local n = type(unit) == "string" and tonumber(unit:match("^party(%d)$")) if n then return MOCK.party[n] end return nil end
for i = 1, 4 do local f = CreateFrame("Button", "PartyMemberFrame" .. i, UIParent); f.unit = "party" .. i end
TargetFrame = CreateFrame("Button", "TargetFrame", UIParent); TargetFrame.unit = "target"
TargetFrameToT = CreateFrame("Button", "TargetFrameToT", UIParent); TargetFrameToT.unit = "targettarget"
function CloseDropDownMenus() MOCK.menusClosed = (MOCK.menusClosed or 0) + 1 end
function InCombatLockdown() return MOCK.combat end
CENTURION_SQUAD_NAMES = { [0] = "Gold Team", [1] = "Green Team", [2] = "Blue Team", [3] = "Purple Team" }
MOCK.squadLines = {}
for sq = 0, 3 do for i = 1, 5 do CreateFrame("Button", "CenturionSquadFrame" .. sq .. "Member" .. i, UIParent) end end
function CenturionSquadFrames_OnLine(line) table.insert(MOCK.squadLines, line) end

-- Recount's "Reset Recount?" on joining a group.
Recount = { ShowReset = function(self) MOCK.recountAsked = (MOCK.recountAsked or 0) + 1 end }
ERR_JOINED_GROUP_S = "%s joins the party."
ERR_SET_LOOT_FREEFORALL = "Looting changed to Free for All."
