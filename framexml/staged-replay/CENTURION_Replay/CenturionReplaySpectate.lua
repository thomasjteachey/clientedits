------------------------------------------------------------------------------
-- CenturionReplaySpectate.lua - spectating a replay player.
--
-- Right-click a player in the replay (in the world, or their arena frame), or
-- press Spectate: the camera follows them without turning when they turn, the
-- arena frames hide and the normal UI shows, and YOUR action bars become
-- theirs - their spells, items and macros (as the spell the macro casts), with
-- their cooldowns, lit when they press one. Right-click someone else to
-- switch. Move (W/A/S/D, strafe, autorun) and you are back in your own body
-- with your own bars, standing where they stood.
--
-- The pieces:
--   * camera: CenturionSpectate() in the client-tweaks dinput8.dll puts the
--     normal camera on them, with the camera's own yaw lock held so it does not
--     swing round when they turn; zoom and mouse drag are the normal camera's.
--     The DLL also takes the right-click ("arm") and ends the follow on a
--     movement key.
--   * bars: the server (BGReplay.cpp) on "ASSUN SPECTATE <guid>" teaches the
--     client their spells and sends their bars and cooldowns as yours, the way
--     an All-Star life swaps bars; "SPECTATE OFF" puts yours back. Your saved
--     bars are never touched. It also lists their teammates as your party, so
--     the party frames are theirs.
--   * frames: your player frame shows them (through the focus) and the enemy
--     frames show their enemies (CenturionSquadFrames, fed from the replay).
------------------------------------------------------------------------------

local active = false
local followed = nil
local resumeGuid = nil
local uiWasHidden = false
local quietUntil = 0
local QUIET_AFTER = 5
local warnedNoCamera = false

-- "You have learned a new spell" for every borrowed spell, and "unlearned"
-- after: All-Star's chat filter, for while spectating and a moment after. The
-- party the server lists you in (their teammates) says the same as a real one
-- would - "X joins the party", the loot rules - and is hidden the same way.
local patterns = {}
local function AddPattern(name, specs)
    local fmt = _G[name]
    if type(fmt) ~= "string" then
        return
    end
    local pattern = string.gsub(fmt, "([%(%)%.%%%+%-%*%?%[%]%^%$])", "%%%1")
    pattern = string.gsub(pattern, specs, ".+")
    table.insert(patterns, "^" .. pattern .. "$")
end
for _, name in ipairs({ "ERR_LEARN_SPELL_S", "ERR_LEARN_ABILITY_S", "ERR_LEARN_PASSIVE_S", "ERR_SPELL_UNLEARNED_S",
    "ERR_JOINED_GROUP_S", "ERR_LEFT_GROUP_S", "ERR_LEFT_GROUP_YOU", "ERR_GROUP_DISBANDED",
    "ERR_RAID_MEMBER_ADDED_S", "ERR_RAID_MEMBER_REMOVED_S", "ERR_RAID_YOU_JOINED", "ERR_RAID_YOU_LEFT",
    "ERR_NEW_LEADER_S", "ERR_NEW_LEADER_YOU", "ERR_SET_LOOT_FREEFORALL", "ERR_SET_LOOT_GROUP",
    "ERR_SET_LOOT_ROUNDROBIN", "ERR_SET_LOOT_NBG", "ERR_SET_LOOT_MASTER", "ERR_SET_LOOT_THRESHOLD_S",
    "ERR_DUNGEON_DIFFICULTY_CHANGED_S", "ERR_RAID_DIFFICULTY_CHANGED_S" }) do
    AddPattern(name, "%%%%s")
end

-- Recount asks "Reset Recount?" on joining a group (its default); the
-- watched player's party is not one you joined. Wrapped once Recount is there.
local recountWrapped = false
local function QuietRecount()
    if recountWrapped or type(Recount) ~= "table" or type(Recount.ShowReset) ~= "function" then
        return
    end
    recountWrapped = true
    local showReset = Recount.ShowReset
    Recount.ShowReset = function(...)
        if active or GetTime() < quietUntil then
            return
        end
        return showReset(...)
    end
