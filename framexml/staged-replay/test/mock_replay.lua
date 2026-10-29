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
	if arg ~= "?" and arg ~= "arm" and arg ~= "click" then
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
ChatFrame_AddMessageEventFilter = function(event, fn) MOCK.chatFilters = MOCK.chatFilters or {}; MOCK.chatFilters[event] = fn end
ERR_LEARN_SPELL_S = "You have learned a new spell: %s."
ERR_SPELL_UNLEARNED_S = "You have unlearned %s."
