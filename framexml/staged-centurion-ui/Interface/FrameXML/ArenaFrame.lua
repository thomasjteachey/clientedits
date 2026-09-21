-- Interface/FrameXML/ArenaFrame.lua
-- Extend Blizzard arena battlemaster frame to support 4 rated + 4 practice
-- while keeping the stock ArenaFrame.xml.

MAX_ARENA_TEAMS   = 4      -- 2v2, 3v3, 4v4, 5v5
MAX_ARENA_BATTLES = 8      -- 4 rated + 4 practice
NO_ARENA_SEASON   = 0

-- Blizzard's top list position
local RATED_X     = 23
local RATED_Y_TOP = -79

local function ArenaFrame_EnsureButtons()
    -- make sure the additional buttons exist

    -- 4th rated (5v5)
    if not _G.ArenaZone4 then
        local btn = CreateFrame("Button", "ArenaZone4", ArenaFrame, "ArenaButtonTemplate")
        btn:SetID(4)
        btn:SetScript("OnClick", ArenaButton_OnClick)
    end

    -- extra practice rows
    if not _G.ArenaZone7 then
        local btn = CreateFrame("Button", "ArenaZone7", ArenaFrame, "ArenaButtonTemplate")
        btn:SetID(7)
        btn:SetScript("OnClick", ArenaButton_OnClick)
    end
    if not _G.ArenaZone8 then
        local btn = CreateFrame("Button", "ArenaZone8", ArenaFrame, "ArenaButtonTemplate")
        btn:SetID(8)
        btn:SetScript("OnClick", ArenaButton_OnClick)
    end
end

local function ArenaFrame_Position()
    ArenaFrame_EnsureButtons()

    ----------------------------------------------------------------
    -- 1) Rated rows – force exact positions (top parchment)
    ----------------------------------------------------------------
    if ArenaZone1 then
        ArenaZone1:ClearAllPoints()
        ArenaZone1:SetPoint("TOPLEFT", ArenaFrame, "TOPLEFT", RATED_X, RATED_Y_TOP)
    end
    if ArenaZone2 then
        ArenaZone2:ClearAllPoints()
        ArenaZone2:SetPoint("TOPLEFT", ArenaZone1, "BOTTOMLEFT", 0, 0)
    end
    if ArenaZone3 then
        ArenaZone3:ClearAllPoints()
        ArenaZone3:SetPoint("TOPLEFT", ArenaZone2, "BOTTOMLEFT", 0, 0)
    end
    if ArenaZone4 then
        -- new 5v5 rated row
        ArenaZone4:ClearAllPoints()
        ArenaZone4:SetPoint("TOPLEFT", ArenaZone3, "BOTTOMLEFT", 0, 0)
    end

    ----------------------------------------------------------------
    -- 2) Practice rows – line up horizontally with rated, but sit
    --    under the "Practice Battle:" header. Drop a bit so they
    --    don't clip the grey border.
    ----------------------------------------------------------------
    if ArenaFrameNameHeader2 then
        -- row 5
        if ArenaZone5 then
            ArenaZone5:ClearAllPoints()
            -- horizontal: same as rated block
            ArenaZone5:SetPoint("LEFT", ArenaFrame, "LEFT", RATED_X, 0)
            -- vertical: under the header, but with extra gap (-8)
            ArenaZone5:SetPoint("TOP", ArenaFrameNameHeader2, "BOTTOM", 0, -10)
        end
        -- row 6
        if ArenaZone6 then
            ArenaZone6:ClearAllPoints()
            ArenaZone6:SetPoint("LEFT", ArenaFrame, "LEFT", RATED_X, 0)
            ArenaZone6:SetPoint("TOP", ArenaZone5, "BOTTOM", 0, 0)
        end
        -- row 7
        if ArenaZone7 then
            ArenaZone7:ClearAllPoints()
            ArenaZone7:SetPoint("LEFT", ArenaFrame, "LEFT", RATED_X, 0)
            ArenaZone7:SetPoint("TOP", ArenaZone6, "BOTTOM", 0, 0)
        end
        -- row 8
        if ArenaZone8 then
            ArenaZone8:ClearAllPoints()
            ArenaZone8:SetPoint("LEFT", ArenaFrame, "LEFT", RATED_X, 0)
            ArenaZone8:SetPoint("TOP", ArenaZone7, "BOTTOM", 0, 0)
        end
    end

    ----------------------------------------------------------------
    -- 3) Flavor text – in orange box, same left edge as lists
    ----------------------------------------------------------------
    if ArenaFrameZoneDescription and ArenaZone8 then
        ArenaFrameZoneDescription:ClearAllPoints()
        -- match left = 23, drop enough to be clearly in orange
        ArenaFrameZoneDescription:SetPoint("LEFT", ArenaFrame, "LEFT", RATED_X, 0)
        ArenaFrameZoneDescription:SetPoint("TOP", ArenaZone8, "BOTTOM", 0, -40)
        ArenaFrameZoneDescription:SetWidth(294)
        ArenaFrameZoneDescription:SetHeight(40)
    end