end

local function Quiet()
    return active or GetTime() < quietUntil
end

if ChatFrame_AddMessageEventFilter then
    ChatFrame_AddMessageEventFilter("CHAT_MSG_SYSTEM", function(self, event, message, ...)
        if not Quiet() or type(message) ~= "string" then
            return false
        end
        for _, pattern in ipairs(patterns) do
            if string.find(message, pattern) then
                return true
            end
        end
        return false
    end)
end

-- Sound: WoW hears from the character by default (Sound_ListenerAtCharacter),
-- which stays where you left it while the camera is on someone across the
-- arena. While spectating it hears from the camera; your setting comes back
-- after (and on logout, should you log out spectating).
local LISTENER_CVAR = "Sound_ListenerAtCharacter"
local savedListener = nil

local function ListenAtCamera()
    if savedListener ~= nil or not GetCVar or not SetCVar then
        return
    end
    local current = GetCVar(LISTENER_CVAR)
    if current == nil then
        return
    end
    savedListener = current
    if current ~= "0" then
        SetCVar(LISTENER_CVAR, "0")
    end
end

local function ListenerBack()
    if savedListener == nil then
        return
    end
    if SetCVar and GetCVar and GetCVar(LISTENER_CVAR) ~= savedListener then
        SetCVar(LISTENER_CVAR, savedListener)
    end
    savedListener = nil
end

local function TellServer(text)
    if SendAddonMessage and UnitName then
        local name = UnitName("player")
        if name then
            SendAddonMessage("ASSUN", text, "WHISPER", name)
        end
    end
end

-- "Spectating <name> - move to stop", over the top of the screen.
local status = CreateFrame("Frame", "CenturionReplaySpectateStatus", WorldFrame)
status:SetWidth(400)
status:SetHeight(24)
status:SetPoint("TOP", WorldFrame, "TOP", 0, -125)   -- under the arena's world-state lines
status:SetFrameStrata("HIGH")
status.text = status:CreateFontString(nil, "OVERLAY")
status.text:SetFont(STANDARD_TEXT_FONT, 15, "OUTLINE")
status.text:SetPoint("CENTER", 0, 0)
status:Hide()

local CLASS_TOKENS = {
    [1] = "WARRIOR", [2] = "PALADIN", [3] = "HUNTER", [4] = "ROGUE", [5] = "PRIEST",
    [6] = "DEATHKNIGHT", [7] = "SHAMAN", [8] = "MAGE", [9] = "WARLOCK", [11] = "DRUID",
}

local function ShowStatus()
    local p = followed and ATPlayers and ATPlayers[followed]
    if not p then
        status:Hide()
        return
    end
    local token = CLASS_TOKENS[tonumber(p.class) or 0]
    local c = token and RAID_CLASS_COLORS and RAID_CLASS_COLORS[token]
    local hex = c and string.format("%02x%02x%02x", c.r * 255, c.g * 255, c.b * 255) or "ffffff"
    status.text:SetText("Spectating |cff" .. hex .. (p.name or "?") .. "|r - move to stop")
    status:Show()
end

-- The arena frames stay hidden while spectating: SunwellAS shows them again
-- on its own as the feed arrives, so this runs every frame.
local function HideArenaFrames()
    if not ATPlayers then
        return
    end
    for _, p in pairs(ATPlayers) do
        if p.fsmall and p.fsmall.main and p.fsmall.main:IsShown() then p.fsmall.main:Hide() end
        if p.fsmall and p.fsmall.pet and p.fsmall.pet:IsShown() then p.fsmall.pet:Hide() end
        if p.fself and p.fself.main and p.fself.main:IsShown() then p.fself.main:Hide() end
        if p.ftarget and p.ftarget.main and p.ftarget.main:IsShown() then p.ftarget.main:Hide() end
        if p.cooldowns and p.cooldowns:IsShown() then p.cooldowns:Hide() end
    end
