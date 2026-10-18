-- Drives the staged ArenaFrame.lua (Solo Shuffle group join) with mocked WoW APIs.
-- Globals in: ARENA (path to the file under test). Out: OUT ("... fail=N").

local results, fail = {}, 0
local function check(name, ok)
    results[#results + 1] = (ok and "PASS " or "FAIL ") .. name
    if not ok then fail = fail + 1 end
end

-- Any widget: remembers shown/enabled/text, every other method is a no-op.
local function widget(name)
    local w = { name = name, shown = true, enabled = true }
    return setmetatable(w, { __index = function(t, key)
        if key == "Show" then return function(self) self.shown = true end end
        if key == "Hide" then return function(self) self.shown = false end end
        if key == "IsShown" then return function(self) return self.shown end end
        if key == "Enable" then return function(self) self.enabled = true end end
        if key == "Disable" then return function(self) self.enabled = false end end
        if key == "SetText" then return function(self, text) self.text = text end end
        if key == "SetID" then return function(self, id) self.id = id end end
        if key == "GetID" then return function(self) return self.id end end
        if key == "CreateFontString" then return function(self, n) return widget(n) end end
        -- fields (ArenaFrame.selection) stay nil; only Methods are mocked
        if not key:match("^%u") then return nil end
        return function() end
    end })
end

_G.CreateFrame = function(_, name) local w = widget(name); if name then _G[name] = w end; return w end
_G.UnitName = function() return "Tester" end
_G.SendAddonMessage = function() end
_G.GameTooltip_Hide = function() end
_G.PVP_TEAMTYPE = "%dv%d"
_G.ARENA_RATED = "Rated"
_G.ARENA_BATTLES = "Arena Battles"
_G.ARENA_RATED_BATTLE = "Rated Battle:"
_G.ARENA_MASTER_TEXT = "Let the games begin!"
_G.ARENA_MASTER_NO_SEASON_TEXT = "No season."
_G.IsBattlefieldArena = function() return true end
_G.ShowUIPanel = function(f) f.shown = true end
_G.HideUIPanel = function(f) f.shown = false end
_G.GetCurrentArenaSeason = function() return 8 end

local party, raid, leader = 0, 0, false
_G.GetNumPartyMembers = function() return party end
_G.GetNumRaidMembers = function() return raid end
_G.IsPartyLeader = function() return leader end
local joined
_G.JoinBattlefield = function(...) joined = { ... } end

dofile(ARENA)

-- The frame and its stock children exist by the time events arrive.
_G.ArenaFrame = widget("ArenaFrame")
for i = 1, 6 do _G["ArenaZone" .. i] = widget("ArenaZone" .. i); _G["ArenaZone" .. i]:SetID(i) end
_G.ArenaFrameJoinButton = widget("ArenaFrameJoinButton"); ArenaFrameJoinButton:SetID(1)
_G.ArenaFrameGroupJoinButton = widget("ArenaFrameGroupJoinButton"); ArenaFrameGroupJoinButton:SetID(2)
_G.ArenaFrameZoneDescription = widget("ArenaFrameZoneDescription")
_G.ArenaFrameFrameLabel = widget("ArenaFrameFrameLabel")
_G.ArenaFrameNameHeader = widget("ArenaFrameNameHeader")
_G.ArenaFrameNameHeader2 = widget("ArenaFrameNameHeader2")
ArenaFrame_OnLoad(ArenaFrame)

local driver_events = {}
-- the ARENAQUEUE switch is handled by a frame made at load; find its OnEvent
-- by replaying through CreateFrame is not possible after the fact, so set the
-- mode the way that handler does.
local function shuffleMode(on)
    CENTURION_ARENA_SHUFFLE = on
    CENTURION_ARENA_SQUADS = 0
end

local function open()
    ArenaFrame_OnEvent(ArenaFrame, "BATTLEFIELDS_SHOW")
end

-- 1. Shuffle, alone: Join Battle on, Join as Group shown but greyed, 3v3 picked.
shuffleMode(true); party, raid, leader = 0, 0, false; ArenaFrame.selection = nil
open()
check("solo: 3v3 row picked", ArenaFrame.selection == 6)
check("solo: join enabled", ArenaFrameJoinButton.enabled)
check("solo: group join shown", ArenaFrameGroupJoinButton.shown)
check("solo: group join greyed", not ArenaFrameGroupJoinButton.enabled)
check("solo: header", ArenaFrameNameHeader.text == "Solo or grouped. New teams every round.")
check("solo: rated rows hidden", not ArenaZone1.shown and not ArenaZone4.shown and ArenaZone5.shown and ArenaZone6.shown)

-- 2. Shuffle, raid leader of six: group join live, default 3v3, joins row 2 as group.
party, raid, leader = 0, 6, true; ArenaFrame.selection = nil
open()
check("raid leader: 3v3 row picked", ArenaFrame.selection == 6)
check("raid leader: group join enabled", ArenaFrameGroupJoinButton.shown and ArenaFrameGroupJoinButton.enabled)
joined = nil
ArenaFrameJoinButton_OnClick(ArenaFrameGroupJoinButton)
check("raid leader: JoinBattlefield(2, 1)", joined and joined[1] == 2 and joined[2] == 1 and joined[3] == nil)

-- 3. Shuffle, party leader who picked 2v2 last time keeps it, and alone joins alone.
party, raid, leader = 3, 0, true; ArenaFrame.selection = 5
open()
check("party leader: 2v2 kept", ArenaFrame.selection == 5)
joined = nil
ArenaFrameJoinButton_OnClick(ArenaFrameJoinButton)
check("party leader: Join Battle joins alone", joined and joined[1] == 1 and joined[2] == nil)

-- 4. Shuffle, party member who is not the leader: group join greyed.
party, raid, leader = 3, 0, false; ArenaFrame.selection = nil
open()
check("party member: group join greyed", ArenaFrameGroupJoinButton.shown and not ArenaFrameGroupJoinButton.enabled)

-- 5. Ordinary arena master, party leader: practice row as before, group join live.
shuffleMode(false); party, raid, leader = 1, 0, true; ArenaFrame.selection = nil
open()
check("arena: party goes to practice", ArenaFrame.selection == 5)
check("arena: group join enabled", ArenaFrameGroupJoinButton.shown and ArenaFrameGroupJoinButton.enabled)
check("arena: header", ArenaFrameNameHeader.text == "Rated Battle:")

-- 6. Ordinary arena master, rated row: join greyed, group join hidden (unchanged).
ArenaButton_OnClick(ArenaZone2)
check("arena rated: join greyed", not ArenaFrameJoinButton.enabled)
check("arena rated: group join hidden", not ArenaFrameGroupJoinButton.shown)

OUT = table.concat(results, "\n") .. "\nfail=" .. fail