end

function ArenaFrame_OnLoad(self)
    self:RegisterEvent("BATTLEFIELDS_SHOW")
    self:RegisterEvent("BATTLEFIELDS_CLOSED")
    self:RegisterEvent("UPDATE_BATTLEFIELD_STATUS")
    self:RegisterEvent("PARTY_LEADER_CHANGED")

    ArenaFrame_Position()
end

function ArenaFrame_OnEvent(self, event, ...)
    if not IsBattlefieldArena() then
        return
    end

    if event == "BATTLEFIELDS_SHOW" then
        ShowUIPanel(ArenaFrame)

        -- make sure widgets are laid out
        ArenaFrame_Position()

        -- default selection
        if ((GetNumPartyMembers() > 0) or (GetNumRaidMembers() > 0))
            and IsPartyLeader()
            and GetCurrentArenaSeason() ~= NO_ARENA_SEASON
        then
            -- parties go to practice
            if not ArenaFrame.selection or ArenaFrame.selection < (MAX_ARENA_TEAMS + 1) then
                ArenaFrame.selection = MAX_ARENA_TEAMS + 1
            end
        else
            -- solo → 2v2 rated
            ArenaFrame.selection = ArenaFrame.selection or 1
        end

        -- standard text
        if ArenaFrameZoneDescription then
            if GetCurrentArenaSeason() == NO_ARENA_SEASON then
                ArenaFrameZoneDescription:SetText(ARENA_MASTER_NO_SEASON_TEXT)
            else
                ArenaFrameZoneDescription:SetText(ARENA_MASTER_TEXT)
            end
        end

        ArenaFrame_Update()

    elseif event == "BATTLEFIELDS_CLOSED" then
        HideUIPanel(ArenaFrame)
    else
        ArenaFrame_Update()
    end
end

function ArenaButton_OnClick(self)
    ArenaFrame.selection = self:GetID()
    ArenaFrame_Update()
end

function ArenaFrame_Update()
    -- 4 rated: 2s, 3s, 4s, 5s
    local ratedSizes = {
        [1] = 2,
        [2] = 3,
        [3] = 4,
        [4] = 5,
    }

    -- 4 practice: 2s, 3s, 4s, 5s skirmish
    local practiceLabels = {
        [1] = "2v2 Skirmish",
        [2] = "3v3 Skirmish",
        [3] = "4v4 Skirmish",
        [4] = "5v5 Skirmish",
    }

    for i = 1, MAX_ARENA_BATTLES do
        local button = _G["ArenaZone"..i]
        if button then
            if i <= MAX_ARENA_TEAMS then
                local size = ratedSizes[i]
                button:SetText(string.format(PVP_TEAMTYPE, size, size) .. " " .. ARENA_RATED)
                if GetCurrentArenaSeason() == NO_ARENA_SEASON then
                    button:Disable()
                else
                    button:Enable()
                end
            else
                local idx = i - MAX_ARENA_TEAMS
                button:SetText(practiceLabels[idx] or "Skirmish")
                button:Enable()
            end

            if ArenaFrame.selection == i then
                button:LockHighlight()
            else
                button:UnlockHighlight()
            end
        end
    end

    -- only practice queues are joinable from this frame
    if ArenaFrame.selection and ArenaFrame.selection > MAX_ARENA_TEAMS then
        ArenaFrameJoinButton:Enable()
        ArenaFrameGroupJoinButton:Show()
        if (((GetNumPartyMembers() > 0) or (GetNumRaidMembers() > 0)) and IsPartyLeader()) then
            ArenaFrameGroupJoinButton:Enable()
        else
            ArenaFrameGroupJoinButton:Disable()
        end
    else
        ArenaFrameJoinButton:Disable()
        ArenaFrameGroupJoinButton:Hide()
    end
end

function ArenaFrameJoinButton_OnClick(self)
    local GROUPJOIN_BUTTONID = 2

    if ArenaFrame.selection and ArenaFrame.selection <= MAX_ARENA_TEAMS then
        -- rated 1..4
        JoinBattlefield(ArenaFrame.selection, 1, 1)
    elseif ArenaFrame.selection then
        -- practice 5..8 → 1..4
        local idx = ArenaFrame.selection - MAX_ARENA_TEAMS
        if self:GetID() == GROUPJOIN_BUTTONID then
            JoinBattlefield(idx, 1)
        else
            JoinBattlefield(idx)
        end
    end

    HideUIPanel(ArenaFrame)
end