end

------------------------------------------------------------------------------
-- the spectated player's presses, on the default action buttons
------------------------------------------------------------------------------

local BUTTON_SETS = { "ActionButton", "BonusActionButton", "MultiBarBottomLeftButton", "MultiBarBottomRightButton",
    "MultiBarRightButton", "MultiBarLeftButton" }
local lit = {}

local function ButtonSpell(button)
    local action = button.action
    if not action and ActionButton_GetPagedID then
        action = ActionButton_GetPagedID(button)
    end
    if not action or not GetActionInfo then
        return nil
    end
    local kind, id, _, spellId = GetActionInfo(action)
    if kind ~= "spell" then
        return nil
    end
    return spellId or id
end

local function LightSpell(spellId)
    local name = GetSpellInfo(spellId)
    for _, set in ipairs(BUTTON_SETS) do
        for i = 1, 12 do
            local button = _G[set .. i]
            if button and button:IsVisible() then
                local spell = ButtonSpell(button)
                if spell and (spell == spellId or (name and GetSpellInfo(spell) == name)) then
                    button:LockHighlight()
                    lit[button] = GetTime() + 0.35
                end
            end
        end
    end
end

------------------------------------------------------------------------------
-- the spectated player's casts, on YOUR cast bar
--
-- CastingBarFrame is driven by UNIT_SPELLCAST_* for "player", which the
-- viewer never casts; the server sends the spectated player's (SPE start,
-- CSG went off, CSF interrupted, CSD pushback, CHS/CHU channel) and this sets
-- the bar up exactly as CastingBarFrame_OnEvent does for a cast of your own,
-- so CastingBarFrame_OnUpdate runs it, flash and fade included.
------------------------------------------------------------------------------

local castSpell = nil
local SPELL_FAILED_INTERRUPTED, SPELL_FAILED_INTERRUPTED_COMBAT = 40, 41

local function BarParts(bar)
    local n = bar:GetName()
    return _G[n .. "Spark"], _G[n .. "Text"], _G[n .. "Flash"], _G[n .. "Icon"]
end

local function CastStart(spell, castMs)
    local bar = CastingBarFrame
    if not bar then
        return
    end
    local name, _, icon = GetSpellInfo(spell)
    local spark, text, flash, barIcon = BarParts(bar)
    bar:SetStatusBarColor(1.0, 0.7, 0.0)
    if spark then spark:Show() end
    if flash then flash:SetAlpha(0.0); flash:Hide() end
    bar.value = 0
    bar.maxValue = castMs / 1000
    bar:SetMinMaxValues(0, bar.maxValue)
    bar:SetValue(0)
    if text then text:SetText(name or "") end
    if barIcon then barIcon:SetTexture(icon) end
    bar:SetAlpha(1.0)
    bar.holdTime = 0
    bar.casting = 1
    bar.castID = nil
    bar.channeling = nil
    bar.flash = nil
    bar.fadeOut = nil
    if bar.showCastbar ~= false then
        bar:Show()
    end
    castSpell = spell
end

local function CastSucceeded(spell)
    local bar = CastingBarFrame
    if not bar or not bar.casting or castSpell ~= spell then
        return
    end
    local spark, _, flash = BarParts(bar)
    bar:SetValue(bar.maxValue or 0)
    if CastingBarFrame_FinishSpell then
        CastingBarFrame_FinishSpell(bar, spark, flash)
    end
    castSpell = nil
end

