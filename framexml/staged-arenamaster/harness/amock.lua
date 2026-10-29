-- Additions to wowmock.lua / wowmock_ext.lua for CENTURION_ArenaMaster: the
-- stock templates it builds on (UIDropDownMenuTemplate with the real
-- DropDownList1 geometry, UICheckButtonTemplate, InputBoxTemplate), the
-- arena team and party API, and ArenaFrame. Loaded after both.

local Region = getmetatable(UIParent)

for _, name in ipairs({ "SetSpacing", "LockHighlight", "UnlockHighlight", "SetFocus", "HighlightText",
	"SetTextInsets", "SetMultiLine", "SetDisabledFontObject", "SetNormalFontObject", "SetHighlightFontObject" }) do
	Region[name] = function() end
end

function Region:SetBackdropColor(r, g, b, a)
	self._backdropColor = { r, g, b, a or 1 }
end

function Region:SetChecked(c)
	self._checked = c and true or false
	if self._checkTex then
		self._checkTex._shown = self._checked
	end
end
function Region:GetChecked() return self._checked and 1 or nil end

-- An EditBox shows its text from the left, as the client draws one.
local baseSetText = Region.SetText
function Region:SetText(t)
	if self._kind == "EditBox" and not self._label then
		self._label = self:CreateFontString(nil, "OVERLAY", "GameFontHighlight")
		self._label:SetPoint("LEFT", self, "LEFT", 0, 0)
		self._label:SetJustifyH("LEFT")
	end
	return baseSetText(self, t)
end

local baseCreate = CreateFrame
local LABEL = "Interface\\Glues\\CharacterCreate\\CharacterCreate-LabelFrame"

function CreateFrame(kind, name, parent, template)
	local f = baseCreate(kind, name, parent, template)
	if template == "UIDropDownMenuTemplate" then
		f._w, f._h = 40, 32
		local l = f:CreateTexture(name .. "Left", "ARTWORK")
		l:SetTexture(LABEL); l:SetWidth(25); l:SetHeight(64)
		l:SetPoint("TOPLEFT", f, "TOPLEFT", 0, 17); l:SetTexCoord(0, 0.1953125, 0, 1)
		local m = f:CreateTexture(name .. "Middle", "ARTWORK")
		m:SetTexture(LABEL); m:SetWidth(115); m:SetHeight(64)
		m:SetPoint("LEFT", l, "RIGHT"); m:SetTexCoord(0.1953125, 0.8046875, 0, 1)
		local r = f:CreateTexture(name .. "Right", "ARTWORK")
		r:SetTexture(LABEL); r:SetWidth(25); r:SetHeight(64)
		r:SetPoint("LEFT", m, "RIGHT"); r:SetTexCoord(0.8046875, 1, 0, 1)
		local t = f:CreateFontString(name .. "Text", "ARTWORK", "GameFontHighlightSmall")
		t:SetHeight(10); t:SetJustifyH("RIGHT"); t:SetPoint("RIGHT", r, "RIGHT", -43, 2)
		local b = baseCreate("Button", name .. "Button", f)
		b._w, b._h = 24, 24
		b:SetPoint("TOPRIGHT", r, "TOPRIGHT", -16, -18)
		b:SetNormalTexture("Interface\\ChatFrame\\UI-ChatIcon-ScrollDown-Up")
		b:SetScript("OnClick", function() ToggleDropDownMenu(nil, nil, f) end)
	elseif template == "UICheckButtonTemplate" then
		f._w, f._h = 32, 32
		f:SetNormalTexture("Interface\\Buttons\\UI-CheckBox-Up")
		local c = f:CreateTexture(nil, "OVERLAY")
		c:SetTexture("Interface\\Buttons\\UI-CheckBox-Check")
		c:SetAllPoints(f)
		c._shown = false
		f._checkTex = c
		f:SetHighlightTexture("Interface\\Buttons\\UI-CheckBox-Highlight", "ADD")
		local base = f._scripts
		f.Click = function(self)
			self:SetChecked(not self._checked)
			if self._scripts.OnClick then self._scripts.OnClick(self, "LeftButton") end
		end
	elseif template == "InputBoxTemplate" then
		local file = "Interface\\Common\\Common-Input-Border"
		local l = f:CreateTexture(nil, "BACKGROUND")
		l:SetTexture(file); l:SetWidth(8); l:SetHeight(20)
		l:SetPoint("LEFT", f, "LEFT", -5, 0); l:SetTexCoord(0, 0.0625, 0, 0.625)
		local r = f:CreateTexture(nil, "BACKGROUND")
		r:SetTexture(file); r:SetWidth(8); r:SetHeight(20)
		r:SetPoint("RIGHT", f, "RIGHT", 0, 0); r:SetTexCoord(0.9375, 1, 0, 0.625)
		local m = f:CreateTexture(nil, "BACKGROUND")
		m:SetTexture(file); m:SetHeight(20)
		m:SetPoint("LEFT", l, "RIGHT"); m:SetPoint("RIGHT", r, "LEFT"); m:SetTexCoord(0.0625, 0.9375, 0, 0.625)
	end
	return f
