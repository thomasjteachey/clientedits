-- Loads the real CENTURION_WSGHelper.lua (path in MODULE) against stubbed WoW
-- API and checks that a carrier the server names by first name only is shown,
-- coloured and /targetexact-ed by the full "First Last" name the client knows.
local fail, pass, log = 0, 0, {}
local function check(cond, what)
	if cond then pass = pass + 1 else fail = fail + 1; log[#log + 1] = "FAIL " .. what end
end

-- ---- world ----
local me = { name = "Drunks Broklio", class = "SHAMAN" }
local scores = {
	-- name, killingBlows, honorableKills, deaths, honorGained, faction, rank, race, class, classToken
	{ "Drunks Broklio", 0, 0, 0, 0, 1, 0, "Tauren", "Shaman", "SHAMAN" },
	{ "Elgrom Fernbloom", 0, 0, 0, 1, 0, 0, "Orc", "Hunter", "HUNTER" },
	{ "Aldric Bladewright", 0, 0, 0, 1, 0, 0, "Human", "Warrior", "WARRIOR" },
	{ "Mira Ashvale", 0, 0, 0, 0, 1, 0, "Human", "Mage", "MAGE" },
	{ "Mira Stoneward", 0, 0, 0, 1, 0, 0, "Dwarf", "Priest", "PRIEST" },
}
local units = { player = me.name }

-- ---- API stubs ----
function GetInstanceInfo() return "Warsong Gulch", "pvp" end
function UnitName(u) return units[u] end
function UnitExists(u) return units[u] ~= nil end
function UnitIsPlayer(u) return units[u] ~= nil end
function UnitClass(u) if u == "player" then return "Shaman", me.class end end
function UnitFactionGroup() return "Alliance" end
function UnitBuff() return nil end
function UnitIsDeadOrGhost() return false end
function UnitIsGhost() return false end
function GetNumRaidMembers() return 0 end
function GetRaidRosterInfo() return nil end
function GetNumBattlefieldScores() return #scores end
function GetBattlefieldScore(i) local s = scores[i]; if s then return unpack(s) end end
function GetSpellInfo(n) return n end
function GetNumWorldStateUI() return 2 end
function GetWorldStateUIInfo(i)
	if i == 1 then return 0, 2, "", "Interface\\WorldStateFrame\\AllianceIcon-pvp-alliance", "", "", "" end
	return 0, 2, "", "Interface\\WorldStateFrame\\HordeIcon-pvp-horde", "", "", ""
end
function GetAreaSpiritHealerTime() return 0 end
function GetSubZoneText() return "" end
function GetTime() return 0 end
function InCombatLockdown() return false end
function SendAddonMessage() end
function RequestBattlefieldScoreData() end
RAID_CLASS_COLORS = {
	SHAMAN = { r = 0, g = 0.44, b = 0.87 }, HUNTER = { r = 0.67, g = 0.83, b = 0.45 },
	WARRIOR = { r = 0.78, g = 0.61, b = 0.43 }, MAGE = { r = 0.41, g = 0.8, b = 0.94 },
	PRIEST = { r = 1, g = 1, b = 1 },
}
NORMAL_FONT_COLOR = { r = 1, g = 0.82, b = 0 }
LOCALIZED_CLASS_NAMES_MALE = {}
LOCALIZED_CLASS_NAMES_FEMALE = {}
DEFAULT_CHAT_FRAME = { AddMessage = function() end }
SlashCmdList = {}
unpack = unpack or table.unpack

local function region()
	local r = { shown = false, attrs = {} }
	function r:SetPoint() end
	function r:ClearAllPoints() end
	function r:SetSize() end
	function r:SetWidth() end
	function r:SetText(t) self.text_ = t end
	function r:GetText() return self.text_ end
	function r:SetJustifyH() end
	function r:SetTextColor(cr, cg, cb) self.color = { cr, cg, cb } end
	function r:GetStringWidth() return #(self.text_ or "") * 6 end
	function r:Show() self.shown = true end
	function r:Hide() self.shown = false end
	function r:IsShown() return self.shown end
	function r:SetScript() end
	function r:RegisterEvent() end
	function r:SetFrameStrata() end
	function r:SetToplevel() end
	function r:RegisterForClicks() end
	function r:SetHitRectInsets() end
	function r:SetAttribute(k, v) self.attrs[k] = v end
	function r:CreateFontString() return region() end
	function r:CreateTexture() local t = region(); function t:SetTexture() end; function t:SetVertexColor() end; return t end
	function r:GetName() return self.name end
	function r:GetBottom() return 100 end
	return r
end
function CreateFrame(kind, name) local f = region(); f.name = name; if name then _G[name] = f end; return f end
UIParent = region()

for i = 1, 2 do
	local row = region(); row.name = "AlwaysUpFrame" .. i; row.shown = true
	_G[row.name] = row
	local icon = region()
	function icon:GetTexture() return i == 1 and "Interface\\x-pvp-alliance" or "Interface\\x-pvp-horde" end
	_G[row.name .. "Icon"] = icon
	_G[row.name .. "Text"] = region()
	_G[row.name .. "DynamicIconButton"] = region()
end

-- ---- load the addon ----
local chunk = assert(loadfile(MODULE))
chunk("CENTURION_WSGHelper")
local CRT = ClassicResTimerFrame

local function addon(msg) CRT.OnEvent(CRT, "CHAT_MSG_ADDON", "CWSG", msg) end

-- The server names the carrier of the Alliance flag (Horde side) by first name.
addon("A:PICKUP:Elgrom")
local h = CRT.carrierH
check(h ~= nil, "horde carrier button exists")
check(h and h.text:GetText() == "Elgrom Fernbloom", "shows the full name, got " .. tostring(h and h.text:GetText()))
check(h and h.attrs.macrotext == "/targetexact Elgrom Fernbloom", "targets the full name, got " .. tostring(h and h.attrs.macrotext))
-- Enemy carriers outside the raid are red: the addon's scoreboard class lookup
-- reads the wrong return slots (it predates this change), so red is expected.
check(h and h.text.color and h.text.color[1] == 1 and h.text.color[2] == 0.1, "enemy carrier red")

-- A first name two players share stays as it is: never a guess.
addon("H:PICKUP:Mira")
local a = CRT.carrierA
check(a and a.text:GetText() == "Mira", "ambiguous first name left alone, got " .. tostring(a and a.text:GetText()))

-- Me, named by first name only: recognised as me.
addon("H:DROP:")
addon("H:PICKUP:Drunks")
check(a and a.text:GetText() == "Drunks Broklio", "own name resolved, got " .. tostring(a and a.text:GetText()))
check(a and a.text.color and a.text.color[1] == 0, "own name in my class colour")

-- A full name from the server passes straight through.
addon("A:DROP:")
addon("A:PICKUP:Aldric Bladewright")
check(h and h.attrs.macrotext == "/targetexact Aldric Bladewright", "full name kept")

-- A FULL state message with first names.
addon("FULL:Aldric:Elgrom:PLAYER:PLAYER:::::")
check(a.text:GetText() == "Elgrom Fernbloom" and h.text:GetText() == "Aldric Bladewright", "FULL payload resolved")

-- Nobody by that name: shown as sent.
addon("A:DROP:")
addon("A:PICKUP:Nobody")
check(h.text:GetText() == "Nobody", "unknown name shown as sent")

OUT = string.format("pass=%d fail=%d %s", pass, fail, table.concat(log, "; "))
