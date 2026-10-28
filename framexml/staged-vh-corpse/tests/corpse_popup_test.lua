-- Loads the real staged WorldStateFrame.lua against mocked client globals and a stand-in
-- StaticPopup_Show, then checks the corpse prompt: refused in Violet Hold (both client
-- rows), shown everywhere else, and every other popup passed through untouched.
-- Global set by the runner: WSF_PATH.

local pass, fail, log = 0, 0, {}
local function check(what, got, want)
	if got == want then pass = pass + 1
	else fail = fail + 1; log[#log + 1] = ("FAIL %s: got %s want %s"):format(what, tostring(got), tostring(want)) end
end

-- ------------------------------------------------------------------ client state
local S
local function reset() S = { instanceType = "none", mapName = "x", arenaByMap = false, queues = {} } end
reset()
function IsInInstance() return S.instanceType ~= "none", S.instanceType end
function GetInstanceInfo() return S.mapName, S.instanceType end
function IsActiveBattlefieldArena() if S.arenaByMap then return 1, nil end return nil, nil end
function GetBattlefieldStatus(i)
	local q = S.queues[i]
	if not q then return "none", nil, 0, 0, 0, 0, 0 end
	return q.status, "Some Map", 1, 10, 69, q.teamSize, 0
end
function InCombatLockdown() return false end
MAX_BATTLEFIELD_QUEUES = 2

-- ------------------------------------------------------------------ load-time mocks
function GetTime() return 100 end
function GetRealZoneText() return "x" end
function GetZoneText() return "x" end
function SendAddonMessage() end
function UIFrameFlashStop() end
tinsert = table.insert
LOOT_ITEM_CREATED_SELF = "You create: %s."
function CreateFrame()
	local f = {}
	function f:RegisterEvent() end
	function f:SetScript(name, fn) self[name] = fn end
	return f
end
StaticPopupDialogs = {}

-- ------------------------------------------------------------------ stock StaticPopup_Show
local shown = {}
local DIALOG = {}
function StaticPopup_Show(which, a1, a2, data)
	shown[#shown + 1] = { which = which, a1 = a1, a2 = a2, data = data }
	return DIALOG, "second"
end

dofile(WSF_PATH)

local function popups(label, setup, corpseShown)
	reset()
	setup()
	shown = {}
	local d = StaticPopup_Show("RECOVER_CORPSE")
	check(label .. ": corpse prompt shown", #shown == 1, corpseShown)
	check(label .. ": corpse prompt return", d, corpseShown and DIALOG or nil)

	-- Every other popup goes through as it was asked for, values back as they came.
	shown = {}
	local r1, r2 = StaticPopup_Show("RESURRECT", "Healer", nil, 7)
	check(label .. ": other popup shown", #shown, 1)
	check(label .. ": other popup which", shown[1] and shown[1].which, "RESURRECT")
	check(label .. ": other popup arg", shown[1] and shown[1].a1, "Healer")
	check(label .. ": other popup data", shown[1] and shown[1].data, 7)
	check(label .. ": other popup return 1", r1, DIALOG)
	check(label .. ": other popup return 2", r2, "second")

	shown = {}
	StaticPopup_Show("RECOVER_CORPSE_INSTANCE")
	check(label .. ": instance corpse prompt untouched", #shown, 1)
end

-- The Hold, battleground row (the live client Map.dbc): refused.
popups("hold", function()
	S.instanceType, S.mapName = "pvp", "The Violet Hold Gauntlet"
	S.queues[1] = { status = "active", teamSize = 0 }
end, false)

-- The Hold under its old arena row: refused too (the client would not ask there anyway).
popups("hold old row", function()
	S.instanceType, S.mapName, S.arenaByMap = "arena", "The Violet Hold Gauntlet", true
	S.queues[1] = { status = "active", teamSize = 0 }
end, false)

-- Another battleground: stock.
popups("warsong", function()
	S.instanceType, S.mapName = "pvp", "Warsong Gulch"
	S.queues[1] = { status = "active", teamSize = 0 }
end, true)

-- A real arena: left to the client, as before.
popups("arena", function()
	S.instanceType, S.mapName, S.arenaByMap = "arena", "Nagrand Arena", true
	S.queues[1] = { status = "active", teamSize = 3 }
end, true)

-- The open world, and the dungeon that shares the name: stock.
popups("world", function() end, true)
popups("dungeon", function()
	S.instanceType, S.mapName = "party", "Violet Hold"
end, true)

OUT = ("pass=%d fail=%d%s"):format(pass, fail, #log > 0 and ("\n" .. table.concat(log, "\n")) or "")
