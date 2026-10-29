------------------------------------------------------------------------------
-- CenturionReplaySpectate.lua - spectating a replay player.
--
-- Right-click a player in the replay (in the world, or their arena frame), or
-- press Spectate: the camera follows them without turning when they turn, the
-- arena frames hide and the normal UI shows, and YOUR action bars become
-- theirs - their spells, items and macros (as the spell the macro casts), with
-- their cooldowns, lit when they press one. Right-click someone else to
-- switch. Move (W/A/S/D, strafe, autorun) and you are back in your own body
-- with your own bars.
--
-- The pieces:
--   * camera: CenturionSpectate() in the client-tweaks dinput8.dll - the
--     client's commentator camera, only for the camera itself; every other
--     control keeps working. The DLL also takes the right-click ("arm") and
--     ends the follow on a movement key.
--   * bars: the server (BGReplay.cpp) on "ASSUN SPECTATE <guid>" teaches the
--     client their spells and sends their bars and cooldowns as yours, the way
--     an All-Star life swaps bars; "SPECTATE OFF" puts yours back. Your saved
--     bars are never touched.
------------------------------------------------------------------------------

local active = false
local followed = nil
local resumeGuid = nil
local uiWasHidden = false
local quietUntil = 0
local QUIET_AFTER = 5
local warnedNoCamera = false

-- "You have learned a new spell" for every borrowed spell, and "unlearned"
-- after: All-Star's chat filter, for while spectating and a moment after.
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
for _, name in ipairs({ "ERR_LEARN_SPELL_S", "ERR_LEARN_ABILITY_S", "ERR_LEARN_PASSIVE_S", "ERR_SPELL_UNLEARNED_S" }) do
    AddPattern(name, "%%%%s")
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
status:SetPoint("TOP", WorldFrame, "TOP", 0, -30)
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

-- The mouse wheel zooms the follow camera.
local function OnZoom(sign, amount)
    if active and CenturionSpectate then
        CenturionSpectate("zoom", sign * 2 * (tonumber(amount) or 1))
    end
end
if hooksecurefunc then
    if CameraZoomIn then
        hooksecurefunc("CameraZoomIn", function(amount) OnZoom(-1, amount) end)
    end
    if CameraZoomOut then
        hooksecurefunc("CameraZoomOut", function(amount) OnZoom(1, amount) end)
    end
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

    followed = guid
    active = true
    resumeGuid = nil
    CameraFollow(guid)
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
    ShowStatus()
end

-- stopCamera false: the DLL already ended the follow (a movement key).
local function StopSpectating(stopCamera, tellServer)
    if not active then
        return
    end
    active = false
    followed = nil
    quietUntil = GetTime() + QUIET_AFTER
    if stopCamera and CenturionSpectate then
        CenturionSpectate("off")
    end
    if tellServer then
        TellServer("SPECTATE OFF")
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
    if prefix == "SPE" and active and guid == followed then
        local spell, castTime = strsplit(",", value or "")
        spell = tonumber(spell)
        castTime = tonumber(castTime) or 0
        if spell and spell ~= 6603 and castTime < 99998 then
            LightSpell(spell)
        end
    end
end

------------------------------------------------------------------------------
-- ticking
------------------------------------------------------------------------------

local ticker = CreateFrame("Frame")
local since = 0
local resumeTick = 0

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
            local current = CenturionSpectate("?")
            if not current then
                StopSpectating(false, true)
                return
            end
        end

        HideArenaFrames()

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
events:SetScript("OnEvent", function()
    resumeGuid = nil
end)
