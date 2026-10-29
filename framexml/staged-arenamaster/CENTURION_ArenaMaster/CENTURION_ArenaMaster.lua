-- CENTURION_ArenaMaster: the one arena battlemaster's window.
--
-- Every plain arena battlemaster is the Arena Master. Talking to one, the
-- server whispers (prefix CCGAME):
--
--   ARENAQUEUE:0        the old ArenaFrame.lua back to its ordinary mode
--   ARENAQUEUE:MASTER   this addon takes the window over
--   AM:MODES:<rated>:<skirmish>:<multi>:<shuffle>:<allstar>:<replays>:<squads>
--   AM:LINEUP:<shuffled>:<class>,<name>;...    the All-Star lives, a moment later
--
-- and then opens the stock arena window, which is put away at once for this
-- one. The window is the bounty board's: the achievement frame's wood, carved
-- header and category list, a cork board on the right. An "Arena type" menu
-- picks the queue - Rated, Skirmish, Multi-team (Four Corners), Buffle
-- Shuffle, All-Star - or the arena replays. Requests go back on CCGAMEREQ:
--
--   ARENAMASTER:JOIN:<R|S|MR|MS|B|A>:<size>:<group 0|1>
--   ARENABOTS[:0|1]                      the "Arena bot matches" box
--   REPLAYS:LIST:<RECENT|FAV>  REPLAYS:WATCH:<id>  REPLAYS:FAV:<id>:<0|1>
--
-- Optional, if the server sends it: AM:POOL:<mapId>,<mapId>,... - the arenas a
-- skirmish can be fought in, shown on the Skirmish page.

local FW, FH = 768, 500
local AX, AY, AW, AH = 196, 19, 553, 461      -- the board, beside the list

local INK   = { 40 / 255, 24 / 255, 6 / 255 }
local BLOOD = { 110 / 255, 12 / 255, 8 / 255 }
local WIN   = { 16 / 255, 84 / 255, 8 / 255 }
local BLACK = { 0, 0, 0 }
local MORPHEUS = "Fonts\\MORPHEUS.ttf"
local FRIZ = "Fonts\\FRIZQT__.TTF"
local SCREENS = "Interface\\Glues\\LoadingScreens\\"

local CLASS_TOKEN = {
	[1] = "WARRIOR", [2] = "PALADIN", [3] = "HUNTER", [4] = "ROGUE", [5] = "PRIEST",
	[6] = "DEATHKNIGHT", [7] = "SHAMAN", [8] = "MAGE", [9] = "WARLOCK", [11] = "DRUID",
}

-- The arenas every client has art for; a map without its own loading screen
-- borrows one of them, always the same one for the same map.
local STOCK_SCREENS = {
	SCREENS .. "LoadScreenNagrandArenaBattlegrounds",
	SCREENS .. "LoadScreenBladesEdgeArena",
	SCREENS .. "LoadScreenRuinsofLordaeronBattlegrounds",
	SCREENS .. "LoadScreenDalaranSewersArena",
	SCREENS .. "LoadScreenOrgrimmarArena",
}

local MODES = {
	{ key = "R", name = "Rated",          icon = "Interface\\Icons\\Achievement_Arena_3v3_7" },
	{ key = "S", name = "Skirmish",       icon = "Interface\\Icons\\Achievement_Arena_2v2_1" },
	{ key = "M", name = "Multi-team",     icon = "Interface\\Icons\\Achievement_Arena_5v5_7" },
	{ key = "B", name = "Buffle Shuffle", icon = "Interface\\Icons\\INV_Misc_Dice_01" },
	{ key = "A", name = "All-Star",       icon = "Interface\\Icons\\Achievement_FeatsOfStrength_Gladiator_07" },
	{ key = "P", name = "Replays",        icon = "Interface\\Icons\\INV_Misc_Spyglass_02" },
}
local MODE = {}
for i, m in ipairs(MODES) do
	m.index = i
	MODE[m.key] = m
end

local TEAM_ICON = {
	[2] = "Interface\\Icons\\Achievement_Arena_2v2_7",
	[3] = "Interface\\Icons\\Achievement_Arena_3v3_7",
	[4] = "Interface\\Icons\\Achievement_Arena_3v3_7",
	[5] = "Interface\\Icons\\Achievement_Arena_5v5_7",
}

local BOTS_TOOLTIP = "Arena bot matches\n\nWhen a skirmish queue has nobody left to match you with, it is filled out with bots so the match starts anyway.\n\nUncheck to hold your place until real opponents queue. Rated arenas are never filled either way."

------------------------------------------------------------------
-- state
------------------------------------------------------------------
local state = {
	pending = false,            -- ARENAQUEUE:MASTER seen; the next arena window is ours
	modes = { R = true, S = true, M = true, B = true, A = true, P = true },
	squads = 4,
	mode = "S",
	bracket = { R = 1, S = 2, M = 2, B = 2, A = 4, P = 1 },
	lineup = nil,               -- { shuffled = bool, { class =, name = }, ... }
	pool = nil,                 -- mapIds a skirmish can land on, if the server says
	bots = nil,                 -- the "Arena bot matches" box, as the server last said
	replays = { RECENT = nil, FAV = nil },
	counts = {},
	loading = {},               -- list -> rows being received
	page = 1,
	selected = nil,             -- replay id
	replayError = nil,
}

------------------------------------------------------------------
-- small helpers
------------------------------------------------------------------
local function Split(text, sep)
	local out = {}
	if text == nil or text == "" then
		return out
	end
	local from = 1
	while true do
		local at = string.find(text, sep, from, true)
		if not at then
			table.insert(out, string.sub(text, from))
			break
		end
		table.insert(out, string.sub(text, from, at - 1))
		from = at + string.len(sep)
	end
	return out
end

local function Send(body)
	SendAddonMessage("CCGAMEREQ", body, "WHISPER", UnitName("player"))
end

local function ClassName(class)
	local token = CLASS_TOKEN[class]
	return token and LOCALIZED_CLASS_NAMES_MALE and LOCALIZED_CLASS_NAMES_MALE[token] or "Unknown"
end

-- A roster's class comes back localized; the portraits want the token.
local CLASS_BY_NAME = {}
for _, names in ipairs({ LOCALIZED_CLASS_NAMES_MALE or {}, LOCALIZED_CLASS_NAMES_FEMALE or {} }) do
	for token, name in pairs(names) do
		CLASS_BY_NAME[name] = token
	end
end

local function SetClassIcon(tex, token)
	local tc = token and CLASS_ICON_TCOORDS and CLASS_ICON_TCOORDS[token]
	if tc then
		tex:SetTexture("Interface\\TargetingFrame\\UI-Classes-Circles")
		tex:SetTexCoord(tc[1], tc[2], tc[3], tc[4])
		tex:Show()
	else
		tex:Hide()
	end
end

