-- Loads the real staged WorldStateFrame.lua against mocked client globals and
-- checks the last-stand rules: in Violet Hold (an arena MAP, queued with team
-- size 0) the arena enemy frames stay hidden and the scoreboard still opens;
-- in a real arena and in an ordinary battleground nothing changes.
-- Global set by the runner: WSF_PATH.

local pass, fail, log = 0, 0, {}
local function check(what, got, want)
	if got == want then pass = pass + 1
	else fail = fail + 1; log[#log + 1] = ("FAIL %s: got %s want %s"):format(what, tostring(got), tostring(want)) end
end

-- ------------------------------------------------------------------ client state
-- instanceType: what IsInInstance() reads off the CURRENT map's Map.dbc row.
-- arenaByMap:   IsActiveBattlefieldArena(), i.e. the BATTLEFIELD map's row is 4.
-- queues:       GetBattlefieldStatus(i) -> status, teamSize.
local S = { instanceType = "none", arenaByMap = false, queues = {}, combat = false }
function IsInInstance() return S.instanceType ~= "none", S.instanceType end
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
function GetInstanceInfo() return "x", S.instanceType end
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

dofile(WSF_PATH)

local hook
for _, f in ipairs(frames) do
	if f.events["ADDON_LOADED"] and f.events["UPDATE_BATTLEFIELD_STATUS"] then hook = f end
end
check("last-stand frame registered", hook ~= nil, true)
-- Tolerates a missing hook so the LIVE file can be run too, to show what the new
-- Map.dbc would do to it on its own.
local function fire(event, arg1) if hook then hook.OnEvent(hook, event, arg1) end end

-- ------------------------------------------------------------------ Blizzard_ArenaUI
-- ArenaEnemyFrames_UpdateVisible is copied verbatim from Blizzard_ArenaUI.lua:105.
local shows = 0
ArenaEnemyFrames = { show = true, shown = false }
function ArenaEnemyFrames:Show() if not self.shown then shows = shows + 1 end self.shown = true end
function ArenaEnemyFrames:Hide() self.shown = false end
function ArenaEnemyFrames_UpdateVisible()
	local _, instanceType = IsInInstance();
	if ( ArenaEnemyFrames.show and (instanceType == "arena")) then
		ArenaEnemyFrames:Show();
	else
		ArenaEnemyFrames:Hide();
	end
end

-- ------------------------------------------------------------------ scoreboard
local panelShown = 0
WorldStateScoreFrame = { IsShown = function() return false end }
function ShowUIPanel() panelShown = panelShown + 1 end
function HideUIPanel() end
MiniMapBattlefieldFrame = { status = "active" }
local function scoreboardOpens()
	local before = panelShown
	ToggleWorldStateScoreFrame()
	return panelShown > before
end

local function place(instanceType, arenaByMap, queues)
	S.instanceType, S.arenaByMap, S.queues = instanceType, arenaByMap, queues
end

-- 1. Violet Hold. The arena UI loads at PLAYER_ENTERING_WORLD (IsInInstance says
--    "arena"), and Blizzard's OnLoad shows the frames before ADDON_LOADED arrives.
place("arena", true, { { status = "active", teamSize = 0 } })
ArenaEnemyFrames:Show()
fire("ADDON_LOADED", "Blizzard_ArenaUI")
check("VH: hidden once the arena UI has loaded", ArenaEnemyFrames.shown, false)
shows = 0
ArenaEnemyFrames_UpdateVisible()           -- what Blizzard calls at PLAYER_ENTERING_WORLD
fire("UPDATE_BATTLEFIELD_STATUS")
fire("PLAYER_REGEN_ENABLED")
check("VH: stays hidden through every refresh", ArenaEnemyFrames.shown, false)
check("VH: never flashed on and off", shows, 0)
check("VH: scoreboard opens mid-run", scoreboardOpens(), true)

-- 2. Other addons loading do nothing.
fire("ADDON_LOADED", "Blizzard_TalentUI")
check("unrelated ADDON_LOADED leaves it hidden", ArenaEnemyFrames.shown, false)

-- 3. A real arena: the status the client still holds from the Hold is gone after
--    the loading screen, then the real one arrives with its team size.
place("arena", false, {})
ArenaEnemyFrames_UpdateVisible()
check("arena before its status: shown", ArenaEnemyFrames.shown, true)
place("arena", true, { { status = "active", teamSize = 3 } })
fire("UPDATE_BATTLEFIELD_STATUS")
check("arena 3v3: shown", ArenaEnemyFrames.shown, true)
check("arena 3v3: scoreboard locked mid-match", scoreboardOpens(), false)

-- 4. Leaving the Hold straight into an arena while the Hold's status is still
--    cached: hidden for a moment, then the arena's own status puts them back.
place("arena", true, { { status = "active", teamSize = 0 } })
fire("UPDATE_BATTLEFIELD_STATUS")
check("stale VH status: hidden", ArenaEnemyFrames.shown, false)
place("arena", true, { { status = "active", teamSize = 5 } })
fire("UPDATE_BATTLEFIELD_STATUS")
check("then the 5v5 status: shown again", ArenaEnemyFrames.shown, true)

-- 5. The status lands in combat: nothing touches the secure frames until it ends.
place("arena", true, { { status = "active", teamSize = 0 } })
S.combat = true
fire("UPDATE_BATTLEFIELD_STATUS")
check("in combat: left alone", ArenaEnemyFrames.shown, true)
S.combat = false
fire("PLAYER_REGEN_ENABLED")
check("combat over: hidden", ArenaEnemyFrames.shown, false)

-- 6. The Hold active in the second queue slot, something else queued in the first.
place("arena", true, { { status = "queued", teamSize = 2 }, { status = "active", teamSize = 0 } })
ArenaEnemyFrames_UpdateVisible()
check("VH in slot 2: hidden", ArenaEnemyFrames.shown, false)
check("VH in slot 2: scoreboard opens", scoreboardOpens(), true)

-- 7. An ordinary battleground and the open world are untouched.
place("pvp", false, { { status = "active", teamSize = 0 } })
fire("UPDATE_BATTLEFIELD_STATUS")
check("battleground: hidden (stock rule)", ArenaEnemyFrames.shown, false)
check("battleground: scoreboard opens", scoreboardOpens(), true)
place("none", false, {})
fire("UPDATE_BATTLEFIELD_STATUS")
check("world: hidden (stock rule)", ArenaEnemyFrames.shown, false)
MiniMapBattlefieldFrame.status = "none"
check("world: no scoreboard", scoreboardOpens(), false)

OUT = ("pass=%d fail=%d\n%s"):format(pass, fail, table.concat(log, "\n"))