local function CastFailed(spell, result)
    local bar = CastingBarFrame
    if not bar or not bar:IsShown() or bar.fadeOut or not (bar.casting or bar.channeling) or castSpell ~= spell then
        return
    end
    local spark, text = BarParts(bar)
    bar:SetValue(bar.maxValue or 0)
    bar:SetStatusBarColor(1.0, 0.0, 0.0)
    if spark then spark:Hide() end
    if text then
        local interrupted = result == SPELL_FAILED_INTERRUPTED or result == SPELL_FAILED_INTERRUPTED_COMBAT
        text:SetText(interrupted and INTERRUPTED or FAILED)
    end
    bar.casting = nil
    bar.channeling = nil
    bar.fadeOut = 1
    bar.holdTime = GetTime() + (CASTING_BAR_HOLD_TIME or 1)
    castSpell = nil
end

local function CastDelayed(ms)
    local bar = CastingBarFrame
    if bar and bar.casting and bar.value then
        bar.value = math.max(0, bar.value - ms / 1000)
    end
end

local function ChannelStart(spell, durationMs)
    local bar = CastingBarFrame
    if not bar or durationMs <= 0 then
        return
    end
    local name, _, icon = GetSpellInfo(spell)
    local spark, text, flash, barIcon = BarParts(bar)
    bar:SetStatusBarColor(0.0, 1.0, 0.0)
    bar.value = durationMs / 1000
    bar.maxValue = durationMs / 1000
    bar:SetMinMaxValues(0, bar.maxValue)
    bar:SetValue(bar.value)
    if text then text:SetText(name or "") end
    if barIcon then barIcon:SetTexture(icon) end
    if spark then spark:Hide() end
    if flash then flash:SetAlpha(0.0); flash:Hide() end
    bar:SetAlpha(1.0)
    bar.holdTime = 0
    bar.casting = nil
    bar.channeling = 1
    bar.flash = nil
    bar.fadeOut = nil
    if bar.showCastbar ~= false then
        bar:Show()
    end
    castSpell = spell
end

local function ChannelUpdate(remainingMs)
    local bar = CastingBarFrame
    if not bar or not bar.channeling then
        return
    end
    if remainingMs > 0 then
        bar.value = remainingMs / 1000
        return
    end
    local spark, _, flash = BarParts(bar)
    if spark then spark:Hide() end
    if flash then flash:SetAlpha(0.0); flash:Show() end
    bar:SetValue(bar.maxValue or 0)
    bar.channeling = nil
    bar.flash = 1
    bar.fadeOut = 1
    bar.holdTime = 0
    castSpell = nil
end

-- Spectating ends or moves to someone else: their cast is not yours.
local function CastClear()
    local bar = CastingBarFrame
    castSpell = nil
    if bar and (bar.casting or bar.channeling or bar.fadeOut) then
        bar.casting = nil
        bar.channeling = nil
        bar.flash = nil
        bar.fadeOut = nil
        bar:Hide()
    end
end

------------------------------------------------------------------------------
-- their frames: the player frame, the party frames, the enemy frames
------------------------------------------------------------------------------

-- The player frame shows them: the DLL makes them the focus
-- (CenturionSpectate("focus", guid) - FocusUnit is protected), and the player
-- frame reads "focus" the way it reads "vehicle" in a vehicle. The focus frame
-- would show them a second time, so it is out of sight meanwhile; your own
-- focus comes back after. The party frames are the server's (SMSG_GROUP_LIST
-- of their teammates).
local playerFrameBorrowed = false

local function SameGuid(a, b)
    return a and b and string.upper(a) == string.upper(b)
end

local function PaintPlayerFrame(unit)
    if PlayerLevelText and UnitLevel then
        PlayerLevelText:SetText(UnitLevel(unit))
    end
    -- UnitFramesImproved colours the bar by class and locks the colour.
    if PlayerFrameHealthBar and PlayerFrameHealthBar.lockColor and type(UnitColor) == "function" then
        PlayerFrameHealthBar:SetStatusBarColor(UnitColor(unit))
    end
end

-- What the client's DLL can do: 3 sets the focus as Blizzard's code would (2
-- knew "focus" but the client refused it from an addon, "blocked from an
-- action only available to the Blizzard UI"; older ones would take the word
-- for "stop following"). Asked while not following, where an older DLL does
-- nothing with it.
local FOCUS_DLL = 3
local dllVersion = nil