-- CENTURION: "Arena bot matches" - whether an unrated arena queue this
-- character is waiting in may be popped as a clone-filled skirmish.
--
--   CCGAMEREQ ARENABOTS[:0|1] -> CCGAME ARENABOTS:<on>
--
-- It sits on the Practice Battle line because that is the only queue it
-- touches: a rated arena is never filled. It stays hidden until the server
-- answers, so a realm that does not fill skirmishes - or a server built before
-- this existed - keeps the stock frame and no empty box.
--
-- ArenaFrame.xml is Blizzard's and is not edited, so the box is built here,
-- the same way this file already builds the extra zone buttons. Everything
-- hangs off OnLoad: the <Script> tag runs this file BEFORE the frame exists,
-- so ArenaFrame is still nil at this point.

-- The vertical middle of ArenaFrameNameHeader2. The divider hangs off the
-- frame's LEFT at (14,95) and the header off the divider at (14,-9), which puts
-- the middle of that 14-pixel GameFontHighlight line here. A constant for the
-- same reason RATED_Y_TOP is one: neither ever moves.
local PRACTICE_HEADER_Y = -177

CENTURION_ARENA_BOTS_LABEL = "Arena bot matches"
CENTURION_ARENA_BOTS_TOOLTIP = "Arena bot matches\n\nWhen a skirmish queue has nobody left to match you with, it is filled out with bots so the match starts anyway.\n\nUncheck to hold your place until real opponents queue. Rated arenas are never filled either way."

local function ArenaBots_Send(message)
    local playerName = UnitName("player")
    if SendAddonMessage and playerName then
        SendAddonMessage("CCGAMEREQ", message, "WHISPER", playerName)
    end
end

local function ArenaBots_Button()
    if _G.ArenaFrameArenaBots then
        return _G.ArenaFrameArenaBots
    end
    if not ArenaFrame then
        return nil
    end

    local cb = CreateFrame("CheckButton", "ArenaFrameArenaBots", ArenaFrame)
    cb:SetWidth(22)
    cb:SetHeight(22)
    cb:SetPoint("RIGHT", ArenaFrame, "TOPRIGHT", -40, PRACTICE_HEADER_Y)
    -- the label is part of the switch, as it is on the Battlegrounds tab
    cb:SetHitRectInsets(-110, 0, 0, 0)
    cb:SetNormalTexture("Interface\\Buttons\\UI-CheckBox-Up")
    cb:SetPushedTexture("Interface\\Buttons\\UI-CheckBox-Down")
    cb:SetHighlightTexture("Interface\\Buttons\\UI-CheckBox-Highlight", "ADD")
    cb:SetCheckedTexture("Interface\\Buttons\\UI-CheckBox-Check")
    cb:SetDisabledCheckedTexture("Interface\\Buttons\\UI-CheckBox-Check-Disabled")

    local text = cb:CreateFontString("ArenaFrameArenaBotsText", "ARTWORK", "GameFontNormalSmall")
    text:SetJustifyH("RIGHT")
    text:SetText(CENTURION_ARENA_BOTS_LABEL)
    text:SetPoint("RIGHT", cb, "LEFT", -1, 1)

    cb:SetScript("OnClick", function(self)
        if self:GetChecked() then
            ArenaBots_Send("ARENABOTS:1")
        else
            ArenaBots_Send("ARENABOTS:0")
        end
    end)
    cb:SetScript("OnEnter", function(self)
        GameTooltip:SetOwner(self, "ANCHOR_BOTTOMRIGHT", 5, 40)
        GameTooltip:SetText(CENTURION_ARENA_BOTS_TOOLTIP, nil, nil, nil, nil, 1)
    end)
    cb:SetScript("OnLeave", GameTooltip_Hide)

    cb:Hide()
    return cb
end

local function ArenaBots_Ask()
    if ArenaBots_Button() then
        ArenaBots_Send("ARENABOTS")
    end
end

local ArenaBotsDriver = CreateFrame("Frame")
ArenaBotsDriver:RegisterEvent("PLAYER_ENTERING_WORLD")
ArenaBotsDriver:RegisterEvent("CHAT_MSG_ADDON")
ArenaBotsDriver:SetScript("OnEvent", function(self, event, prefix, message)
    if event == "PLAYER_ENTERING_WORLD" then
        ArenaBots_Ask()
        return
    end

    if prefix ~= "CCGAME" or type(message) ~= "string" then
        return
    end

    local on = string.match(message, "^ARENABOTS:(%d)$")
    if on then
        local cb = ArenaBots_Button()
        if cb then
            if on == "1" then
                cb:SetChecked(1)
            else
                cb:SetChecked(nil)
            end
            cb:Show()
        end
    end
end)

-- Wrapped rather than edited into ArenaFrame_OnLoad above, so the stock
-- function and this are one append apart and neither has to know the other.
local ArenaBots_PreviousOnLoad = ArenaFrame_OnLoad
function ArenaFrame_OnLoad(self)
    ArenaBots_PreviousOnLoad(self)
    ArenaBots_Button()
    -- Asked again whenever the battlemaster's window opens, so the box is right
    -- even if the login answer was missed.
    self:HookScript("OnShow", ArenaBots_Ask)
end
