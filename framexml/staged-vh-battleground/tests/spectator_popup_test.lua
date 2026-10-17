-- Loads the real staged WorldStateFrame.lua against mocked client globals and the
-- stock DEATH popup (copied from StaticPopup.lua 3.3.5, lines 1125-1151), then checks
-- the death popup in Violet Hold now that its map is a battleground in the client:
-- spectator text and the spectator chat line there, stock behaviour everywhere else,
-- and never the line twice. Also re-checks the last-stand rules under the new row.
-- Global set by the runner: WSF_PATH.

local pass, fail, log = 0, 0, {}
local function check(what, got, want)
	if got == want then pass = pass + 1
	else fail = fail + 1; log[#log + 1] = ("FAIL %s: got %s want %s"):format(what, tostring(got), tostring(want)) end
end

-- ------------------------------------------------------------------ client state
local S = { instanceType = "none", mapName = "x", arenaByMap = false, queues = {}, combat = false, releaseTime = 0 }
function IsInInstance() return S.instanceType ~= "none", S.instanceType end
function GetInstanceInfo() return S.mapName, S.instanceType end
function IsActiveBattlefieldArena() if S.arenaByMap then return 1, nil end return nil, nil end
function GetBattlefieldStatus(i)
	local q = S.queues[i]
	if not q then return "none", nil, 0, 0, 0, 0, 0 end
	return q.status, "Some Map", 1, 10, 69, q.teamSize, 0
end
function InCombatLockdown() return S.combat end
MAX_BATTLEFIELD_QUEUES = 2

-- ------------------------------------------------------------------ load-time mocks
function GetTime() return 100 end
function GetRealZoneText() return "x" end
function GetZoneText() return "x" end
function SendAddonMessage() end
function UIFrameFlashStop() end
tinsert = table.insert
LOOT_ITEM_CREATED_SELF = "You create: %s."

local frames = {}
function CreateFrame()
	local f = { events = {} }
	function f:RegisterEvent(e) self.events[e] = true end
	function f:SetScript(name, fn) self[name] = fn end
	frames[#frames + 1] = f
	return f
end

-- ------------------------------------------------------------------ stock DEATH popup
DEATH_RELEASE_TIMER = "%d %s until release"
DEATH_RELEASE_NOTIMER = "You have died. Release to the nearest graveyard?"
DEATH_RELEASE_SPECTATOR = "You have died.  Release spirit to enter Spectator Mode."
ARENA_SPECTATOR = "You are in Spectator Mode. To abandon this battle, right click the Arena icon on the minimap and select 'Leave Arena'."
ChatTypeInfo = { SYSTEM = { r = 1, g = 1, b = 0, id = 1 } }
local chat, repops = {}, 0
DEFAULT_CHAT_FRAME = { AddMessage = function(_, msg) chat[#chat + 1] = msg end }
function GetReleaseTimeRemaining() return S.releaseTime end
function HasSoulstone() return nil end
function RepopMe() repops = repops + 1 end
function CannotBeResurrected() return nil end

StaticPopupDialogs = {}
StaticPopupDialogs["DEATH"] = {
	OnShow = function(self)
		self.timeleft = GetReleaseTimeRemaining();
		local text = HasSoulstone();
		if ( text ) then
			self.button2:SetText(text);
		end

		if ( IsActiveBattlefieldArena() ) then
			self.text:SetText(DEATH_RELEASE_SPECTATOR);
		elseif ( self.timeleft == -1 ) then
			self.text:SetText(DEATH_RELEASE_NOTIMER);
		end
	end,
	OnAccept = function(self)
		if ( IsActiveBattlefieldArena() ) then
			local info = ChatTypeInfo["SYSTEM"];
			DEFAULT_CHAT_FRAME:AddMessage(ARENA_SPECTATOR, info.r, info.g, info.b, info.id);
		end
		RepopMe();
		if ( CannotBeResurrected() ) then
			return 1
		end
	end,
}

dofile(WSF_PATH)

local function popup()
	local dialog = { text = { value = " " } }
	function dialog.text:SetText(t) self.value = t end
	return dialog
end

local function die(label, setup, wantText, wantChat)
	S = { instanceType = "none", mapName = "x", arenaByMap = false, queues = {}, combat = false, releaseTime = 0 }
	setup()
	chat, repops = {}, 0
	local d = popup()
	StaticPopupDialogs["DEATH"].OnShow(d)
	check(label .. ": popup text", d.text.value, wantText)
	StaticPopupDialogs["DEATH"].OnAccept(d)
	check(label .. ": released", repops, 1)
	check(label .. ": chat lines", #chat, wantChat and 1 or 0)
	check(label .. ": chat line", chat[1], wantChat)
end

-- The Hold, battleground row (the new client Map.dbc): the popup is taught it.
die("hold", function()
	S.instanceType, S.mapName = "pvp", "The Violet Hold Gauntlet"
	S.queues[1] = { status = "active", teamSize = 0 }
end, DEATH_RELEASE_SPECTATOR, CENTURION_LAST_STAND_SPECTATOR)

-- Another battleground: stock. No release timer here means the text stays as set.
die("warsong", function()
	S.instanceType, S.mapName = "pvp", "Warsong Gulch"
	S.queues[1] = { status = "active", teamSize = 0 }
end, " ", nil)

-- A battleground with a release timer of -1: stock no-timer text.
die("warsong no timer", function()
	S.instanceType, S.mapName, S.releaseTime = "pvp", "Warsong Gulch", -1
	S.queues[1] = { status = "active", teamSize = 0 }
end, DEATH_RELEASE_NOTIMER, nil)

-- A real arena: the stock arena text and the stock line, once.
die("arena", function()
	S.instanceType, S.mapName, S.arenaByMap = "arena", "Nagrand Arena", true
	S.queues[1] = { status = "active", teamSize = 3 }
end, DEATH_RELEASE_SPECTATOR, ARENA_SPECTATOR)

-- The Hold under its OLD arena row: stock handles it; ours must not add a second line.
die("hold old row", function()
	S.instanceType, S.mapName, S.arenaByMap = "arena", "The Violet Hold Gauntlet", true
	S.queues[1] = { status = "active", teamSize = 0 }
end, DEATH_RELEASE_SPECTATOR, ARENA_SPECTATOR)

-- The open world and a dungeon called Violet Hold: stock.
die("world", function() end, " ", nil)
die("dungeon", function()
	S.instanceType, S.mapName = "party", "Violet Hold"
end, " ", nil)

OUT = ("pass=%d fail=%d%s"):format(pass, fail, #log > 0 and ("\n" .. table.concat(log, "\n")) or "")
