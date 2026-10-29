------------------------------------------------------------------------------
-- CenturionReplayScore.lua - the scoreboard, any time during a replay.
--
-- An arena keeps the scoreboard shut until the match is over
-- (ToggleWorldStateScoreFrame in WorldStateFrame.lua), and the server never
-- answers MSG_PVP_LOG_DATA mid-match. A replay is watched, not played, so
-- here it opens whenever you like: the Scoreboard button, or the scoreboard
-- key. The server (BGReplay.cpp) answers from the replay as it stands -
-- damage, healing and killing blows up to the moment you are watching - and
-- the board keeps asking every two seconds while it is open. A Buffle
-- Shuffle's Rounds Won column comes from the server's RWN lines through the
-- squad frames' feed, as in a live match.
--
-- The default UI is usually hidden during a replay and the scoreboard is part
-- of it, so opening the board shows the UI; closing it hides the UI again if
-- the board was what showed it.
------------------------------------------------------------------------------

local REFRESH = 2
local shownUI = false

local function InReplay()
    return specingEnabled == true and WorldStateScoreFrame ~= nil
end

local function Ask()
    if RequestBattlefieldScoreData then
        RequestBattlefieldScoreData()
    end
end

function CenturionReplayScore_Open()
    if not InReplay() then
        return
    end
    if CREP_IsBlizzardUIHidden and CREP_IsBlizzardUIHidden() then
        shownUI = true
        CREP_SetBlizzardUIHidden(false)
    end
    Ask()
    ShowUIPanel(WorldStateScoreFrame)
    if WorldStateScoreFrame_Update then
        WorldStateScoreFrame_Update()
        WorldStateScoreFrame_Resize()
    end
    -- Buffle Shuffle's Rounds Won column and Four Corners' squad colours come
    -- from CenturionSquadFrames' feed (CenturionReplaySpectate.lua).
    if CenturionReplaySpectate_ScoreFeed then
        CenturionReplaySpectate_ScoreFeed(true)
    end
end

function CenturionReplayScore_Toggle()
    if not InReplay() then
        return
    end
    if WorldStateScoreFrame:IsShown() then
        HideUIPanel(WorldStateScoreFrame)
    else
        CenturionReplayScore_Open()
    end
end

-- The scoreboard key: the replay's board, else the stock rule.
if ToggleWorldStateScoreFrame then
    local stockToggle = ToggleWorldStateScoreFrame
    ToggleWorldStateScoreFrame = function(...)
        if InReplay() then
            CenturionReplayScore_Toggle()
            return
        end
        return stockToggle(...)
    end
end

local refresher = CreateFrame("Frame")
local since = 0
refresher:SetScript("OnUpdate", function(self, elapsed)
    if not InReplay() or not WorldStateScoreFrame:IsShown() then
        since = 0
        return
    end
    since = since + elapsed
    if since >= REFRESH then
        since = 0
        Ask()
    end
end)

if WorldStateScoreFrame then
    WorldStateScoreFrame:HookScript("OnHide", function()
        if CenturionReplaySpectate_ScoreFeed then
            CenturionReplaySpectate_ScoreFeed(false)
        end
        if not shownUI then
            return
        end
        shownUI = false
        -- Spectating keeps the UI up for its bars; it puts the UI back itself.
        local spectating = CenturionReplaySpectate_IsActive and CenturionReplaySpectate_IsActive()
        if InReplay() and not spectating and CREP_SetBlizzardUIHidden then
            CREP_SetBlizzardUIHidden(true)
        end
    end)
end