end

------------------------------------------------------------------ dropdowns, as 3.3.5 lays them out
function UIDropDownMenu_SetWidth(frame, width, padding)
	_G[frame:GetName() .. "Middle"]:SetWidth(width)
	frame:SetWidth(width + 50)
	_G[frame:GetName() .. "Text"]:SetWidth(width - 25)
end
function UIDropDownMenu_SetText(frame, text) _G[frame:GetName() .. "Text"]:SetText(text) end
function UIDropDownMenu_Initialize(frame, fn) frame._init = fn end
function UIDropDownMenu_CreateInfo() return {} end
function UIDropDownMenu_AddButton(info) table.insert(MOCK.menu, info) end

local list = baseCreate("Frame", "DropDownList1", UIParent)
list._shown = false
list._level = 50
list:SetBackdrop({ bgFile = "Interface\\DialogFrame\\UI-DialogBox-Background-Dark",
	edgeFile = "Interface\\DialogFrame\\UI-DialogBox-Border", tile = true, tileSize = 32, edgeSize = 32,
	insets = { left = 11, right = 12, top = 12, bottom = 9 } })
MOCK.listButtons = {}

function CloseDropDownMenus() list:Hide() end

function ToggleDropDownMenu(level, value, frame)
	if list._shown then
		list:Hide()
		return
	end
	MOCK.menu = {}
	frame._init(frame, 1)
	for _, b in ipairs(MOCK.listButtons) do b._shown = false end
	local maxWidth = 0
	for i, info in ipairs(MOCK.menu) do
		local b = MOCK.listButtons[i]
		if not b then
			b = baseCreate("Button", "DropDownList1Button" .. i, list)
			b._h = 16
			b.check = b:CreateTexture(nil, "ARTWORK")
			b.check:SetTexture("Interface\\Buttons\\UI-CheckBox-Check")
			b.check:SetWidth(18); b.check:SetHeight(18)
			b.check:SetPoint("LEFT", b, "LEFT", 0, 0)
			b.icon = b:CreateTexture(nil, "ARTWORK")
			b.icon:SetWidth(16); b.icon:SetHeight(16)
			b.icon:SetPoint("RIGHT", b, "RIGHT", 0, 0)
			b.text = b:CreateFontString(nil, "ARTWORK")
			b.hl = b:CreateTexture(nil, "HIGHLIGHT")
			b.hl:SetTexture("Interface\\QuestFrame\\UI-QuestTitleHighlight")
			b.hl:SetBlendMode("ADD")
			b.hl:SetAllPoints(b)
			MOCK.listButtons[i] = b
		end
		b._shown = true
		b.info = info
		local xPos = info.notCheckable and 15 or 17
		b:ClearAllPoints()
		b:SetPoint("TOPLEFT", list, "TOPLEFT", xPos, -((i - 1) * 16) - 15)
		b.text:ClearAllPoints()
		b.text:SetPoint("LEFT", b, "LEFT", info.notCheckable and 0 or 20, 0)
		b.text:SetText(info.text)
		if info.isTitle then
			b.text:SetFontObject(GameFontNormalSmall)
		elseif info.disabled then
			b.text:SetFontObject(GameFontDisableSmall)
		else
			b.text:SetFontObject(GameFontHighlightSmall)
		end
		b.check._shown = info.checked and true or false
		if info.icon then
			b.icon:SetTexture(info.icon)
			if info.tCoordLeft then
				b.icon:SetTexCoord(info.tCoordLeft, info.tCoordRight, info.tCoordTop, info.tCoordBottom)
			end
			b.icon:SetDesaturated(info.disabled and true or false)
			b.icon._shown = true
		else
			b.icon._shown = false
		end
		local width = b.text:GetStringWidth() + 40 + (info.icon and 10 or 0) - (info.notCheckable and 30 or 0)
		if width > maxWidth then maxWidth = width end
	end
	for i, info in ipairs(MOCK.menu) do
		MOCK.listButtons[i]:SetWidth(maxWidth)
	end
	list:SetWidth(maxWidth + 25)
	list:SetHeight(#MOCK.menu * 16 + 30)
	list:ClearAllPoints()
	list:SetPoint("TOPLEFT", _G[frame:GetName() .. "Left"], "BOTTOMLEFT", 8, 22)
	list._shown = true
end

function MOCK.pickMenu(text)
	for _, b in ipairs(MOCK.listButtons) do
		if b._shown and b.info.text == text and not b.info.disabled then
			b.info.func()
			list:Hide()
			return true
		end
	end
	return false
end

-- GameFontDisableSmall: grey.
do
	local f = CreateFont("GameFontDisableSmall")
	f:SetFont("Fonts\\FRIZQT__.TTF", 10)
	f:SetTextColor(0.5, 0.5, 0.5)
	f:SetShadowColor(0, 0, 0, 1)
	f:SetShadowOffset(1, -1)
end

------------------------------------------------------------------ arena teams, the group
MOCK.teams = {}         -- index -> { name, size, rating, played, wins, roster = { {name, class, rating} } }
MOCK.party = {}         -- names of the other members
MOCK.raid = false
MOCK.leader = true
MOCK.rosterAsked = {}

function GetArenaTeam(i)
	local t = MOCK.teams[i]
	if not t then return nil end
	return t.name, t.size, t.rating, t.played, t.wins
end
function ArenaTeamRoster(i) MOCK.rosterAsked[i] = true end
function GetNumArenaTeamMembers(i) local t = MOCK.teams[i]; return t and #t.roster or 0 end
function GetArenaTeamRosterInfo(i, k)
	local m = MOCK.teams[i] and MOCK.teams[i].roster[k]
	if not m then return nil end
	return m[1], 0, 60, m[2], 1, 0, 0, 0, 0, m[3]
end

function GetNumPartyMembers() return MOCK.raid and 0 or #MOCK.party end
function GetNumRaidMembers() return MOCK.raid and (#MOCK.party + 1) or 0 end
function IsPartyLeader() return MOCK.leader end
function IsRaidLeader() return MOCK.leader end
function UnitName(unit)
	if unit == nil or unit == "player" then return "Baku" end
	local n = tonumber(string.match(unit, "^party(%d)$"))
	if n then return MOCK.party[n] end
	n = tonumber(string.match(unit, "^raid(%d+)$"))
	if n then
		if n == 1 then return "Baku" end
		return MOCK.party[n - 1]
	end
	return nil
end

LOCALIZED_CLASS_NAMES_FEMALE = LOCALIZED_CLASS_NAMES_MALE

------------------------------------------------------------------ the stock arena window
ArenaFrame = CreateFrame("Frame", "ArenaFrame", UIParent)
ArenaFrame._shown = false
function ShowUIPanel(f)
	f._shown = true
	if f._scripts.OnShow then f._scripts.OnShow(f) end
end
function HideUIPanel(f) f:Hide() end