local function AskDllVersion()
    if dllVersion == nil and CenturionSpectate then
        dllVersion = tonumber((CenturionSpectate("version"))) or 0
    end
end

local function BorrowPlayerFrame(guid)
    if not CenturionSpectate or (dllVersion or 0) < FOCUS_DLL or not PlayerFrame or not UnitFrame_SetUnit or InCombatLockdown() then
        return
    end
    local focus = CenturionSpectate("focus", guid)
    if not SameGuid(focus, guid) then
        return
    end
    if FocusFrame then
        FocusFrame:SetAlpha(0)
        FocusFrame:EnableMouse(false)
    end
    UnitFrame_SetUnit(PlayerFrame, "focus", PlayerFrameHealthBar, PlayerFrameManaBar)
    PaintPlayerFrame("focus")
    playerFrameBorrowed = true
end

local function ReturnPlayerFrame()
    if playerFrameBorrowed then
        playerFrameBorrowed = false
        if not InCombatLockdown() then
            UnitFrame_SetUnit(PlayerFrame, "player", PlayerFrameHealthBar, PlayerFrameManaBar)
        end
        PaintPlayerFrame("player")
    end
    if CenturionSpectate and (dllVersion or 0) >= FOCUS_DLL then
        CenturionSpectate("focus", "off")
    end
    if FocusFrame then
        FocusFrame:SetAlpha(1)
        FocusFrame:EnableMouse(true)
    end
end

-- The enemy frames show their enemies: CenturionSquadFrames (FrameXML) draws
-- the stock enemy frames from lines the server feeds a real match (SQ / SQR /
-- SQH, its header says how); here the same lines are made from the replay's
-- own feed, from their side. A squad match keeps its squads; otherwise gold
-- (Alliance, 469) is squad 0 and green (Horde, 67) squad 1, the two-team
-- look with no headers.
local feedActive = false
local feedRoster = nil
local feedStates = {}
local feedWins = {}
local feedSeats = {}            -- squad -> seat -> guid, as the frames show them
-- Fed for the scoreboard alone (not spectating): the squad frames' Rounds Won
-- column and squad colours, with its enemy frames kept out of sight.
local scoreOnly = false

local function FeedLine(line)
    if CenturionSquadFrames_OnLine then
        CenturionSquadFrames_OnLine(line)
    end
end

local function FeedSquadOf(p, squadMatch)
    if squadMatch then
        local squad = tonumber(p.squad)
        return (squad and squad >= 0 and squad <= 3) and squad or nil
    end
    return tonumber(p.team) == 67 and 1 or 0
end