------------------------------------------------------------------------------
-- The arena layout. A replay viewer's client is not in an arena as far as it
-- knows: the server sends no arena status to a spectator after the loading
-- screen (Battleground::SendSpectatorBattlefieldStatusTo - an active arena
-- turned a spectator's names blue), so the board draws the battleground
-- columns (Deaths, Honorable Kills, a Team "A" for everyone) and the squad
-- frames' Rounds Won column, an arena one, never appears. After the stock
-- update, a replay's board is laid out the way an unrated arena's is, with
-- Rounds Won after the names when the replay has them (Buffle Shuffle).
------------------------------------------------------------------------------

local WINS_TEXT = "Rounds\nWon"

local function ReplayWinsByName()
    local wins = nil
    for _, p in pairs(ATPlayers or {}) do
        if p.wins ~= nil and p.name then
            wins = wins or {}
            wins[p.name] = tonumber(p.wins) or 0
        end
    end
    return wins
end

local function ArenaColours(index, nameText, left, right)
    local _, _, _, _, _, faction = GetBattlefieldScore(index)
    if faction == 0 then            -- green
        left:SetVertexColor(0.19, 0.57, 0.11)
        right:SetVertexColor(0.19, 0.57, 0.11)
        nameText:SetVertexColor(0.1, 1.0, 0.1)
    elseif faction == 1 then        -- gold
        left:SetVertexColor(0.85, 0.71, 0.26)
        right:SetVertexColor(0.85, 0.71, 0.26)
        nameText:SetVertexColor(1, 0.82, 0)
    end
end

local function ReplayWidth(wins)
    local columns = 3 + 1 + (GetNumBattlefieldStats and GetNumBattlefieldStats() or 0) + (wins and 1 or 0)
    local width = (WORLDSTATESCOREFRAME_PADDING or 35) + WorldStateScoreFrameName:GetWidth()
        + WorldStateScoreFrameClass:GetWidth() + 43 + columns * (WORLDSTATESCOREFRAME_COLUMN_SPACING or 76)
    if WorldStateScoreScrollFrame and WorldStateScoreScrollFrame:IsShown() then
        width = width + 37
    end
    return width
end

local function ReplayLayout()
    if not InReplay() or (IsActiveBattlefieldArena and IsActiveBattlefieldArena()) then
        return
    end
    local wins = ReplayWinsByName()

    for _, name in ipairs({ "WorldStateScoreFrameTab1", "WorldStateScoreFrameTab2", "WorldStateScoreFrameTab3",
        "WorldStateScoreFrameTeam", "WorldStateScoreFrameTeamSkill", "WorldStateScoreFrameDeaths", "WorldStateScoreFrameHK" }) do
        local f = _G[name]
        if f then f:Hide() end
    end

    -- Moved in this order: Killing Blows hung from Rounds Won while that
    -- still trails Healing Done would be an anchor loop.
    if wins then
        WorldStateScoreFrameHonorGained:SetPoint("LEFT", "WorldStateScoreFrameName", "RIGHT", 4, 0)
        WorldStateScoreFrameKB:SetPoint("LEFT", "WorldStateScoreFrameHonorGained", "RIGHT", -5, 0)
        WorldStateScoreFrameHonorGainedText:SetText(CENTURION_SHUFFLE_WINS or WINS_TEXT)
        WorldStateScoreFrameHonorGained.tooltip = CENTURION_SHUFFLE_WINS_TOOLTIP or "Rounds won"
        WorldStateScoreFrameHonorGained.sortType = nil
        WorldStateScoreFrameHonorGained:Show()
    else
        WorldStateScoreFrameHonorGained:Hide()
        WorldStateScoreFrameKB:SetPoint("LEFT", "WorldStateScoreFrameName", "RIGHT", 4, 0)
    end
    WorldStateScoreFrameDamageDone:SetPoint("LEFT", "WorldStateScoreFrameKB", "RIGHT", -5, 0)

    local numScores = GetNumBattlefieldScores()
    local offset = FauxScrollFrame_GetOffset(WorldStateScoreScrollFrame)
    for i = 1, MAX_WORLDSTATE_SCORE_BUTTONS do
        local row = "WorldStateScoreButton" .. i
        for _, part in ipairs({ "HonorableKills", "Deaths", "Team", "TeamSkill" }) do
            local f = _G[row .. part]
            if f then f:Hide() end
        end
        local index = offset + i
        if index <= numScores then
            local nameButton = _G[row .. "Name"]
            local honor = _G[row .. "HonorGained"]
            if wins then
                local w = nameButton and nameButton.name and wins[nameButton.name]
                honor:SetText(w and tostring(w) or "")
                honor:Show()
            else
                honor:Hide()
            end
            ArenaColours(index, _G[row .. "NameText"], _G[row .. "FactionLeft"], _G[row .. "FactionRight"])
        end
    end
    -- A squad match's colours (Four Corners) over the two sides'.
    if CenturionSquadFrames_ColourScoreboard then
        CenturionSquadFrames_ColourScoreboard()
    end
    WorldStateScorePlayerCount:Hide()
end

if type(WorldStateScoreFrame_Update) == "function" then
    hooksecurefunc("WorldStateScoreFrame_Update", ReplayLayout)
end

-- The stock resize sizes the board for the battleground columns; a replay's
-- is the arena width (and one column more for Rounds Won).
if type(WorldStateScoreFrame_Resize) == "function" then
    hooksecurefunc("WorldStateScoreFrame_Resize", function(width)
        if width or not InReplay() or (IsActiveBattlefieldArena and IsActiveBattlefieldArena()) then
            return
        end
        WorldStateScoreFrame_Resize(ReplayWidth(ReplayWinsByName()))
    end)
end
