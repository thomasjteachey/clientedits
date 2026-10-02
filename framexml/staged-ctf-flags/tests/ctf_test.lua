-- Runs WorldStateFrame.lua's CWSG flag logic against the patch-enUS-7 world map and
-- battlefield minimap consumers, with the client's carrier slots modelled from
-- Wow.exe: MSG_BATTLEGROUND_PLAYER_POSITIONS handler 0x54B3F0 stores N carriers from
-- slot N-1 down to 0, GetNumBattlefieldFlagPositions 0x54A0E0 is 2 when slot 1 is
-- set, GetBattlefieldFlagPosition(i) reads slot i-1. The raw token is the race-based
-- one (everyone Alliance race, as on B+), which is what the Lua has to correct.
-- Globals set by the runner: WSF_PATH, WMF_CHUNK, BFM_CHUNK.

local lines = {}
local function log(fmt, ...) lines[#lines + 1] = string.format(fmt, ...) end

-- ------------------------------------------------------------------ WoW API mocks
local clock = 100
function GetTime() return clock end
function GetInstanceInfo() return "Warsong Gulch", "pvp" end
function GetRealZoneText() return "Warsong Gulch" end
function GetZoneText() return "Warsong Gulch" end
function IsInInstance() return true, "pvp" end
function SendAddonMessage() end
function CreateFrame() return setmetatable({}, { __index = function() return function() end end }) end
function UIFrameFlashStop() end
tinsert = table.insert
LOOT_ITEM_CREATED_SELF = "You create: %s."
WORLDSTATES = {}
function GetNumWorldStateUI() return #WORLDSTATES end
function GetWorldStateUIInfo(i)
	local w = WORLDSTATES[i]
	return 0, w.state, "", w.icon, w.dynamicIcon, "", "", "", 0, 0, 0
end

MAP_RESETS = 0
function SetMapToCurrentZone() MAP_RESETS = MAP_RESETS + 1 end
function RequestBattlefieldPositions() end
function GetNumRaidMembers() return 0 end
MAX_PARTY_MEMBERS = 4
function UnitExists() return false end
if ( BFF_PATH and BFF_PATH ~= "" ) then
	dofile(BFF_PATH) -- BattlefieldFrame.xml loads before WorldStateFrame.xml (toc lines 52 / 123)
end
dofile(WSF_PATH)
WorldStateAlwaysUpFrame_Update = function() end -- the real one drives UI frames we do not mock

-- ------------------------------------------------------------------ client carrier slots
local slots = {}
local function positions(entries) -- entries in SERVER packet order: A-flag carrier, then H-flag carrier
	local n = #entries
	if n <= 0 then slots[0], slots[1] = nil, nil; return end
	if n < 2 then slots[1] = nil end
	local idx = n
	for _, e in ipairs(entries) do
		idx = idx - 1
		slots[idx] = e
	end
end
function GetNumBattlefieldFlagPositions()
	if slots[1] then return 2 elseif slots[0] then return 1 end
	return 0
end
function GetBattlefieldFlagPosition(i)
	local e = slots[i - 1]
	if not e then return 0, 0, nil end
	return e.x, e.y, "HordeFlag" -- race-based token for an Alliance-race carrier
end

-- ------------------------------------------------------------------ map / minimap frames
NUM_WORLDMAP_FLAGS = 2
WorldMapDetailFrame = { GetWidth = function() return 1000 end, GetHeight = function() return 1000 end }
BattlefieldMinimap = { GetWidth = function() return 1000 end, GetHeight = function() return 1000 end, IsShown = function() return false end }
local function makeFlag(name)
	local f = { shown = false }
	function f:Hide() self.shown = false end
	function f:Show() self.shown = true end
	function f:SetPoint(_, _, _, x, y) self.x, self.y = x, y end
	_G[name] = f
	_G[name .. "Texture"] = { SetTexture = function(_, path) f.tex = path end }
end
for i = 1, 2 do makeFlag("WorldMapFlag" .. i); makeFlag("BattlefieldMinimapFlag" .. i) end
assert(load(WMF_CHUNK))()
assert(load(BFM_CHUNK))()

local function drawn(prefix, update)
	update()
	local parts = {}
	for i = 1, 2 do
		local f = _G[prefix .. i]
		if f.shown then
			parts[#parts + 1] = string.format("%s@%.2f,%.2f", f.tex:match("([^\\]+)$"), f.x / 1000, -f.y / 1000)
		end
	end
	table.sort(parts)
	return table.concat(parts, " ")
end

-- In game both frames get CHAT_MSG_ADDON; BattlefieldFrame registered first.
local function cwsg(payload)
	if ( CenturionWSG_HandleAddonMessage ) then
		CenturionWSG_HandleAddonMessage("CWSG", payload, "WHISPER", "")
	end
	CenturionCTF_HandleAddonMessage("CWSG", payload, "WHISPER", "")
end

local pass, fail = 0, 0
local function check(label, expect)
	local map = drawn("WorldMapFlag", WorldMapFrame_UpdateBattlefieldFlags)
	local mini = drawn("BattlefieldMinimapFlag", BattlefieldMinimap_UpdateFlags)
	local parts = {}
	for p in string.gmatch(expect, "%S+") do parts[#parts + 1] = p end
	table.sort(parts)
	expect = table.concat(parts, " ")
	local ok = (map == expect) and (mini == expect)
	if ok then pass = pass + 1 else fail = fail + 1 end
	log("%-4s %-58s map=[%s]%s", ok and "ok" or "FAIL", label, map,
		ok and "" or ("  minimap=[" .. mini .. "]  expected=[" .. expect .. "]"))
end

-- Carriers (map coordinates). "Horde side" players are Alliance race on B+.
local P1 = { x = 0.20, y = 0.30 } -- carries the physical Alliance flag
local P2 = { x = 0.70, y = 0.60 } -- carries the physical Horde flag
local P3 = { x = 0.45, y = 0.50 }

log("-- one carrier")
cwsg("A:PICKUP:P1:0.2000:0.3000"); cwsg("FULL:P1::PLAYER:BASE:0.2000:0.3000::")
check("A picked up, slots not refreshed yet", "")
positions({ P1 })
check("A carried by P1", "AllianceFlag@0.20,0.30")

log("-- second carrier")
cwsg("H:PICKUP:P2:0.7000:0.6000"); cwsg("FULL:P1:P2:PLAYER:PLAYER:0.2000:0.3000:0.7000:0.6000")
check("H picked up, slots still hold only P1", "AllianceFlag@0.20,0.30")
positions({ P1, P2 })
check("both carried (packet A-carrier first)", "AllianceFlag@0.20,0.30 HordeFlag@0.70,0.60")

log("-- one of two dropped")
cwsg("H:DROP:0.6800:0.5900"); cwsg("FULL:P1::PLAYER:GROUND:0.2000:0.3000:0.6800:0.5900")
check("H dropped, slots still hold P2 and P1", "AllianceFlag@0.20,0.30 HordeFlag@0.68,0.59")
positions({ P1 })
check("H dropped, slots refreshed", "AllianceFlag@0.20,0.30 HordeFlag@0.68,0.59")
cwsg("H:RETURN"); cwsg("FULL:P1::PLAYER:BASE:0.2000:0.3000::")
check("H returned", "AllianceFlag@0.20,0.30")

log("-- capture")
cwsg("A:CAPTURE"); cwsg("A:WAIT"); cwsg("FULL:::WAIT:BASE::::")
check("A captured, slots still hold P1", "")
positions({})
check("A captured, slots refreshed", "")

log("-- single carrier dropped")
cwsg("A:PICKUP:P3:0.4500:0.5000"); cwsg("FULL:P3::PLAYER:BASE:0.4500:0.5000::")
positions({ P3 })
check("A carried by P3", "AllianceFlag@0.45,0.50")
cwsg("A:DROP:0.4400:0.4900"); cwsg("FULL:::GROUND:BASE:0.4400:0.4900::")
check("A dropped, slots still hold P3", "AllianceFlag@0.44,0.49")
positions({})
check("A dropped, slots refreshed", "AllianceFlag@0.44,0.49")

log("-- both carried, both dropped")
cwsg("A:PICKUP:P1:0.2:0.3"); cwsg("H:PICKUP:P2:0.7:0.6"); cwsg("FULL:P1:P2:PLAYER:PLAYER:0.2:0.3:0.7:0.6")
positions({ P1, P2 })
check("both carried again", "AllianceFlag@0.20,0.30 HordeFlag@0.70,0.60")
cwsg("A:DROP:0.2100:0.3100"); cwsg("H:DROP:0.6900:0.6100"); cwsg("FULL:::GROUND:GROUND:0.2100:0.3100:0.6900:0.6100")
check("both dropped, slots still hold both carriers", "AllianceFlag@0.21,0.31 HordeFlag@0.69,0.61")

log("-- no CWSG received (HasFull false), order alone")
CENTURION_WSG_AUTH.HasFull = false
positions({ P1, P2 })
check("both carried, no CWSG", "AllianceFlag@0.20,0.30 HordeFlag@0.70,0.60")
WORLDSTATES = {
	{ state = 1, icon = "Interface\\TargetingFrame\\UI-PVP-Alliance", dynamicIcon = "Interface\\WorldStateFrame\\HordeFlag" },
	{ state = 2, icon = "Interface\\TargetingFrame\\UI-PVP-Horde", dynamicIcon = "Interface\\WorldStateFrame\\AllianceFlag" },
}
positions({ P1 })
check("one carried, no CWSG, world state says Alliance flag held", "AllianceFlag@0.20,0.30")

log("-- old helper shim")
log("%-4s Centurion_GetFixedWSGFlagToken(0.5,0.5,'HordeFlag') = %s", "info", tostring(Centurion_GetFixedWSGFlagToken(0.5, 0.5, "HordeFlag")))

log("RESULT pass=%d fail=%d  bridge loaded=%s  SetMapToCurrentZone calls from CWSG handling=%d",
	pass, fail, tostring(CenturionWSG_HandleAddonMessage ~= nil), MAP_RESETS)
OUT = table.concat(lines, "\n")