local function FeedMembers()
    local guids = {}
    local squadMatch = false
    for guid, p in pairs(ATPlayers or {}) do
        guids[#guids + 1] = guid
        local squad = tonumber(p.squad)
        if squad and squad >= 0 and squad <= 3 then
            squadMatch = true
        end
    end
    table.sort(guids)
    local squads, count = {}, squadMatch and 0 or 2
    for _, guid in ipairs(guids) do
        local p = ATPlayers[guid]
        p.guid = p.guid or guid
        local squad = FeedSquadOf(p, squadMatch)
        if squad then
            squads[squad] = squads[squad] or {}
            table.insert(squads[squad], p)
            if squad + 1 > count then
                count = squad + 1
            end
        end
    end
    return squads, count, squadMatch
end

local function FeedState(p)
    if tonumber(p.status) == 0 or (tonumber(p.health) or 1) <= 0 then
        return "d"
    end
    local health, maxHealth = tonumber(p.health) or 0, tonumber(p.maxhealth) or 0
    local power, maxPower = tonumber(p.power) or 0, tonumber(p.maxpower) or 0
    local hp = maxHealth > 0 and math.ceil(health * 100 / maxHealth) or 100
    local pw = maxPower > 0 and math.floor(power * 100 / maxPower) or 0
    return math.min(hp, 100) .. "." .. math.min(pw, 100) .. "." .. (tonumber(p.powertype) or 0)
end

local function FeedUpdate(guid)
    local me = guid and ATPlayers and ATPlayers[guid]
    if (guid and not me) or not ATPlayers or not CenturionSquadFrames_OnLine then
        return
    end
    local squads, count, squadMatch = FeedMembers()
    local mine = me and FeedSquadOf(me, squadMatch) or nil

    local roster = { (mine or "-") .. ":" .. count }
    local lines = {}
    for squad = 0, count - 1 do
        local items = {}
        for _, p in ipairs(squads[squad] or {}) do
            items[#items + 1] = (p.name or "?") .. "," .. (tonumber(p.class) or 0)
        end
        local name = CENTURION_SQUAD_NAMES and CENTURION_SQUAD_NAMES[squad] or ""
        lines[squad] = "SQR:" .. squad .. ":" .. name .. ":" .. table.concat(items, ";")
        roster[#roster + 1] = lines[squad]
    end
    local signature = table.concat(roster, "|")
    feedSeats = {}
    for squad = 0, count - 1 do
        feedSeats[squad] = {}
        for index, p in ipairs(squads[squad] or {}) do
            feedSeats[squad][index] = p.guid
        end
    end
    if signature ~= feedRoster then
        feedRoster = signature
        feedStates = {}
        feedWins = {}
        FeedLine("SQ:" .. (mine or "-") .. ":" .. count)
        for squad = 0, count - 1 do
            FeedLine(lines[squad])
        end
    end
    feedActive = true

    for squad = 0, count - 1 do
        local states = {}
        for _, p in ipairs(squads[squad] or {}) do
            states[#states + 1] = FeedState(p)
        end
        local line = "SQH:" .. squad .. ":" .. table.concat(states, ",")
        if feedStates[squad] ~= line then
            feedStates[squad] = line
            FeedLine(line)
        end

        -- Buffle Shuffle: the rounds each has won (the server's RWN lines).
        local wins, any = {}, false
        for _, p in ipairs(squads[squad] or {}) do
            wins[#wins + 1] = tonumber(p.wins) or 0
            any = any or p.wins ~= nil
        end
        local winsLine = any and ("SQV:" .. squad .. ":" .. table.concat(wins, ",")) or nil
        if winsLine and feedWins[squad] ~= winsLine then
            feedWins[squad] = winsLine
            FeedLine(winsLine)
        end
    end

    if scoreOnly and CenturionSquadFrames and CenturionSquadFrames:IsShown() and not InCombatLockdown() then
        CenturionSquadFrames:Hide()
    end
end

local function FeedStop()
    if feedActive then
        FeedLine("SQ:-")
    end
    feedActive = false
    feedRoster = nil
    feedStates = {}
    feedWins = {}
    feedSeats = {}
    scoreOnly = false
end

-- A right-click on one of those enemy frames spectates that player. The
-- frames are secure buttons (right-click = /focus them); this runs after, and
-- the focus then goes to whoever is watched.
for squad = 0, 3 do
    for index = 1, 5 do
        local button = _G["CenturionSquadFrame" .. squad .. "Member" .. index]
        if button and button.HookScript then
            button:HookScript("PostClick", function(self, mouseButton)
                if mouseButton ~= "RightButton" or not active then
                    return
                end
                local guid = feedSeats[squad] and feedSeats[squad][index]
                if guid and CenturionReplaySpectate_Start then
                    CenturionReplaySpectate_Start(guid)
                end
            end)
        end
    end
end

-- For CenturionReplayScore.lua: while the scoreboard is open and nobody is
-- spectated, the same feed with no side of your own (so no enemy frames
-- either), for the Rounds Won column and the squad colours on the board.
function CenturionReplaySpectate_ScoreFeed(on)
    if active then
        return                      -- spectating feeds it already, from their side
    end
    if on then
        if not scoreOnly then
            FeedStop()
            scoreOnly = true
        end
        FeedUpdate(nil)
    elseif scoreOnly then
        FeedStop()
    end
end

------------------------------------------------------------------------------
-- the camera
------------------------------------------------------------------------------

local function CameraFollow(guid)
    if CenturionSpectate then
        CenturionSpectate(guid)
        return true
    end
    if not warnedNoCamera then
        warnedNoCamera = true
        DEFAULT_CHAT_FRAME:AddMessage("|cffffcc00Spectate:|r this client has no follow camera yet - update it with the launcher. Their bars still show.")
    end
    return false
end

------------------------------------------------------------------------------
-- the API SunwellAS.lua calls
------------------------------------------------------------------------------

function CenturionReplaySpectate_IsActive()
    return active
end

function CenturionReplaySpectate_Start(guid)
    if not guid or not ATPlayers or not ATPlayers[guid] then
        return
    end
    if active and followed == guid then
        return
    end

    if not active and CREP_IsBlizzardUIHidden then
        uiWasHidden = CREP_IsBlizzardUIHidden() and true or false
    end
    if active then
        CastClear()   -- the last player's cast is not this one's
    end

    if not active then
        AskDllVersion()             -- before the follow: see dllVersion
    end
    QuietRecount()
    followed = guid
    active = true
    resumeGuid = nil
    CameraFollow(guid)
    ListenAtCamera()
    TellServer("SPECTATE " .. guid)

    if CREP_SetWatch then
        CREP_SetWatch(guid)
    end
    -- Their buttons are on the normal action bars: the normal UI shows.
    if CREP_SetBlizzardUIHidden then
        CREP_SetBlizzardUIHidden(false)
    end
    if CREP_SpectateButton and CREP_SpectateButton.text then
        CREP_SpectateButton.text:SetText("Stop spectating")
    end
    HideArenaFrames()
    BorrowPlayerFrame(guid)
    if scoreOnly then
        FeedStop()                  -- the scoreboard's feed becomes theirs
    end
    feedRoster = nil                -- their side, not the last player's
    FeedUpdate(guid)
    ShowStatus()
end

-- stopCamera false: the DLL already ended the follow (a movement key).
-- land: that key was a movement key, so the server puts you where they stand
-- (the DLL keeps the camera on them until you are there).
local function StopSpectating(stopCamera, tellServer, land)
    if not active then
        return
    end
    active = false
    followed = nil
    quietUntil = GetTime() + QUIET_AFTER
    if stopCamera and CenturionSpectate then
        CenturionSpectate("off")
    end
    ListenerBack()
    CastClear()
    ReturnPlayerFrame()
    FeedStop()
    if WorldStateScoreFrame and WorldStateScoreFrame:IsShown() then
        CenturionReplaySpectate_ScoreFeed(true)
    end
    if tellServer then
        TellServer(land and "SPECTATE OFF LAND" or "SPECTATE OFF")
    end
    status:Hide()
    for button in pairs(lit) do
        button:UnlockHighlight()
    end
    lit = {}
    if CREP_SpectateButton and CREP_SpectateButton.text then
        CREP_SpectateButton.text:SetText("Spectate")
    end
    if specingEnabled then
        if CREP_SetBlizzardUIHidden then
            CREP_SetBlizzardUIHidden(uiWasHidden)
        end
        if CREP_RestoreFrames then
            CREP_RestoreFrames()
        end
    end
end

function CenturionReplaySpectate_Stop()
    StopSpectating(true, true)
end

-- The replay UI went away: a restart (spectating picks up the same player once
-- the replay runs again) or leaving the replay (PLAYER_ENTERING_WORLD forgets
-- that). The server puts the viewer's bars back on its own in both cases.
function CenturionReplaySpectate_Reset()
    if active then
        resumeGuid = followed
    end
    StopSpectating(true, false)
    if CenturionSpectate then
        CenturionSpectate("disarm")
    end
end

function CenturionReplaySpectate_OnCommand(guid, prefix, value)
    if not active or guid ~= followed then
        return
    end
    value = value or ""
    if prefix == "SPE" then
        local spell, castTime = strsplit(",", value)
        spell = tonumber(spell)
        castTime = tonumber(castTime) or 0
        if spell and spell ~= 6603 and castTime < 99998 then
            LightSpell(spell)
            if castTime > 0 then
                CastStart(spell, castTime)
            end
        end
    elseif prefix == "CSG" then
        local spell = tonumber(value)
        if spell then
            CastSucceeded(spell)
        end
    elseif prefix == "CSF" then
        local spell, result = strsplit(",", value)
        spell = tonumber(spell)
        if spell then
            CastFailed(spell, tonumber(result) or 0)
        end
    elseif prefix == "CSD" then
        CastDelayed(tonumber(value) or 0)
    elseif prefix == "CHS" then
        local spell, duration = strsplit(",", value)
        spell = tonumber(spell)
        if spell then
            ChannelStart(spell, tonumber(duration) or 0)
        end
    elseif prefix == "CHU" then
        ChannelUpdate(tonumber(value) or 0)
    end
end

------------------------------------------------------------------------------
-- ticking
------------------------------------------------------------------------------

local ticker = CreateFrame("Frame")
local since = 0
local resumeTick = 0
local feedSince = 0
local FEED_EVERY = 0.25

ticker:SetScript("OnUpdate", function(self, elapsed)
    -- Right-click to spectate while a replay is showing.
    if specingEnabled and CenturionSpectate then
        CenturionSpectate("arm")
        local clicked = CenturionSpectate("click")
        if clicked and ATPlayers and ATPlayers[clicked] then
            CenturionReplaySpectate_Start(clicked)
        end
    end

    if active then
        -- A movement key ended the follow in the DLL (or a world reload did).
        if CenturionSpectate then
            local current, _, moved = CenturionSpectate("?")
            if not current then
                StopSpectating(false, true, moved == 1)
                return
            end
        end

        HideArenaFrames()

        feedSince = feedSince + elapsed
        if feedSince >= FEED_EVERY then
            feedSince = 0
            FeedUpdate(followed)
            -- A right-click on an enemy frame focuses them (/focus); the
            -- player frame stays on the watched player.
            if playerFrameBorrowed and UnitGUID and not SameGuid(UnitGUID("focus"), followed) then
                CenturionSpectate("focus", followed)
            elseif not playerFrameBorrowed then
                BorrowPlayerFrame(followed)   -- combat had it locked, or the unit was not here yet
            end
        end

        local now = GetTime()
        for button, untilTime in pairs(lit) do
            if now >= untilTime then
                button:UnlockHighlight()
                lit[button] = nil
            end
        end

        since = since + elapsed
        if since >= 0.5 then
            since = 0
            ShowStatus()
        end
        return
    end

    -- The scoreboard's feed keeps up while it is open.
    if scoreOnly then
        feedSince = feedSince + elapsed
        if feedSince >= FEED_EVERY then
            feedSince = 0
            FeedUpdate(nil)
        end
    end

    -- A restarted replay brings the actors back under the same guids.
    if resumeGuid then
        resumeTick = resumeTick + elapsed
        if resumeTick >= 0.5 then
            resumeTick = 0
            if specingEnabled and ATPlayers and ATPlayers[resumeGuid] then
                CenturionReplaySpectate_Start(resumeGuid)
            end
        end
    end
end)

local events = CreateFrame("Frame")
events:RegisterEvent("PLAYER_ENTERING_WORLD")
events:RegisterEvent("PLAYER_LOGOUT")
events:SetScript("OnEvent", function(self, event)
    if event == "PLAYER_LOGOUT" then
        ListenerBack()   -- CVars are saved at logout: never keep the spectate one
        return
    end
    resumeGuid = nil
end)