local function MapInfo(mapId)
	local maps = CENTURION_ArenaMaster_Maps or {}
	local info = maps[mapId]
	local name = info and info.name or ("Arena " .. tostring(mapId))
	local screen = info and info.screen
	if not screen or screen == "" then
		screen = STOCK_SCREENS[((tonumber(mapId) or 0) % #STOCK_SCREENS) + 1]
	end
	return name, screen
end

-- A loading screen is a square texture the client stretches to 4:3. Show the
-- part of it that fills a w x h box at its real shape, around height cy.
local function SetScreen(tex, file, w, h, cy, zoom)
	zoom = zoom or 1
	local hs = zoom
	local vs = 4 * zoom * h / (3 * w)
	if vs > 1 then
		hs = hs / vs
		vs = 1
	end
	local left = (1 - hs) / 2
	local top = math.min(math.max(0, (cy or 0.5) - vs / 2), 1 - vs)
	tex:SetTexture(file)
	tex:SetTexCoord(left, left + hs, top, top + vs)
end

local function MakeFont(name, file, size, colour, shadow)
	local f = CreateFont(name)
	f:SetFont(file, size)
	f:SetTextColor(colour[1], colour[2], colour[3])
	if shadow then
		f:SetShadowColor(shadow[1], shadow[2], shadow[3], 1)
		f:SetShadowOffset(1, -1)
	else
		f:SetShadowOffset(0, 0)
	end
	return f
end

local fontTitle     = MakeFont("CenturionArenaMasterTitle", MORPHEUS, 24, BLOOD)
local fontTitleSm   = MakeFont("CenturionArenaMasterTitleSmall", MORPHEUS, 19, BLOOD)
local fontHeading   = MakeFont("CenturionArenaMasterHeading", MORPHEUS, 15, BLACK)
local fontFact      = MakeFont("CenturionArenaMasterFact", MORPHEUS, 14, BLACK)
local fontCardTitle = MakeFont("CenturionArenaMasterCardTitle", MORPHEUS, 15, BLACK)
local fontCardSmall = MakeFont("CenturionArenaMasterCardTitleSmall", MORPHEUS, 13, BLACK)
local fontInk12     = MakeFont("CenturionArenaMasterInk12", FRIZ, 12, INK)
local fontInk11     = MakeFont("CenturionArenaMasterInk11", FRIZ, 11, INK)
local fontInk10     = MakeFont("CenturionArenaMasterInk10", FRIZ, 10, INK)
local fontInk9      = MakeFont("CenturionArenaMasterInk9", FRIZ, 9, INK)
local fontWin       = MakeFont("CenturionArenaMasterWin", FRIZ, 10, WIN)
local fontLoss      = MakeFont("CenturionArenaMasterLoss", FRIZ, 10, BLOOD)
local fontNote      = MakeFont("CenturionArenaMasterNote", FRIZ, 10, { 0.78, 0.78, 0.78 }, BLACK)

-- The first of the fonts the text fits in at that width; the last one
-- otherwise. True when one did.
local function FitText(fs, text, width, ...)
	fs:SetText(text)
	for i = 1, select("#", ...) do
		fs:SetFontObject((select(i, ...)))
		if fs:GetStringWidth() <= width then
			return true
		end
	end
	return false
end

------------------------------------------------------------------
-- the frame: the achievement frame's wood, header and category list
------------------------------------------------------------------
local am = CreateFrame("Frame", "CenturionArenaMaster", UIParent)
am:SetWidth(FW)
am:SetHeight(FH)
am:SetPoint("CENTER", UIParent, "CENTER", 0, -20)
am:SetFrameStrata("MEDIUM")
am:SetToplevel(true)
am:EnableMouse(true)
am:SetBackdrop({
	bgFile   = "Interface\\TutorialFrame\\TutorialFrameBackground",
	edgeFile = "Interface\\AchievementFrame\\UI-Achievement-WoodBorder",
	tile = true, tileSize = 32, edgeSize = 64,
	insets = { left = 16, right = 16, top = 16, bottom = 16 },
})
am:Hide()
tinsert(UISpecialFrames, "CenturionArenaMaster")

-- Positions are the mockup's: x right and y down from the frame's top-left.
local function At(region, x, y, rel)
	region:SetPoint("TOPLEFT", rel or am, "TOPLEFT", x, -y)
end

local function Corner(point, x, y, l, r, t, b)
	local tex = am:CreateTexture(nil, "BORDER")
	tex:SetTexture("Interface\\AchievementFrame\\UI-Achievement-WoodBorder-Corner")
	tex:SetWidth(64)
	tex:SetHeight(64)
	tex:SetPoint(point, am, point, x, y)
	tex:SetTexCoord(l, r, t, b)
end
Corner("TOPLEFT", 4, -2, 0, 1, 0, 1)
Corner("TOPRIGHT", -4, -2, 1, 0, 0, 1)
Corner("BOTTOMLEFT", 4, 3, 0, 1, 1, 0)
Corner("BOTTOMRIGHT", -4, 3, 1, 0, 1, 0)

local HEADER = "Interface\\AchievementFrame\\UI-Achievement-Header"
local headerLeft = am:CreateTexture(nil, "ARTWORK")
headerLeft:SetTexture(HEADER)
headerLeft:SetWidth(512)
headerLeft:SetHeight(106)
headerLeft:SetPoint("BOTTOMLEFT", am, "TOPLEFT", 26, -39)
headerLeft:SetTexCoord(0, 1, 0, 0.4140625)

local headerRight = am:CreateTexture(nil, "ARTWORK")
headerRight:SetTexture(HEADER)
headerRight:SetWidth(215)
headerRight:SetHeight(100)
headerRight:SetPoint("BOTTOMLEFT", headerLeft, "BOTTOMRIGHT", 0, -6)
headerRight:SetTexCoord(0, 0.419921875, 0.4140625, 0.8046875)

local plate = am:CreateTexture(nil, "OVERLAY")
plate:SetTexture(HEADER)
plate:SetWidth(193)
plate:SetHeight(39)
plate:SetPoint("BOTTOM", am, "TOPLEFT", 26 + 363 + 20, -39 + 16)
plate:SetTexCoord(0.419921875, 0.6796875, 0.4140625, 0.56640625)

local title = am:CreateFontString(nil, "OVERLAY", "GameFontNormal")
title:SetPoint("BOTTOM", plate, "TOP", 0, -2)
title:SetText("Arena Master")

local plateText = am:CreateFontString(nil, "OVERLAY", "GameFontHighlight")
plateText:SetPoint("CENTER", plate, "TOP", 0, -19)

local closeButton = CreateFrame("Button", nil, am, "UIPanelCloseButton")
closeButton:SetPoint("TOPRIGHT", am, "TOPRIGHT", -8, -8)
closeButton:SetFrameLevel(am:GetFrameLevel() + 5)

-- Forward declarations: the draw functions call each other.
local Draw, SetMode, SetBracket, DrawReplays, RequestReplays

------------------------------------------------------------------
-- the board
------------------------------------------------------------------
local area = CreateFrame("Frame", nil, am)
area:SetWidth(AW)
area:SetHeight(AH)
At(area, AX, AY)

local cork = area:CreateTexture(nil, "BACKGROUND")
cork:SetTexture("Interface\\AchievementFrame\\UI-Achievement-AchievementBackground")
cork:SetAllPoints(area)
cork:SetVertexColor(0.41, 0.31, 0.19)

local shadeTop = area:CreateTexture(nil, "BORDER")
shadeTop:SetTexture("Interface\\Buttons\\WHITE8X8")
shadeTop:SetPoint("TOPLEFT", area, "TOPLEFT")
shadeTop:SetPoint("TOPRIGHT", area, "TOPRIGHT")
shadeTop:SetHeight(10)
shadeTop:SetGradientAlpha("VERTICAL", 0, 0, 0, 0, 0, 0, 0, 0.5)

local shadeLeft = area:CreateTexture(nil, "BORDER")
shadeLeft:SetTexture("Interface\\Buttons\\WHITE8X8")
shadeLeft:SetPoint("TOPLEFT", area, "TOPLEFT")
shadeLeft:SetPoint("BOTTOMLEFT", area, "BOTTOMLEFT")
shadeLeft:SetWidth(10)
shadeLeft:SetGradientAlpha("HORIZONTAL", 0, 0, 0, 0.5, 0, 0, 0, 0)

------------------------------------------------------------------
-- the left column: the arena type menu and the brackets
------------------------------------------------------------------
local typeLabel = am:CreateFontString(nil, "OVERLAY", "GameFontNormalSmall")
typeLabel:SetPoint("LEFT", am, "TOPLEFT", 24, -30)
typeLabel:SetText("Arena type")

local DD_WIDTH = 136
local dd = CreateFrame("Frame", "CenturionArenaMasterType", am, "UIDropDownMenuTemplate")
dd:SetPoint("TOPLEFT", am, "TOPLEFT", 4, -40)
UIDropDownMenu_SetWidth(dd, DD_WIDTH)

-- The stock menu has no icon of its own; the mode's goes where the
-- character-creation menus keep theirs, left of the text.
local ddIcon = dd:CreateTexture(nil, "OVERLAY")
ddIcon:SetWidth(15)
ddIcon:SetHeight(15)
ddIcon:SetPoint("TOPLEFT", dd, "TOPLEFT", 26, -6)
ddIcon:SetTexCoord(0.07, 0.93, 0.07, 0.93)

local ddText = _G["CenturionArenaMasterTypeText"]
ddText:ClearAllPoints()
ddText:SetPoint("LEFT", dd, "TOPLEFT", 46, -14)
ddText:SetFontObject(GameFontHighlight)
ddText:SetJustifyH("LEFT")

-- The whole box opens the menu, not just its arrow.
local ddHit = CreateFrame("Button", nil, dd)
ddHit:SetPoint("TOPLEFT", dd, "TOPLEFT", 20, 0)
ddHit:SetWidth(DD_WIDTH - 2)
ddHit:SetHeight(28)
ddHit:SetScript("OnClick", function()
	ToggleDropDownMenu(nil, nil, dd)
	PlaySound("igMainMenuOptionCheckBoxOn")
end)

local function ModeAvailable(key)
	return state.modes[key] and true or false
end

local function InitTypeMenu(self, level)
	local info = UIDropDownMenu_CreateInfo()
	info.text = "Arena type"
	info.isTitle = 1
	info.notCheckable = 1
	UIDropDownMenu_AddButton(info, level)
	for _, m in ipairs(MODES) do
		if m.key == "P" then
			info = UIDropDownMenu_CreateInfo()
			info.text = ""
			info.disabled = 1
			info.notCheckable = 1
			UIDropDownMenu_AddButton(info, level)
		end
		info = UIDropDownMenu_CreateInfo()
		info.text = m.name
		info.value = m.key
		info.icon = m.icon
		info.tCoordLeft, info.tCoordRight, info.tCoordTop, info.tCoordBottom = 0.07, 0.93, 0.07, 0.93
		info.checked = state.mode == m.key
		if not ModeAvailable(m.key) then
			info.disabled = 1
		end
		local key = m.key
		info.func = function()
			SetMode(key)
		end
		UIDropDownMenu_AddButton(info, level)
	end
end
UIDropDownMenu_Initialize(dd, InitTypeMenu)

local CATEGORY_ROWS = 5
local categoryButtons = {}
for i = 1, CATEGORY_ROWS do
	local b = CreateFrame("Button", nil, am)
	b:SetWidth(158)
	b:SetHeight(24)
	At(b, 21, 78 + (i - 1) * 25)

	b.bg = b:CreateTexture(nil, "BACKGROUND")
	b.bg:SetTexture("Interface\\AchievementFrame\\UI-Achievement-Category-Background")
	b.bg:SetPoint("TOPLEFT", b, "TOPLEFT")
	b.bg:SetPoint("TOPRIGHT", b, "TOPRIGHT")
	b.bg:SetHeight(32)
	b.bg:SetTexCoord(0, 0.6640625, 0, 1)

	b.sel = b:CreateTexture(nil, "BORDER")
	b.sel:SetTexture("Interface\\AchievementFrame\\UI-Achievement-Category-Background")
	b.sel:SetAllPoints(b.bg)
	b.sel:SetTexCoord(0, 0.6640625, 0, 1)
	b.sel:SetBlendMode("ADD")
	b.sel:SetAlpha(0.6)
	b.sel:Hide()

	b.label = b:CreateFontString(nil, "OVERLAY", "GameFontNormal")
	b.label:SetPoint("LEFT", b, "TOPLEFT", 14, -13)
	b.label:SetJustifyH("LEFT")

	b.count = b:CreateFontString(nil, "OVERLAY", "GameFontHighlightSmall")
	b.count:SetPoint("RIGHT", b, "TOPRIGHT", -14, -13)

	local hl = b:CreateTexture(nil, "HIGHLIGHT")
	hl:SetTexture("Interface\\AchievementFrame\\UI-Achievement-Category-Highlight")
	hl:SetBlendMode("ADD")
	hl:SetPoint("TOPLEFT", b, "TOPLEFT", 0, 0)
	hl:SetPoint("BOTTOMRIGHT", b, "BOTTOMRIGHT", -1, -7)
	hl:SetTexCoord(0, 0.6640625, 0, 1)

	b:SetScript("OnClick", function(self)
		PlaySound("igMainMenuOptionCheckBoxOn")
		SetBracket(self.row)
	end)
	b:Hide()
	categoryButtons[i] = b
end

-- The "Arena bot matches" box, at the foot of the list on every unrated page.
local bots = CreateFrame("CheckButton", "CenturionArenaMasterBots", am, "UICheckButtonTemplate")
bots:SetWidth(24)
bots:SetHeight(24)
At(bots, 22, 444)
bots.text = bots:CreateFontString(nil, "ARTWORK", "GameFontNormalSmall")
bots.text:SetPoint("LEFT", bots, "RIGHT", 2, 1)
bots.text:SetText("Arena bot matches")
bots:SetScript("OnClick", function(self)
	local on = self:GetChecked() and true or false
	PlaySound(on and "igMainMenuOptionCheckBoxOn" or "igMainMenuOptionCheckBoxOff")
	Send(on and "ARENABOTS:1" or "ARENABOTS:0")
end)
bots:SetScript("OnEnter", function(self)
	GameTooltip:SetOwner(self, "ANCHOR_RIGHT")
	GameTooltip:SetText(BOTS_TOOLTIP, nil, nil, nil, nil, 1)
	GameTooltip:Show()
end)
bots:SetScript("OnLeave", function()
	GameTooltip:Hide()
end)

-- The Replays page's own part of the list: a match by its number, and the
-- rule for what gets recorded.
local matchLabel = am:CreateFontString(nil, "OVERLAY", "GameFontNormalSmall")
matchLabel:SetPoint("LEFT", am, "TOPLEFT", 24, -150)
matchLabel:SetText("Match number")

local matchBox = CreateFrame("EditBox", "CenturionArenaMasterMatch", am, "InputBoxTemplate")
matchBox:SetWidth(76)
matchBox:SetHeight(20)
At(matchBox, 31, 160)
matchBox:SetAutoFocus(false)
matchBox:SetNumeric(true)
matchBox:SetMaxLetters(9)

local matchWatch = CreateFrame("Button", nil, am, "UIPanelButtonTemplate")
matchWatch:SetWidth(62)
matchWatch:SetHeight(22)
At(matchWatch, 118, 159)
matchWatch:SetText("Watch")

local function WatchTyped()
	local id = tonumber(matchBox:GetText() or "")
	matchBox:ClearFocus()
	if id and id > 0 then
		PlaySound("igMainMenuOptionCheckBoxOn")
		Send("REPLAYS:WATCH:" .. id)
	end
end
matchWatch:SetScript("OnClick", WatchTyped)
matchBox:SetScript("OnEnterPressed", WatchTyped)
matchBox:SetScript("OnEscapePressed", function(self)
	self:ClearFocus()
end)

local noteRule = am:CreateTexture(nil, "ARTWORK")
noteRule:SetTexture("Interface\\Buttons\\WHITE8X8")
noteRule:SetVertexColor(1, 0.82, 0, 0.24)
noteRule:SetWidth(140)
noteRule:SetHeight(1)
At(noteRule, 30, 400)

local noteIcon = am:CreateTexture(nil, "ARTWORK")
noteIcon:SetTexture("Interface\\DialogFrame\\UI-Dialog-Icon-AlertNew")
noteIcon:SetWidth(18)
noteIcon:SetHeight(18)
At(noteIcon, 24, 410)

local noteText = am:CreateFontString(nil, "OVERLAY", "GameFontNormalSmall")
noteText:SetWidth(128)
noteText:SetJustifyH("LEFT")
noteText:SetJustifyV("TOP")
At(noteText, 46, 410)
noteText:SetText("Replays are only recorded when both teams are fully human.")

local noteSub = am:CreateFontString(nil, "OVERLAY")
noteSub:SetFontObject(fontNote)
noteSub:SetWidth(128)
noteSub:SetJustifyH("LEFT")
noteSub:SetJustifyV("TOP")
At(noteSub, 46, 450)
noteSub:SetText("Bot-filled matches are never saved.")

local replayLeft = { matchLabel, matchBox, matchWatch, noteRule, noteIcon, noteText, noteSub }

------------------------------------------------------------------
-- the sheet: one parchment, the arena's art across its top
------------------------------------------------------------------
local PW, PH = AW - 24, AH - 12 - 52
local poster = CreateFrame("Frame", nil, area)
poster:SetWidth(PW)
poster:SetHeight(PH)
At(poster, 12, 12, area)

local posterShadow = poster:CreateTexture(nil, "BACKGROUND")
posterShadow:SetTexture("Interface\\Buttons\\WHITE8X8")
posterShadow:SetVertexColor(0, 0, 0, 0.43)
posterShadow:SetPoint("TOPLEFT", poster, "TOPLEFT", 4, -5)
posterShadow:SetPoint("BOTTOMRIGHT", poster, "BOTTOMRIGHT", 4, -5)

local posterPaper = poster:CreateTexture(nil, "BORDER")
posterPaper:SetTexture("Interface\\AchievementFrame\\UI-Achievement-Parchment")
posterPaper:SetAllPoints(poster)
posterPaper:SetTexCoord(0.03, 0.97, 0.02, 0.98)

local BW, BH = PW - 24, 118
local bannerFrame = CreateFrame("Frame", nil, poster)
bannerFrame:SetWidth(BW + 2)
bannerFrame:SetHeight(BH + 2)
At(bannerFrame, 11, 11, poster)
local bannerEdge = bannerFrame:CreateTexture(nil, "BACKGROUND")
bannerEdge:SetTexture(20 / 255, 12 / 255, 4 / 255, 1)
bannerEdge:SetAllPoints(bannerFrame)
local banner = bannerFrame:CreateTexture(nil, "ARTWORK")
banner:SetWidth(BW)
banner:SetHeight(BH)
At(banner, 1, 1, bannerFrame)
local bannerShade = bannerFrame:CreateTexture(nil, "OVERLAY")
bannerShade:SetTexture("Interface\\Buttons\\WHITE8X8")
bannerShade:SetPoint("BOTTOMLEFT", banner, "BOTTOMLEFT")
bannerShade:SetPoint("BOTTOMRIGHT", banner, "BOTTOMRIGHT")
bannerShade:SetHeight(40)
bannerShade:SetGradientAlpha("VERTICAL", 0, 0, 0, 0.55, 0, 0, 0, 0)

-- The mode's emblem, in the achievement ring, over the art's lower edge.
local emblemFrame = CreateFrame("Frame", nil, poster)
emblemFrame:SetWidth(72)
emblemFrame:SetHeight(72)
emblemFrame:SetFrameLevel(poster:GetFrameLevel() + 3)
At(emblemFrame, 14, 98, poster)
local emblem = emblemFrame:CreateTexture(nil, "ARTWORK")
emblem:SetWidth(54)
emblem:SetHeight(54)
At(emblem, 9, 9, emblemFrame)
emblem:SetTexCoord(0.07, 0.93, 0.07, 0.93)
local emblemRing = emblemFrame:CreateTexture(nil, "OVERLAY")
emblemRing:SetTexture("Interface\\AchievementFrame\\UI-Achievement-IconFrame")
emblemRing:SetAllPoints(emblemFrame)
emblemRing:SetTexCoord(0, 0.5625, 0, 0.5625)

local posterTitle = poster:CreateFontString(nil, "OVERLAY")
posterTitle:SetFontObject(fontTitle)
posterTitle:SetPoint("LEFT", poster, "TOPLEFT", 94, -150)
posterTitle:SetJustifyH("LEFT")

local posterSub = poster:CreateFontString(nil, "OVERLAY")
posterSub:SetFontObject(fontInk11)
posterSub:SetPoint("LEFT", poster, "TOPLEFT", 95, -172)
posterSub:SetJustifyH("LEFT")

local divider = poster:CreateTexture(nil, "ARTWORK")
divider:SetTexture("Interface\\Common\\UI-HorizontalBreak")
divider:SetWidth(PW - 28)
divider:SetHeight(14)
divider:SetAlpha(0.8)
At(divider, 14, 186, poster)

local body = poster:CreateFontString(nil, "OVERLAY")
body:SetFontObject(fontInk12)
body:SetWidth(PW - 48)
body:SetJustifyH("LEFT")
body:SetJustifyV("TOP")
body:SetSpacing(4)
At(body, 24, 204, poster)

-- Three headed facts across the sheet.
local facts = {}
for i = 1, 3 do
	local head = poster:CreateFontString(nil, "OVERLAY")
	head:SetFontObject(fontFact)
	local value = poster:CreateFontString(nil, "OVERLAY")
	value:SetFontObject(fontInk11)
	facts[i] = { head = head, value = value }
end

local function SetFacts(y, list)
	local cw = (PW - 20) / 3
	for i, f in ipairs(facts) do
		local item = list and list[i]
		if item then
			local cx = 10 + (i - 1) * cw + cw / 2
			f.head:ClearAllPoints()
			f.head:SetPoint("CENTER", poster, "TOPLEFT", cx, -y)
			f.head:SetText(item[1])
			f.value:ClearAllPoints()
			f.value:SetPoint("CENTER", poster, "TOPLEFT", cx, -(y + 18))
			f.value:SetText(item[2])
			f.head:Show()
			f.value:Show()
		else
			f.head:Hide()
			f.value:Hide()
		end
	end
end

local heading = poster:CreateFontString(nil, "OVERLAY")
heading:SetFontObject(fontHeading)
local subheading = poster:CreateFontString(nil, "OVERLAY")
subheading:SetFontObject(fontInk9)

local function SetHeading(y, text, sub)
	heading:ClearAllPoints()
	heading:SetPoint("CENTER", poster, "TOPLEFT", PW / 2, -y)
	heading:SetText(text)
	heading:Show()
	if sub then
		subheading:ClearAllPoints()
		subheading:SetPoint("CENTER", poster, "TOPLEFT", PW / 2, -(y + 15))
		subheading:SetText(sub)
		subheading:Show()
	else
		subheading:Hide()
	end
end

-- A row of portraits: the All-Star lives, a rated team's roster.
local MAX_PORTRAITS = 10
local portraits = {}
for i = 1, MAX_PORTRAITS do
	local p = CreateFrame("Frame", nil, poster)
	p:SetWidth(44)
	p:SetHeight(44)
	p.circle = p:CreateTexture(nil, "ARTWORK")
	p.circle:SetPoint("CENTER", p, "CENTER")
	p.ring = p:CreateTexture(nil, "OVERLAY")
	p.ring:SetTexture("Interface\\AchievementFrame\\UI-Achievement-IconFrame")
	p.ring:SetAllPoints(p)
	p.ring:SetTexCoord(0, 0.5625, 0, 0.5625)
	p.name = poster:CreateFontString(nil, "OVERLAY")
	p.name:SetFontObject(fontInk10)
	p.sub = poster:CreateFontString(nil, "OVERLAY")
	p.sub:SetFontObject(fontInk9)
	p:Hide()
	portraits[i] = p
end

local function HidePortraits()
	for _, p in ipairs(portraits) do
		p:Hide()
		p.name:Hide()
		p.sub:Hide()
	end
end

-- people: { { token =, name =, sub = }, ... } spread across x .. x + w.
local function SetPortraits(x, y, w, people, ring)
	ring = ring or 44
	local inner = ring * 0.75
	local step = w / math.max(1, #people)
	for i, p in ipairs(portraits) do
		local who = people[i]
		if who then
			local cx = x + (i - 1) * step + step / 2
			p:SetWidth(ring)
			p:SetHeight(ring)
			p:ClearAllPoints()
			p:SetPoint("TOPLEFT", poster, "TOPLEFT", cx - ring / 2, -y)
			p.circle:SetWidth(inner)
			p.circle:SetHeight(inner)
			SetClassIcon(p.circle, who.token)
			p.name:ClearAllPoints()
			p.name:SetPoint("CENTER", poster, "TOPLEFT", cx, -(y + ring + 6))
			FitText(p.name, who.name or "", step - 4, fontInk10, fontInk9)
			p.sub:ClearAllPoints()
			p.sub:SetPoint("CENTER", poster, "TOPLEFT", cx, -(y + ring + 18))
			p.sub:SetText(who.sub or "")
			p:Show()
			p.name:Show()
			p.sub:Show()
		else
			p:Hide()
			p.name:Hide()
			p.sub:Hide()
		end
	end
end

-- The arenas a skirmish can be fought in (AM:POOL), five thumbnails.
local THUMBS = 5
local thumbs = {}
for i = 1, THUMBS do
	local t = CreateFrame("Frame", nil, poster)
	t:SetWidth(78)
	t:SetHeight(52)
	t.edge = t:CreateTexture(nil, "BACKGROUND")
	t.edge:SetTexture(20 / 255, 12 / 255, 4 / 255, 1)
	t.edge:SetAllPoints(t)
	t.art = t:CreateTexture(nil, "ARTWORK")
	t.art:SetWidth(76)
	t.art:SetHeight(50)
	At(t.art, 1, 1, t)
	t.name = poster:CreateFontString(nil, "OVERLAY")
	t.name:SetFontObject(fontInk10)
	t:Hide()
	thumbs[i] = t
end

local function HideThumbs()
	for _, t in ipairs(thumbs) do
		t:Hide()
		t.name:Hide()
	end
end

local function SetThumbs(y, mapIds)
	local tw, gap = 76, 18
	local n = math.min(THUMBS, #mapIds)
	local x0 = (PW - (n * tw + (n - 1) * gap)) / 2
	for i, t in ipairs(thumbs) do
		local mapId = mapIds[i]
		if i <= n and mapId then
			local name, screen = MapInfo(mapId)
			-- A thumbnail's caption is short: "Blade's Edge", "Ring of Valor".
			name = string.gsub(string.gsub(name, " Arena$", ""), "^The ", "")
			local x = x0 + (i - 1) * (tw + gap)
			t:ClearAllPoints()
			t:SetPoint("TOPLEFT", poster, "TOPLEFT", x - 1, -(y - 1))
			SetScreen(t.art, screen, 76, 50, 0.5, 0.8)
			t.name:ClearAllPoints()
			t.name:SetPoint("CENTER", poster, "TOPLEFT", x + tw / 2, -(y + 62))
			FitText(t.name, name, tw + gap - 2, fontInk10, fontInk9)
			t:Show()
			t.name:Show()
		else
			t:Hide()
			t.name:Hide()
		end
	end
end

-- A line in the middle of the sheet: no team yet, still reading.
local posterMessage = poster:CreateFontString(nil, "OVERLAY")
posterMessage:SetFontObject(fontInk12)
posterMessage:SetWidth(PW - 80)
posterMessage:SetJustifyH("CENTER")

------------------------------------------------------------------
-- the foot of the board: who you are queueing with, and the buttons
------------------------------------------------------------------
local footY = AH - 34
local status = area:CreateFontString(nil, "OVERLAY", "GameFontNormalSmall")
status:SetPoint("LEFT", area, "TOPLEFT", 16, -(footY + 11))
status:SetJustifyH("LEFT")

local FOOT_BUTTONS = 3
local footButtons = {}
for i = 1, FOOT_BUTTONS do
	local b = CreateFrame("Button", nil, area, "UIPanelButtonTemplate")
	b:SetHeight(22)
	b:SetWidth(120)
	b:SetScript("OnClick", function(self)
		if self.action then
			self.action()
		end
	end)
	b:Hide()
	footButtons[i] = b
end

-- buttons: { { label, width, enabled, action }, ... } right to left order as read.
local function SetFootButtons(list)
	local right = AW - 14
	for i = FOOT_BUTTONS, 1, -1 do
		local b = footButtons[i]
		local item = list[#list - (FOOT_BUTTONS - i)]
		if item then
			b:ClearAllPoints()
			b:SetWidth(item[2])
			b:SetPoint("TOPLEFT", area, "TOPLEFT", right - item[2], -footY)
			right = right - item[2] - 6
			b:SetText(item[1])
			if item[3] then
				b:Enable()
			else
				b:Disable()
			end
			b.action = item[4]
			b:Show()
		else
			b:Hide()
		end
	end
end

------------------------------------------------------------------
-- the replays: six cards a page
------------------------------------------------------------------
local replaysFrame = CreateFrame("Frame", nil, area)
replaysFrame:SetAllPoints(area)
replaysFrame:Hide()

local CW, CH = 262, 128
local CARDS = 6
local cards = {}

local function ToggleFavorite(id, on)
	PlaySound("igMainMenuOptionCheckBoxOn")
	Send("REPLAYS:FAV:" .. id .. ":" .. (on and 1 or 0))
end

for i = 1, CARDS do
	local c = CreateFrame("Button", nil, replaysFrame)
	c:SetWidth(CW)
	c:SetHeight(CH)
	At(c, 12 + ((i - 1) % 2) * (CW + 5), 12 + math.floor((i - 1) / 2) * (CH + 7), replaysFrame)

	c.shadow = c:CreateTexture(nil, "BACKGROUND")
	c.shadow:SetTexture("Interface\\Buttons\\WHITE8X8")
	c.shadow:SetVertexColor(0, 0, 0, 0.43)
	c.shadow:SetPoint("TOPLEFT", c, "TOPLEFT", 4, -5)
	c.shadow:SetPoint("BOTTOMRIGHT", c, "BOTTOMRIGHT", 4, -5)

	c.paper = c:CreateTexture(nil, "BORDER")
	c.paper:SetTexture("Interface\\AchievementFrame\\UI-Achievement-Parchment-Horizontal")
	c.paper:SetAllPoints(c)
	c.paper:SetTexCoord(0.02, 0.98, 0.04, 0.96)

	c.sel = c:CreateTexture(nil, "ARTWORK")
	c.sel:SetTexture("Interface\\AchievementFrame\\UI-Achievement-Parchment-Highlight")
	c.sel:SetAllPoints(c)
	c.sel:SetBlendMode("ADD")
	c.sel:SetAlpha(0.9)
	c.sel:Hide()

	c.thumb = CreateFrame("Frame", nil, c)
	c.thumb:SetWidth(78)
	c.thumb:SetHeight(59)
	At(c.thumb, 9, 9, c)
	c.thumb.edge = c.thumb:CreateTexture(nil, "BACKGROUND")
	c.thumb.edge:SetTexture(20 / 255, 12 / 255, 4 / 255, 1)
	c.thumb.edge:SetAllPoints(c.thumb)
	c.thumb.art = c.thumb:CreateTexture(nil, "ARTWORK")
	c.thumb.art:SetWidth(76)
	c.thumb.art:SetHeight(57)
	At(c.thumb.art, 1, 1, c.thumb)

	c.arena = c:CreateFontString(nil, "OVERLAY")
	c.arena:SetFontObject(fontCardTitle)
	c.arena:SetPoint("LEFT", c, "TOPLEFT", 95, -20)
	c.arena:SetJustifyH("LEFT")

	c.line1 = c:CreateFontString(nil, "OVERLAY")
	c.line1:SetFontObject(fontInk10)
	c.line1:SetPoint("LEFT", c, "TOPLEFT", 95, -38)
	c.line1:SetJustifyH("LEFT")

	c.line2 = c:CreateFontString(nil, "OVERLAY")
	c.line2:SetFontObject(fontInk9)
	c.line2:SetPoint("LEFT", c, "TOPLEFT", 95, -52)
	c.line2:SetJustifyH("LEFT")

	-- The star is the favorite toggle as well as the mark.
	c.star = CreateFrame("Button", nil, c)
	c.star:SetWidth(16)
	c.star:SetHeight(16)
	At(c.star, CW - 25, 8, c)
	c.star.tex = c.star:CreateTexture(nil, "ARTWORK")
	c.star.tex:SetTexture("Interface\\TargetingFrame\\UI-RaidTargetingIcon_1")
	c.star.tex:SetAllPoints(c.star)
	c.star:SetScript("OnClick", function(self)
		local row = self:GetParent().row
		if row then
			ToggleFavorite(row.id, not row.fav)
		end
	end)
	c.star:SetScript("OnEnter", function(self)
		local row = self:GetParent().row
		GameTooltip:SetOwner(self, "ANCHOR_RIGHT")
		GameTooltip:SetText(row and row.fav and "Remove from your favorites" or "Keep in your favorites")
		GameTooltip:Show()
	end)
	c.star:SetScript("OnLeave", function()
		GameTooltip:Hide()
	end)

	c.rule = c:CreateTexture(nil, "OVERLAY")
	c.rule:SetTexture("Interface\\Buttons\\WHITE8X8")
	c.rule:SetVertexColor(INK[1], INK[2], INK[3], 0.27)
	c.rule:SetWidth(CW - 20)
	c.rule:SetHeight(1)
	At(c.rule, 10, 76, c)

	c.tick = c:CreateTexture(nil, "OVERLAY")
	c.tick:SetTexture("Interface\\RaidFrame\\ReadyCheck-Ready")
	c.tick:SetWidth(14)
	c.tick:SetHeight(14)
	At(c.tick, 9, 91 - 7, c)

	c.sides = {}
	for k = 1, 2 do
		local ry = 91 + (k - 1) * 21
		local side = { circles = {} }
		for j = 1, 5 do
			local t = c:CreateTexture(nil, "ARTWORK")
			t:SetWidth(15)
			t:SetHeight(15)
			At(t, 26 + (j - 1) * 16, ry - 7.5, c)
			side.circles[j] = t
		end
		side.name = c:CreateFontString(nil, "OVERLAY")
		side.name:SetFontObject(fontInk11)
		side.name:SetJustifyH("LEFT")
		side.rating = c:CreateFontString(nil, "OVERLAY")
		side.rating:SetFontObject(k == 1 and fontWin or fontLoss)
		side.rating:SetPoint("RIGHT", c, "TOPLEFT", CW - 10, -ry)
		side.y = ry
		c.sides[k] = side
	end

	c:SetHighlightTexture("Interface\\AchievementFrame\\UI-Achievement-Parchment-Highlight", "ADD")
	c:GetHighlightTexture():SetAlpha(0.5)
	c:SetScript("OnClick", function(self)
		if self.row then
			PlaySound("igMainMenuOptionCheckBoxOn")
			state.selected = self.row.id
			DrawReplays()
		end
	end)
	c:SetScript("OnDoubleClick", function(self)
		if self.row then
			Send("REPLAYS:WATCH:" .. self.row.id)
		end
	end)
	c:Hide()
	cards[i] = c
end

local replaysMessage = replaysFrame:CreateFontString(nil, "OVERLAY", "GameFontHighlight")
replaysMessage:SetPoint("CENTER", replaysFrame, "TOPLEFT", AW / 2, -(AH / 2 - 20))
replaysMessage:SetWidth(AW - 120)

local function PageButton(direction, x)
	local b = CreateFrame("Button", nil, replaysFrame)
	b:SetWidth(30)
	b:SetHeight(30)
	At(b, x, footY - 4, replaysFrame)
	local base = "Interface\\Buttons\\UI-SpellbookIcon-" .. direction .. "Page-"
	b:SetNormalTexture(base .. "Up")
	b:SetPushedTexture(base .. "Down")
	b:SetDisabledTexture(base .. "Disabled")
	b:SetHighlightTexture("Interface\\Buttons\\UI-Common-MouseHilight", "ADD")
	return b
end
local prevPage = PageButton("Prev", 12)
local nextPage = PageButton("Next", 118)
local pageText = replaysFrame:CreateFontString(nil, "OVERLAY", "GameFontNormal")
pageText:SetPoint("CENTER", replaysFrame, "TOPLEFT", 80, -(footY + 11))

local replayStatus = replaysFrame:CreateFontString(nil, "OVERLAY", "GameFontNormalSmall")
replayStatus:SetPoint("LEFT", replaysFrame, "TOPLEFT", 160, -(footY + 11))
replayStatus:SetJustifyH("LEFT")
replayStatus:SetTextColor(1, 0.3, 0.3)

local favButton = CreateFrame("Button", nil, replaysFrame, "UIPanelButtonTemplate")
favButton:SetWidth(110)
favButton:SetHeight(22)
At(favButton, AW - 14 - 110 - 6 - 110, footY, replaysFrame)
local watchButton = CreateFrame("Button", nil, replaysFrame, "UIPanelButtonTemplate")
watchButton:SetWidth(110)
watchButton:SetHeight(22)
At(watchButton, AW - 14 - 110, footY, replaysFrame)
watchButton:SetText("Watch")

------------------------------------------------------------------
-- the group you would queue with
------------------------------------------------------------------
local function GroupNames()
	local names = {}
	local raid = GetNumRaidMembers() or 0
	if raid > 0 then
		local me = UnitName("player")
		for i = 1, raid do
			local name = UnitName("raid" .. i)
			if name and name ~= me then
				table.insert(names, name)
			end
		end
	else
		for i = 1, GetNumPartyMembers() or 0 do
			local name = UnitName("party" .. i)
			if name then
				table.insert(names, name)
			end
		end
	end
	return names
end

local function GroupLeader()
	if (GetNumRaidMembers() or 0) > 0 then
		return IsRaidLeader() and true or false
	end
	return IsPartyLeader() and true or false
end

-- "you and Claude", "you, Claude and Elgrom"
local function GroupText(names)
	if #names == 0 then
		return "You are not in a group."
	end
	local list = { "you" }
	for _, n in ipairs(names) do
		table.insert(list, n)
	end
	local last = table.remove(list)
	return "Your group: " .. table.concat(list, ", ") .. " and " .. last
end

-- Whether a group join can work for a queue taking up to `limit` players,
-- and the line that says why not.
local function GroupJoin(limit)
	local names = GroupNames()
	if #names == 0 then
		return false, "You are not in a group."
	end
	if not GroupLeader() then
		return false, "Only your group leader can queue the group."
	end
	if #names + 1 > limit then
		return false, string.format("Your group is too big: %d of you, %d at most.", #names + 1, limit)
	end
	return true, GroupText(names)
end

------------------------------------------------------------------
-- arena teams
------------------------------------------------------------------
local MAX_TEAMS = 4

-- { index =, name =, size =, rating =, played =, wins = } for every team the
-- character is on, smallest first.
local function ArenaTeams()
	local teams = {}
	for i = 1, MAX_TEAMS do
		local name, size, rating, played, wins = GetArenaTeam(i)
		if name and size then
			table.insert(teams, { index = i, name = name, size = size, rating = rating or 0, played = played or 0,
				wins = wins or 0 })
		end
	end
	table.sort(teams, function(a, b) return a.size < b.size end)
	return teams
end

local function TeamOfSize(size)
	for _, t in ipairs(ArenaTeams()) do
		if t.size == size then
			return t
		end
	end
	return nil
end

local rosterAsked = {}

local function Roster(team)
	if not rosterAsked[team.index] then
		rosterAsked[team.index] = true
		ArenaTeamRoster(team.index)
	end
	local people = {}
	local n = GetNumArenaTeamMembers(team.index, 1) or 0
	for k = 1, math.min(n, MAX_PORTRAITS) do
		local name, _, _, class, _, _, _, _, _, rating = GetArenaTeamRosterInfo(team.index, k)
		if name then
			table.insert(people, { token = CLASS_BY_NAME[class or ""], name = name,
				sub = rating and tostring(rating) or "" })
		end
	end
	return people
end

------------------------------------------------------------------
-- the pages
------------------------------------------------------------------
local function TeamsLabel(size, teams)
	local parts = {}
	for _ = 1, math.max(2, teams) do
		table.insert(parts, tostring(size))
	end
	return table.concat(parts, "v")
end

-- Each queue page: its rows, and what one row shows.
local PAGES = {}

local function Join(mode, size, group)
	PlaySound("igMainMenuOptionCheckBoxOn")
	Send(string.format("ARENAMASTER:JOIN:%s:%d:%d", mode, size, group and 1 or 0))
	am:Hide()
end

PAGES.S = {
	rows = function()
		local rows = {}
		for size = 2, 5 do
			table.insert(rows, { label = size .. "v" .. size .. " Skirmish", size = size })
		end
		return rows
	end,
	draw = function(row)
		local size = row.size
		return {
			banner = SCREENS .. "LoadScreenNagrandArenaBattlegrounds", cy = 0.42,
			icon = MODE.S.icon,
			title = row.label,
			sub = "Practice match  -  no rating at stake",
			body = string.format("Fight for honor, not rating. Queue alone or bring up to %s; the queue fills any empty places on your team. A loss costs you nothing.",
				size == 2 and "a friend" or (size - 1) .. " friends"),
			facts = { { "Team", size .. " players" }, { "Arena", "Random" }, { "Reward", "Honor" } },
			pool = true,
			join = { mode = "S", size = size, limit = size },
		}
	end,
}

PAGES.R = {
	rows = function()
		local rows = {}
		for _, t in ipairs(ArenaTeams()) do
			table.insert(rows, { label = t.size .. "v" .. t.size .. " Team", count = t.rating, team = t, size = t.size })
		end
		return rows
	end,
	draw = function(row)
		if not row then
			return {
				banner = SCREENS .. "LoadScreenBladesEdgeArena", cy = 0.45,
				icon = MODE.R.icon,
				title = "Rated Arena",
				sub = "Arena teams only",
				message = "You are not on an arena team.\n\nRated arenas are fought by arena teams, and every team you join is listed here with its rating and roster.",
				join = nil,
			}
		end
		local t = row.team
		local people = Roster(t)
		return {
			banner = SCREENS .. "LoadScreenBladesEdgeArena", cy = 0.45,
			icon = TEAM_ICON[t.size] or MODE.R.icon,
			title = t.name,
			small = true,
			sub = string.format("%dv%d arena team  -  rating %d  -  %d played this week", t.size, t.size, t.rating, t.played),
			body = string.format("Your team's rating rises and falls with every match. Queue with your team: any %s of its members can fight.",
				t.size == 2 and "two" or (t.size == 3 and "three" or (t.size == 4 and "four" or "five"))),
			heading = "Roster",
			people = people,
			join = { mode = "R", size = t.size, limit = t.size, rated = true },
		}
	end,
}

PAGES.M = {
	rows = function()
		local rows = {}
		for size = 2, 5 do
			table.insert(rows, { label = TeamsLabel(size, state.squads), size = size })
		end
		return rows
	end,
	draw = function(row)
		local size = row.size
		local team = TeamOfSize(size)
		return {
			banner = SCREENS .. "LoadScreenRuinsofLordaeronBattlegrounds", cy = 0.5,
			icon = MODE.M.icon,
			title = "Four Corners",
			sub = string.format("Up to %d teams of %d  -  Baradin Hold", state.squads, size),
			body = string.format("Up to %d teams, one in each corner of Baradin Hold. Every other team is your enemy, and the last team standing wins. Fight a skirmish, or bring your %dv%d arena team for rating.",
				state.squads, size, size),
			facts = { { "Teams", "Up to " .. state.squads }, { "Team size", size .. " players" },
				{ "Rated", team and team.name or "No team" } },
			join = { mode = "MS", size = size, limit = size, ratedMode = "MR", team = team },
		}
	end,
}

PAGES.B = {
	rows = function()
		return { { label = "2v2 Buffle Shuffle", size = 2 }, { label = "3v3 Buffle Shuffle", size = 3 } }
	end,
	draw = function(row)
		local size = row.size
		local rounds = size == 3 and 6 or 3
		return {
			banner = SCREENS .. "LoadScreenDalaranSewersArena", cy = 0.45,
			icon = MODE.B.icon,
			title = row.label,
			small = true,
			sub = "Solo or grouped  -  new teams every round",
			body = string.format("The teams are dealt again before every round: %d rounds of %dv%d. Win more rounds than you lose. A group of up to %d lands in the same match and is dealt into the teams like anyone else.",
				rounds, size, size, size * 2),
			facts = { { "Rounds", tostring(rounds) }, { "Players", tostring(size * 2) }, { "Reward", "Honor" } },
			join = { mode = "B", size = size, limit = size * 2 },
		}
	end,
}

PAGES.A = {
	rows = function()
		local rows = {}
		for size = 1, 5 do
			table.insert(rows, { label = size .. "v" .. size .. " All-Star", size = size })
		end
		return rows
	end,
	draw = function(row)
		local size = row.size
		local people, sub
		if state.lineup then
			people = {}
			for _, life in ipairs(state.lineup) do
				local token = CLASS_TOKEN[life.class]
				if life.name ~= "" then
					table.insert(people, { token = token, name = life.name, sub = ClassName(life.class) })
				else
					table.insert(people, { token = token, name = ClassName(life.class), sub = "Standard" })
				end
			end
			sub = state.lineup.shuffled and "shuffled every match" or nil
		end
		return {
			banner = SCREENS .. "LoadScreenOrgrimmarArena", cy = 0.5,
			icon = MODE.A.icon,
			title = row.label,
			sub = "Nine lives, one class each",
			body = "Each death brings you back where you fell as your next character: their gear, talents, keybindings and macros. A class you have no character for comes in its standard kit - the tournament spec and gear. The last team with lives left wins.",
			heading = "Your lineup",
			headingSub = sub,
			people = people,
			lineup = true,
			message = not people and "Reading your characters..." or nil,
			join = { mode = "A", size = size, limit = size },
		}
	end,
}

local function CurrentRows()
	local page = PAGES[state.mode]
	return page and page.rows() or {}
end

local function DrawPoster(p)
	SetScreen(banner, p.banner, BW, BH, p.cy, 1)
	emblem:SetTexture(p.icon)
	posterTitle:SetFontObject(p.small and fontTitleSm or fontTitle)
	FitText(posterTitle, p.title or "", PW - 110, p.small and fontTitleSm or fontTitle, fontTitleSm, fontHeading)
	posterSub:SetText(p.sub or "")

	body:SetText(p.body or "")
	if p.body then body:Show() else body:Hide() end

	local y = 262
	if p.facts then
		SetFacts(y, p.facts)
		y = y + 44
	else
		SetFacts(y, nil)
	end

	HidePortraits()
	HideThumbs()
	heading:Hide()
	subheading:Hide()
	if p.heading then
		SetHeading(p.facts and y or 268, p.heading, p.headingSub)
		local top = (p.facts and y or 268) + (p.headingSub and 25 or 16)
		if p.people and #p.people > 0 then
			if p.lineup then
				SetPortraits(8, top, PW - 16, p.people, 42)
			else
				local w = math.min(PW - 40, 120 * #p.people)
				SetPortraits((PW - w) / 2, top, w, p.people, 44)
			end
		end
	end

	if p.pool then
		if state.pool and #state.pool > 0 then
			SetHeading(y + 2, "Arenas in rotation")
			SetThumbs(y + 14, state.pool)
		end
	end

	if p.message then
		posterMessage:ClearAllPoints()
		if p.body then
			posterMessage:SetPoint("TOP", poster, "TOPLEFT", PW / 2, -(p.heading and 300 or 270))
		else
			posterMessage:SetPoint("TOP", poster, "TOPLEFT", PW / 2, -214)
		end
		posterMessage:SetText(p.message)
		posterMessage:Show()
	else
		posterMessage:Hide()
	end
end

local function DrawFoot(p)
	local j = p and p.join
	if not j then
		status:SetText("")
		SetFootButtons({})
		return
	end
	local canGroup, why = GroupJoin(j.limit)
	if j.rated then
		-- A rated match is always queued by the team's group.
		status:SetText(why)
		SetFootButtons({ { "Join Battle", 120, canGroup, function() Join(j.mode, j.size, true) end } })
		return
	end
	local buttons = {}
	if j.ratedMode then
		local ratedOk = canGroup and j.team ~= nil
		table.insert(buttons, { "Join Rated", 110, ratedOk, function() Join(j.ratedMode, j.size, true) end })
		if not j.team then
			why = string.format("You need a %dv%d arena team to play it rated.", j.size, j.size)
		end
	end
	local width = j.ratedMode and 110 or 120
	if j.limit > 1 then
		table.insert(buttons, { "Join as Group", width, canGroup, function() Join(j.mode, j.size, true) end })
	end
	table.insert(buttons, { "Join Battle", width, true, function() Join(j.mode, j.size, false) end })
	status:SetText(why)
	SetFootButtons(buttons)
end

------------------------------------------------------------------
-- the replays page
------------------------------------------------------------------
local function Ago(seconds)
	seconds = tonumber(seconds) or 0
	if seconds < 3600 then
		local m = math.max(1, math.floor(seconds / 60))
		return m == 1 and "a minute ago" or m .. " minutes ago"
	end
	if seconds < 86400 then
		local h = math.floor(seconds / 3600)
		return h == 1 and "an hour ago" or h .. " hours ago"
	end
	local d = math.floor(seconds / 86400)
	return d == 1 and "yesterday" or d .. " days ago"
end

local function Watched(n)
	n = tonumber(n) or 0
	if n == 0 then
		return "not watched yet"
	end
	return n == 1 and "watched once" or "watched " .. n .. " times"
end

local SPECIAL_KIND = { [887] = "Four Corners", [889] = "Buffle Shuffle", [890] = "All-Star" }

local function KindText(row)
	local size = row.arenaType .. "v" .. row.arenaType
	local kind = SPECIAL_KIND[row.typeId] or (row.rated and "Rated" or "Skirmish")
	if row.typeId == 887 then
		size = TeamsLabel(row.arenaType, 4)
	end
	return size .. " " .. kind .. "  -  Match " .. row.id
end

local function SideText(side)
	if side.team and side.team ~= "" then
		return side.team
	end
	local names = {}
	for _, p in ipairs(side.players or {}) do
		table.insert(names, p.name)
	end
	return table.concat(names, ", ")
end

local function FindRow(id)
	for _, list in pairs(state.replays) do
		for _, row in ipairs(list or {}) do
			if row.id == id then
				return row
			end
		end
	end
	return nil
end

local function DrawCard(c, row)
	c.row = row
	local name, screen = MapInfo(row.mapId)
	SetScreen(c.thumb.art, screen, 76, 57, 0.5, 0.8)
	FitText(c.arena, name, CW - 95 - 30, fontCardTitle, fontCardSmall)
	FitText(c.line1, KindText(row), CW - 95 - 8, fontInk10, fontInk9)
	FitText(c.line2, Ago(row.age) .. "  -  " .. Watched(row.watched), CW - 95 - 8, fontInk9)
	c.star.tex:SetDesaturated(not row.fav)
	c.star.tex:SetAlpha(row.fav and 1 or 0.35)
	if row.id == state.selected then c.sel:Show() else c.sel:Hide() end

	for k, key in ipairs({ "W", "L" }) do
		local side = row.sides[key]
		local s = c.sides[k]
		local players = side.players or {}
		for j, t in ipairs(s.circles) do
			local p = players[j]
			if p then
				SetClassIcon(t, CLASS_TOKEN[p.class])
			else
				t:Hide()
			end
		end
		local x = 26 + math.min(#players, 5) * 16 + 5
		s.name:ClearAllPoints()
		s.name:SetPoint("LEFT", c, "TOPLEFT", x, -s.y)
		local ratingText = ""
		if row.rated then
			ratingText = tostring(key == "W" and row.wRating or row.lRating)
		end
		s.rating:SetText(ratingText)
		local room = CW - 10 - x - (ratingText ~= "" and (s.rating:GetStringWidth() + 8) or 0)
		if not FitText(s.name, SideText(side), room, fontInk11, fontInk10, fontInk9) then
			-- Still too long: the first names that fit, then "...".
			local text = SideText(side)
			while string.len(text) > 4 and s.name:GetStringWidth() > room do
				text = string.sub(text, 1, string.len(text) - 2)
				s.name:SetText(text .. "...")
			end
		end
	end
	c:Show()
end

function DrawReplays()
	local list = state.list or "RECENT"
	local rows = state.replays[list]
	for _, c in ipairs(cards) do
		c:Hide()
		c.row = nil
	end
	replayStatus:SetText(state.replayError or "")
	if not rows then
		replaysMessage:SetText("Reading the replays...")
		replaysMessage:Show()
		pageText:SetText("")
		prevPage:Disable()
		nextPage:Disable()
		favButton:Disable()
		watchButton:Disable()
		return
	end
	local pages = math.max(1, math.ceil(#rows / CARDS))
	state.page = math.min(math.max(1, state.page or 1), pages)
	if #rows == 0 then
		replaysMessage:SetText(list == "FAV" and "No favorites yet.\n\nStar a match to keep it here."
			or "No matches have been recorded yet.")
		replaysMessage:Show()
	else
		replaysMessage:Hide()
	end
	local first = (state.page - 1) * CARDS
	for i, c in ipairs(cards) do
		local row = rows[first + i]
		if row then
			DrawCard(c, row)
		end
	end
	pageText:SetText(#rows > 0 and string.format("Page %d of %d", state.page, pages) or "")
	if state.page > 1 then prevPage:Enable() else prevPage:Disable() end
	if state.page < pages then nextPage:Enable() else nextPage:Disable() end

	local selected = state.selected and FindRow(state.selected)
	if selected then
		favButton:SetText(selected.fav and "Unfavorite" or "Favorite")
		favButton:Enable()
		watchButton:Enable()
	else
		favButton:SetText("Favorite")
		favButton:Disable()
		watchButton:Disable()
	end
end

prevPage:SetScript("OnClick", function()
	PlaySound("igAbiliityPageTurn")
	state.page = state.page - 1
	DrawReplays()
end)
nextPage:SetScript("OnClick", function()
	PlaySound("igAbiliityPageTurn")
	state.page = state.page + 1
	DrawReplays()
end)
favButton:SetScript("OnClick", function()
	local row = state.selected and FindRow(state.selected)
	if row then
		ToggleFavorite(row.id, not row.fav)
	end
end)
watchButton:SetScript("OnClick", function()
	if state.selected then
		PlaySound("igMainMenuOptionCheckBoxOn")
		Send("REPLAYS:WATCH:" .. state.selected)
	end
end)

function RequestReplays(list)
	state.list = list
	state.replayError = nil
	Send("REPLAYS:LIST:" .. list)
end

------------------------------------------------------------------
-- drawing it all
------------------------------------------------------------------
local function ShowAll(list, show)
	for _, region in ipairs(list) do
		if show then region:Show() else region:Hide() end
	end
end

function Draw()
	local mode = MODE[state.mode]
	ddIcon:SetTexture(mode.icon)
	UIDropDownMenu_SetText(dd, mode.name)

	local replays = state.mode == "P"
	local rows
	if replays then
		rows = {
			{ label = "Recent", list = "RECENT", count = state.counts.RECENT },
			{ label = "Favorites", list = "FAV", count = state.counts.FAV },
		}
		state.bracket.P = (state.list == "FAV") and 2 or 1
	else
		rows = CurrentRows()
	end
	local sel = math.min(state.bracket[state.mode] or 1, math.max(1, #rows))
	for i, b in ipairs(categoryButtons) do
		local row = rows[i]
		if row then
			b.row = i
			b.label:SetText(row.label)
			b.count:SetText(row.count and tostring(row.count) or "")
			if i == sel then
				b.sel:Show()
				b.label:SetFontObject(GameFontHighlight)
			else
				b.sel:Hide()
				b.label:SetFontObject(GameFontNormal)
			end
			b:Show()
		else
			b:Hide()
		end
	end

	ShowAll(replayLeft, replays)
	if replays or state.mode == "R" then
		bots:Hide()
	else
		bots:Show()
		bots:SetChecked(state.bots)
	end

	if replays then
		plateText:SetText("Replays")
		poster:Hide()
		status:SetText("")
		SetFootButtons({})
		replaysFrame:Show()
		DrawReplays()
		return
	end

	replaysFrame:Hide()
	poster:Show()
	local row = rows[sel]
	local p = PAGES[state.mode].draw(row)
	plateText:SetText(row and row.label or mode.name)
	DrawPoster(p)
	DrawFoot(p)
end

function SetMode(key)
	if not MODE[key] or not ModeAvailable(key) then
		return
	end
	state.mode = key
	CloseDropDownMenus()
	if key == "P" and not state.replays[state.list or "RECENT"] then
		RequestReplays(state.list or "RECENT")
	end
	Draw()
end

function SetBracket(row)
	if state.mode == "P" then
		local list = row == 2 and "FAV" or "RECENT"
		state.page = 1
		RequestReplays(list)
		state.replays[list] = nil
	else
		state.bracket[state.mode] = row
	end
	Draw()
end

------------------------------------------------------------------
-- opening: the stock arena window is put away for this one
------------------------------------------------------------------
local hideStock = false

local function FirstAvailable()
	if ModeAvailable(state.mode) then
		return state.mode
	end
	for _, m in ipairs(MODES) do
		if ModeAvailable(m.key) then
			return m.key
		end
	end
	return state.mode
end

local function Open()
	state.mode = FirstAvailable()
	state.replays = { RECENT = nil, FAV = nil }
	state.selected = nil
	state.page = 1
	rosterAsked = {}
	Send("ARENABOTS")
	if state.mode == "P" then
		RequestReplays(state.list or "RECENT")
	end
	PlaySound("igCharacterInfoOpen")
	am:Show()
	Draw()
end

am:SetScript("OnHide", function()
	state.pending = false
	CloseDropDownMenus()
	PlaySound("igCharacterInfoClose")
end)

-- Put away the stock window on the frame after it showed: hiding it from
-- inside its own ShowUIPanel would leave the panel layout half done.
am:SetScript("OnUpdate", function()
	if hideStock then
		hideStock = false
		if ArenaFrame and ArenaFrame:IsShown() then
			HideUIPanel(ArenaFrame)
		end
		if ArenaFrame then
			ArenaFrame:SetAlpha(1)
		end
	end
end)

if ArenaFrame then
	ArenaFrame:HookScript("OnShow", function(self)
		if not state.pending then
			return
		end
		state.pending = false
		self:SetAlpha(0)
		hideStock = true
		Open()
	end)
end

------------------------------------------------------------------
-- the server's side
------------------------------------------------------------------
local function OnLineup(shuffled, list)
	local lineup = { shuffled = shuffled == "1" }
	for _, entry in ipairs(Split(list, ";")) do
		local parts = Split(entry, ",")
		local class = tonumber(parts[1])
		if class then
			table.insert(lineup, { class = class, name = parts[2] or "" })
		end
	end
	state.lineup = lineup
end

local function OnReplayLine(parts)
	local what = parts[2]
	if what == "BEGIN" then
		state.loading[parts[3]] = {}
		state.receiving = parts[3]
		return
	end
	if what == "END" then
		local list = parts[3]
		state.replays[list] = state.loading[list] or {}
		state.loading[list] = nil
		state.counts[list] = tonumber(parts[4]) or #state.replays[list]
		if am:IsShown() and state.mode == "P" then
			Draw()
		end
		return
	end
	if what == "R" then
		local id = tonumber(parts[3])
		local into = state.receiving and state.loading[state.receiving]
		if not id or not into then
			return
		end
		table.insert(into, {
			id = id,
			arenaType = tonumber(parts[4]) or 0,
			typeId = tonumber(parts[5]) or 0,
			mapId = tonumber(parts[6]) or 0,
			age = tonumber(parts[7]) or 0,
			watched = tonumber(parts[8]) or 0,
			fav = parts[9] == "1",
			rated = parts[10] == "1",
			wRating = tonumber(parts[11]) or 0,
			lRating = tonumber(parts[12]) or 0,
			sides = { W = {}, L = {} },
		})
		return
	end
	if what == "T" or what == "P" then
		local id = tonumber(parts[3])
		local side = parts[4]
		local row
		for _, r in ipairs(state.receiving and state.loading[state.receiving] or {}) do
			if r.id == id then
				row = r
			end
		end
		if not row or not row.sides[side] then
			return
		end
		-- The rest of the line is the value, colons and all.
		local value = table.concat(parts, ":", 5)
		if what == "T" then
			row.sides[side].team = value
		else
			local players = {}
			for _, p in ipairs(Split(value, ",")) do
				local class, name = string.match(p, "^(%d+)%.(.*)$")
				if class then
					table.insert(players, { class = tonumber(class), name = name })
				end
			end
			row.sides[side].players = players
		end
		return
	end
	if what == "FAV" then
		local id = tonumber(parts[3])
		local on = parts[4] == "1"
		for _, rows in pairs(state.replays) do
			for _, r in ipairs(rows or {}) do
				if r.id == id then
					r.fav = on
				end
			end
		end
		-- The favorites list itself is read again: a match was added or taken off.
		state.replays.FAV = nil
		state.counts.FAV = nil
		if state.list == "FAV" then
			RequestReplays("FAV")
		end
		if am:IsShown() and state.mode == "P" then
			Draw()
		end
		return
	end
	if what == "ERR" then
		state.replayError = table.concat(parts, ":", 3)
		if am:IsShown() and state.mode == "P" then
			DrawReplays()
		end
		if UIErrorsFrame then
			UIErrorsFrame:AddMessage(state.replayError, 1, 0.1, 0.1)
		end
	end
end

local function OnServer(message)
	if message == "ARENAQUEUE:MASTER" then
		state.pending = true
		return
	end
	if string.sub(message, 1, 11) == "ARENAQUEUE:" then
		state.pending = false
		return
	end
	local parts = Split(message, ":")
	local head = parts[1]
	if head == "AM" then
		local what = parts[2]
		if what == "MODES" then
			local keys = { "R", "S", "M", "B", "A", "P" }
			for i, key in ipairs(keys) do
				state.modes[key] = parts[2 + i] ~= "0"
			end
			state.squads = tonumber(parts[9]) or state.squads
		elseif what == "LINEUP" then
			OnLineup(parts[3], table.concat(parts, ":", 4))
		elseif what == "POOL" then
			local pool = {}
			for _, id in ipairs(Split(parts[3] or "", ",")) do
				if tonumber(id) then
					table.insert(pool, tonumber(id))
				end
			end
			state.pool = pool
		end
		if am:IsShown() then
			Draw()
		end
		return
	end
	if head == "RPL" then
		OnReplayLine(parts)
		return
	end
	if head == "ARENABOTS" then
		state.bots = parts[2] == "1"
		bots:SetChecked(state.bots)
	end
end

local listener = CreateFrame("Frame")
listener:RegisterEvent("CHAT_MSG_ADDON")
listener:RegisterEvent("PARTY_MEMBERS_CHANGED")
listener:RegisterEvent("RAID_ROSTER_UPDATE")
listener:RegisterEvent("PARTY_LEADER_CHANGED")
listener:RegisterEvent("ARENA_TEAM_UPDATE")
listener:RegisterEvent("ARENA_TEAM_ROSTER_UPDATE")
listener:RegisterEvent("PLAYER_ENTERING_WORLD")
listener:SetScript("OnEvent", function(self, event, prefix, message)
	if event == "CHAT_MSG_ADDON" then
		if prefix == "CCGAME" and message then
			OnServer(message)
		end
		return
	end
	if event == "PLAYER_ENTERING_WORLD" then
		state.pending = false
		am:Hide()
		return
	end
	if am:IsShown() and state.mode ~= "P" then
		Draw()
	end
end)
