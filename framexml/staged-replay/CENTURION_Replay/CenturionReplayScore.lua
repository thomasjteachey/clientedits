------------------------------------------------------------------------------
-- CenturionReplayScore.lua - the scoreboard, any time during a replay.
--
-- An arena keeps the scoreboard shut until the match is over
-- (ToggleWorldStateScoreFrame in WorldStateFrame.lua), and the server never
-- answers MSG_PVP_LOG_DATA mid-match. A replay is watched, not played, so
-- here it opens whenever you like: the Scoreboard button, or the scoreboard
-- key. The server (BGReplay.cpp) answers from the replay as it stands -
-- damage, healing and killing blows up to the moment you are watching - and
-- the board keeps asking every two seconds while it is open.
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
