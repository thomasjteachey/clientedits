------------------------------------------------------------------------------
-- CenturionReplaySpectate.lua - the replay's spectate view.
--
-- Spectating a replay player:
--   * the camera follows them without turning when they turn (the client's
--     commentator camera, CenturionSpectate in the client-tweaks dinput8.dll):
--     the mouse turns it round them, W / S and the mouse wheel bring it in/out
--   * their action bars show at the bottom - the spells, items and macros they
--     had on their buttons, lit when they press one, with the cooldowns their
--     server-side spell history had (BGReplay.cpp records all three)
--   * the arena frames are hidden; a strip at the top picks who to follow
--
-- Start: right-click a player's arena frame, or the Spectate button (the POV
-- player). Click a name in the strip, or target someone in the world, to switch.
-- Stop: the X in the strip, the Spectate button again, or Escape while the
-- Blizzard UI is shown (with it hidden, Escape first brings it back).
--
-- Server feed (ASSUN, per replay player guid):
--   BRS=<count>                   a new set of buttons follows
--   BAR=slot:type:action,...      buttons (type 0 spell, 128 item, 64/65 macro)
--   BMC=slot^icon^spell^name      what a macro button shows / casts
--   BCD=kind:id:remainingMs       cooldown (kind 0 spell, 1 item; 0 ms = cleared)
--   FRM=<shapeshift form>         picks the stance/form page of the main bar
--   SPE=spell,castTime            (SunwellAS also uses it) a cast: light it
------------------------------------------------------------------------------

local BUTTON_SIZE = 36
local BUTTON_GAP = 4
local ROW_WIDTH = 12 * (BUTTON_SIZE + BUTTON_GAP) - BUTTON_GAP
local QUESTION_MARK = "Interface\\Icons\\INV_Misc_QuestionMark"

-- Shapeshift form -> bonus action bar (SpellShapeshiftForm.dbc BonusActionBar):
-- the main bar shows slots 72 + (bar - 1) * 12 .. + 11 in that form.
local FORM_BONUS_BAR = {
    [1]  = 1,   -- Cat Form
    [5]  = 3,   -- Bear Form
    [8]  = 3,   -- Dire Bear Form
    [31] = 4,   -- Moonkin Form
    [17] = 1,   -- Battle Stance
    [18] = 2,   -- Defensive Stance
    [19] = 3,   -- Berserker Stance
    [30] = 1,   -- Stealth
    [28] = 1,   -- Shadowform
}

local CLASS_TOKENS = {
    [1] = "WARRIOR", [2] = "PALADIN", [3] = "HUNTER", [4] = "ROGUE", [5] = "PRIEST",
    [6] = "DEATHKNIGHT", [7] = "SHAMAN", [8] = "MAGE", [9] = "WARLOCK", [11] = "DRUID",
}

local POWER_COLORS = {
    [0] = { 0.27, 0.17, 0.98 },
    [1] = { 0.81, 0.01, 0.10 },
    [2] = { 1.00, 0.50, 0.25 },
    [3] = { 0.80, 0.80, 0.00 },
    [6] = { 0.00, 0.70, 0.90 },
}

local bars = {}        -- guid -> { [slot] = { type, id, macroIcon, macroSpell, macroName } }
local forms = {}       -- guid -> shapeshift form
local cooldowns = {}   -- guid -> { ["s"..spell | "i"..item] = { start, duration } }
local casts = {}       -- guid -> { spell, start, duration }

local active = false
local followed = nil
local resumeGuid = nil
local dirty = true
local warnedNoCamera = false
local suppressHide = false

local function ClassColor(class)
    local token = CLASS_TOKENS[tonumber(class) or 0]
    local c = token and RAID_CLASS_COLORS and RAID_CLASS_COLORS[token]
    if c then
        return c.r, c.g, c.b
    end
    return 1, 1, 1
end

local function SideColor(p)
    local squad = ReplayUIGetSquad and ReplayUIGetSquad(p)
    if squad and REPLAY_UI_SQUAD_COLORS then
        return unpack(REPLAY_UI_SQUAD_COLORS[squad])
    end
    if tonumber(p.team) == 67 then
        return 1.0, 0.25, 0.25
    end
    return 0.25, 0.55, 1.0
end

------------------------------------------------------------------------------
-- frames
------------------------------------------------------------------------------

local panel = CreateFrame("Frame", "CenturionReplaySpectateFrame", WorldFrame)
panel:SetFrameStrata("LOW")
panel:SetAllPoints(WorldFrame)
panel:EnableMouse(false)
panel:Hide()

-- Escape stops spectating (UIParent is hidden in a replay, but the special
-- frames list is still walked).
if UISpecialFrames then
    table.insert(UISpecialFrames, "CenturionReplaySpectateFrame")
end

local function MakeBar(parent, width, height, r, g, b)
    local bar = CreateFrame("StatusBar", nil, parent)
    bar:SetWidth(width)
    bar:SetHeight(height)
    bar:SetStatusBarTexture("Interface\\TargetingFrame\\UI-StatusBar")
    bar:SetStatusBarColor(r, g, b)
    bar:SetMinMaxValues(0, 1)
    bar:SetValue(1)
    bar.bg = bar:CreateTexture(nil, "BACKGROUND")
    bar.bg:SetAllPoints(bar)
    bar.bg:SetTexture(0, 0, 0, 0.6)
    bar.text = bar:CreateFontString(nil, "OVERLAY")
    bar.text:SetFont(STANDARD_TEXT_FONT, 11, "OUTLINE")
    bar.text:SetPoint("CENTER", 0, 0)
    return bar
end

-- The followed player: name, health, power, cast.
local header = CreateFrame("Frame", nil, panel)
header:SetWidth(ROW_WIDTH)
header:SetHeight(44)
header.name = header:CreateFontString(nil, "OVERLAY")
header.name:SetFont(STANDARD_TEXT_FONT, 15, "OUTLINE")
header.name:SetPoint("BOTTOMLEFT", header, "TOPLEFT", 0, 2)
header.health = MakeBar(header, ROW_WIDTH, 16, 0.1, 0.8, 0.2)
header.health:SetPoint("TOPLEFT", header, "TOPLEFT", 0, 0)
header.power = MakeBar(header, ROW_WIDTH, 10, 0.27, 0.17, 0.98)
header.power:SetPoint("TOPLEFT", header.health, "BOTTOMLEFT", 0, -2)
header.cast = MakeBar(header, ROW_WIDTH, 12, 0.85, 0.70, 0.20)
header.cast:SetPoint("TOPLEFT", header.power, "BOTTOMLEFT", 0, -2)
header.cast:Hide()

local function MakeButton(parent)
    local b = CreateFrame("Frame", nil, parent)
    b:SetWidth(BUTTON_SIZE)
    b:SetHeight(BUTTON_SIZE)
    b.bg = b:CreateTexture(nil, "BACKGROUND")
    b.bg:SetAllPoints(b)
    b.bg:SetTexture(0, 0, 0, 0.55)
    b.icon = b:CreateTexture(nil, "ARTWORK")
    b.icon:SetPoint("TOPLEFT", 1, -1)
    b.icon:SetPoint("BOTTOMRIGHT", -1, 1)
    b.icon:SetTexCoord(0.07, 0.93, 0.07, 0.93)
    b.cooldown = CreateFrame("Cooldown", nil, b)
    b.cooldown:SetAllPoints(b.icon)
    b.flash = b:CreateTexture(nil, "OVERLAY")
    b.flash:SetAllPoints(b)
    b.flash:SetTexture("Interface\\Buttons\\ButtonHilight-Square")
    b.flash:SetBlendMode("ADD")
    b.flash:SetAlpha(0)
    b.flashUntil = 0
    b.label = b:CreateFontString(nil, "OVERLAY")
    b.label:SetFont(STANDARD_TEXT_FONT, 9, "OUTLINE")
    b.label:SetPoint("BOTTOM", 0, 2)
    b.label:SetWidth(BUTTON_SIZE)
    b.label:SetHeight(10)
    return b
end

-- Rows as the default UI lays them out: the main bar (or the form page) at
-- the bottom, Bottom Left (61-72) and Bottom Right (49-60) above it, Right
-- (25-36) and Right 2 (37-48) as columns on the right edge.
local ROWS = {
    { key = "main", first = 0 },
    { key = "bottomleft", first = 60 },
    { key = "bottomright", first = 48 },
}
local COLUMNS = {
    { key = "right", first = 24 },
    { key = "right2", first = 36 },
}

local rowFrames = {}
for i, row in ipairs(ROWS) do
    local f = CreateFrame("Frame", nil, panel)
    f:SetWidth(ROW_WIDTH)
    f:SetHeight(BUTTON_SIZE)
    f.buttons = {}
    for n = 1, 12 do
        local b = MakeButton(f)
        b:SetPoint("LEFT", f, "LEFT", (n - 1) * (BUTTON_SIZE + BUTTON_GAP), 0)
        f.buttons[n] = b
    end
    f.row = row
    rowFrames[i] = f
end

local columnFrames = {}
for i, column in ipairs(COLUMNS) do
    local f = CreateFrame("Frame", nil, panel)
    f:SetWidth(BUTTON_SIZE)
    f:SetHeight(ROW_WIDTH)
    f.buttons = {}
    for n = 1, 12 do
        local b = MakeButton(f)
        b:SetPoint("TOP", f, "TOP", 0, -(n - 1) * (BUTTON_SIZE + BUTTON_GAP))
        f.buttons[n] = b
    end
    f.column = column
    columnFrames[i] = f
end

-- Who to follow: a name per replay player, coloured by class, edged by side.
local picker = CreateFrame("Frame", nil, panel)
picker:SetPoint("TOP", WorldFrame, "TOP", 0, -26)
picker:SetWidth(10)
picker:SetHeight(22)
picker.buttons = {}

local stopButton = CreateFrame("Button", nil, picker)
stopButton:SetWidth(22)
stopButton:SetHeight(22)
stopButton.bg = stopButton:CreateTexture(nil, "BACKGROUND")
stopButton.bg:SetAllPoints(stopButton)
stopButton.bg:SetTexture(0.55, 0.15, 0.15, 0.9)
stopButton.text = stopButton:CreateFontString(nil, "OVERLAY")
stopButton.text:SetFont(STANDARD_TEXT_FONT, 14, "OUTLINE")
stopButton.text:SetPoint("CENTER", 0, 0)
stopButton.text:SetText("X")

local hint = picker:CreateFontString(nil, "OVERLAY")
hint:SetFont(STANDARD_TEXT_FONT, 11, "OUTLINE")
hint:SetTextColor(0.8, 0.8, 0.8)
hint:SetText("Mouse: turn the camera   W/S or wheel: closer/further   X: stop")

local function PickerButton(i)
    local b = picker.buttons[i]
    if b then
        return b
    end
    b = CreateFrame("Button", nil, picker)
    b:SetWidth(96)
    b:SetHeight(20)
    b.bg = b:CreateTexture(nil, "BACKGROUND")
    b.bg:SetAllPoints(b)
    b.edge = b:CreateTexture(nil, "BORDER")
    b.edge:SetPoint("BOTTOMLEFT", 0, 0)
    b.edge:SetPoint("BOTTOMRIGHT", 0, 0)
    b.edge:SetHeight(3)
    b.text = b:CreateFontString(nil, "OVERLAY")
    b.text:SetFont(STANDARD_TEXT_FONT, 11, "OUTLINE")
    b.text:SetPoint("CENTER", 0, 1)
    b.text:SetWidth(92)
    b.text:SetHeight(14)
    b:SetScript("OnClick", function(self)
        if self.guid then
            CenturionReplaySpectate_Start(self.guid)
        end
    end)
    picker.buttons[i] = b
    return b
end

------------------------------------------------------------------------------
-- the buttons
------------------------------------------------------------------------------

local function ButtonIcon(entry)
    if not entry then
        return nil
    end
    if entry.type == 0 then
        local _, _, icon = GetSpellInfo(entry.id)
        return icon or QUESTION_MARK
    end
    if entry.type == 128 then
        return GetItemIcon(entry.id) or QUESTION_MARK
    end
    if entry.type == 64 or entry.type == 65 then
        local icon = entry.macroIcon
        local plain = not icon or icon == "" or icon:upper() == "INV_MISC_QUESTIONMARK"
        if plain and entry.macroSpell and entry.macroSpell > 0 then
            local _, _, spellIcon = GetSpellInfo(entry.macroSpell)
            if spellIcon then
                return spellIcon
            end
        end
        if icon and icon ~= "" then
            if icon:find("\\") then
                return icon
            end
            return "Interface\\Icons\\" .. icon
        end
        return QUESTION_MARK
    end
    return nil
end

-- What a button's cooldown is kept under, and the spell it casts.
local function ButtonKeys(entry)
    if not entry then
        return nil, nil
    end
    if entry.type == 0 then
        return "s" .. entry.id, entry.id
    end
    if entry.type == 128 then
        return "i" .. entry.id, nil
    end
    if (entry.type == 64 or entry.type == 65) and entry.macroSpell and entry.macroSpell > 0 then
        return "s" .. entry.macroSpell, entry.macroSpell
    end
    return nil, nil
end

local function MainBarFirstSlot(guid)
    local bonus = FORM_BONUS_BAR[forms[guid] or 0]
    if not bonus then
        return 0
    end
    local first = 72 + (bonus - 1) * 12
    local set = bars[guid]
    if set then
        for slot = first, first + 11 do
            if set[slot] then
                return first
            end
        end
    end
    return 0   -- an empty form page: the client would show it empty, the
               -- plain bar is more use to a viewer
end

local function FillButtons(frame, set, first)
    local any = false
    for n = 1, 12 do
        local b = frame.buttons[n]
        local entry = set and set[first + n - 1]
        b.entry = entry
        local icon = ButtonIcon(entry)
        if icon then
            b.icon:SetTexture(icon)
            b.icon:Show()
            any = true
        else
            b.icon:Hide()
        end
        b.key, b.spell = ButtonKeys(entry)
        b.spellName = b.spell and GetSpellInfo(b.spell) or nil
        b.label:SetText(entry and (entry.type == 64 or entry.type == 65) and entry.macroName or "")
        b.cdStart, b.cdDuration = nil, nil
        b.cooldown:SetCooldown(0, 0)
        b.flash:SetAlpha(0)
        b.flashUntil = 0
    end
    return any
end

local function LayoutBars()
    local guid = followed
    local set = guid and bars[guid]

    -- Bottom up: the main row, then whichever of the other two rows has
    -- buttons; the header over the top row.
    local y = 10
    local top = nil
    for i, f in ipairs(rowFrames) do
        local first = f.row.key == "main" and MainBarFirstSlot(guid) or f.row.first
        local any = FillButtons(f, set, first)
        if f.row.key == "main" or any then
            f:ClearAllPoints()
            f:SetPoint("BOTTOM", WorldFrame, "BOTTOM", 0, y)
            f:Show()
            y = y + BUTTON_SIZE + BUTTON_GAP
            top = f
        else
            f:Hide()
        end
    end

    header:ClearAllPoints()
    header:SetPoint("BOTTOM", top, "TOP", 0, 34)

    local x = -10
    for _, f in ipairs(columnFrames) do
        local any = FillButtons(f, set, f.column.first)
        if any then
            f:ClearAllPoints()
            f:SetPoint("RIGHT", WorldFrame, "RIGHT", x, 0)
            f:Show()
            x = x - BUTTON_SIZE - BUTTON_GAP
        else
            f:Hide()
        end
    end
end

local function EachButton(fn)
    for _, f in ipairs(rowFrames) do
        if f:IsShown() then
            for _, b in ipairs(f.buttons) do
                fn(b)
            end
        end
    end
    for _, f in ipairs(columnFrames) do
        if f:IsShown() then
            for _, b in ipairs(f.buttons) do
                fn(b)
            end
        end
    end
end

local function RefreshCooldowns()
    local guid = followed
    local now = GetTime()
    local list = guid and cooldowns[guid]
    EachButton(function(b)
        local cd = b.key and list and list[b.key]
        if cd and now < cd.start + cd.duration then
            if b.cdStart ~= cd.start or b.cdDuration ~= cd.duration then
                b.cdStart, b.cdDuration = cd.start, cd.duration
                b.cooldown:SetCooldown(cd.start, cd.duration)
            end
        elseif b.cdStart then
            b.cdStart, b.cdDuration = nil, nil
            b.cooldown:SetCooldown(0, 0)
        end
    end)
end

local function FlashSpell(spellId)
    local name = GetSpellInfo(spellId)
    EachButton(function(b)
        if b.spell and (b.spell == spellId or (name and b.spellName == name)) then
            b.flashUntil = GetTime() + 0.35
            b.flash:SetAlpha(1)
        end
    end)
end

------------------------------------------------------------------------------
-- the picker and the header
------------------------------------------------------------------------------

local function SortedPlayers()
    local list = {}
    if not ATPlayers then
        return list
    end
    for guid, p in pairs(ATPlayers) do
        if p.name and p.name ~= "UNNAMED" then
            table.insert(list, p)
        end
    end
    table.sort(list, function(a, b)
        local sa = (ReplayUIGetSquad and ReplayUIGetSquad(a)) or (tonumber(a.team) or 0)
        local sb = (ReplayUIGetSquad and ReplayUIGetSquad(b)) or (tonumber(b.team) or 0)
        if sa ~= sb then
            return sa < sb
        end
        return a.name < b.name
    end)
    return list
end

local function LayoutPicker()
    local list = SortedPlayers()
    local perRow = 10
    local count = #list
    local inRow = math.min(perRow, math.max(count, 1))
    local width = inRow * 98 + 26
    picker:SetWidth(width)

    for i, p in ipairs(list) do
        local b = PickerButton(i)
        local row = math.floor((i - 1) / perRow)
        local col = (i - 1) % perRow
        b:ClearAllPoints()
        b:SetPoint("TOPLEFT", picker, "TOPLEFT", col * 98, -row * 22)
        b.guid = p.guid
        b.text:SetText(p.name)
        b.text:SetTextColor(ClassColor(p.class))
        b.edge:SetTexture(SideColor(p))
        if p.guid == followed then
            b.bg:SetTexture(0.35, 0.35, 0.10, 0.9)
        else
            b.bg:SetTexture(0, 0, 0, 0.7)
        end
        b:SetAlpha((tonumber(p.status) or 1) == 0 and 0.5 or 1)
        b:Show()
    end
    for i = count + 1, #picker.buttons do
        picker.buttons[i]:Hide()
    end

    stopButton:ClearAllPoints()
    stopButton:SetPoint("TOPLEFT", picker, "TOPLEFT", inRow * 98, 0)
    local rows = math.max(1, math.ceil(count / perRow))
    hint:ClearAllPoints()
    hint:SetPoint("TOP", picker, "TOP", 0, -rows * 22 - 4)
end

local function RefreshHeader()
    local p = followed and ATPlayers and ATPlayers[followed]
    if not p then
        header.name:SetText("")
        return
    end

    header.name:SetText(p.name or "")
    header.name:SetTextColor(ClassColor(p.class))

    local maxHealth = math.max(tonumber(p.maxhealth) or 1, 1)
    local health = math.max(tonumber(p.health) or 0, 0)
    header.health:SetMinMaxValues(0, maxHealth)
    header.health:SetValue(health)
    header.health:SetStatusBarColor(ClassColor(p.class))
    header.health.text:SetText((tonumber(p.status) or 1) == 0 and "DEAD" or (health .. " / " .. maxHealth))

    local maxPower = math.max(tonumber(p.maxpower) or 1, 1)
    local power = math.max(tonumber(p.power) or 0, 0)
    header.power:SetMinMaxValues(0, maxPower)
    header.power:SetValue(power)
    local c = POWER_COLORS[tonumber(p.powertype) or 0] or POWER_COLORS[0]
    header.power:SetStatusBarColor(c[1], c[2], c[3])
    header.power.text:SetText(power .. " / " .. maxPower)

    local cast = casts[followed]
    local now = GetTime()
    if cast and now < cast.start + cast.duration then
        local name = GetSpellInfo(cast.spell) or ""
        header.cast:SetMinMaxValues(0, cast.duration)
        header.cast:SetValue(now - cast.start)
        header.cast.text:SetText(name)
        header.cast:Show()
    else
        header.cast:Hide()
    end
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
-- the camera
------------------------------------------------------------------------------

local function CameraFollow(guid)
    if CenturionSpectate then
        CenturionSpectate(guid)
        return true
    end
    if not warnedNoCamera then
        warnedNoCamera = true
        DEFAULT_CHAT_FRAME:AddMessage("|cffffcc00Spectate:|r this client has no follow camera yet - update it with the launcher. The bars still show.")
    end
    return false
end

local function CameraStop()
    if CenturionSpectate then
        CenturionSpectate("off")
    end
end

-- The mouse wheel zooms the follow camera instead of the normal one.
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
    local switched = followed ~= guid
    followed = guid
    active = true
    resumeGuid = nil
    CameraFollow(guid)
    if CREP_SetWatch then
        CREP_SetWatch(guid)
    end
    if CREP_SpectateButton and CREP_SpectateButton.text then
        CREP_SpectateButton.text:SetText("Stop spectating")
    end
    panel:Show()
    HideArenaFrames()
    if switched then
        dirty = true
    end
    LayoutPicker()
end

function CenturionReplaySpectate_Stop()
    if not active then
        return
    end
    active = false
    followed = nil
    CameraStop()
    suppressHide = true
    panel:Hide()
    suppressHide = false
    if CREP_SpectateButton and CREP_SpectateButton.text then
        CREP_SpectateButton.text:SetText("Spectate")
    end
    if CREP_RestoreFrames then
        CREP_RestoreFrames()
    end
end

-- The replay UI went away: a restart (spectating resumes on the same player
-- once the replay starts again) or leaving the replay (PLAYER_ENTERING_WORLD
-- forgets that).
function CenturionReplaySpectate_Reset()
    if active then
        resumeGuid = followed
    end
    CenturionReplaySpectate_Stop()
    bars, forms, cooldowns, casts = {}, {}, {}, {}
end

function CenturionReplaySpectate_OnCommand(guid, prefix, value)
    value = value or ""
    if prefix == "BRS" then
        bars[guid] = {}
    elseif prefix == "BAR" then
        local set = bars[guid] or {}
        bars[guid] = set
        for slot, kind, id in string.gmatch(value, "(%d+):(%d+):(%d+)") do
            local entry = set[tonumber(slot)] or {}
            entry.type = tonumber(kind)
            entry.id = tonumber(id)
            set[tonumber(slot)] = entry
        end
    elseif prefix == "BMC" then
        local slot, icon, spell, name = strsplit("^", value)
        slot = tonumber(slot)
        local set = bars[guid] or {}
        bars[guid] = set
        if slot then
            local entry = set[slot] or { type = 64, id = 0 }
            entry.macroIcon = icon
            entry.macroSpell = tonumber(spell) or 0
            entry.macroName = name
            set[slot] = entry
        end
    elseif prefix == "BCD" then
        local kind, id, ms = strsplit(":", value)
        kind, id, ms = tonumber(kind), tonumber(id), tonumber(ms)
        if id then
            local list = cooldowns[guid] or {}
            cooldowns[guid] = list
            local key = (kind == 1 and "i" or "s") .. id
            if ms and ms > 0 then
                list[key] = { start = GetTime(), duration = ms / 1000 }
            else
                list[key] = nil
            end
        end
        return   -- RefreshCooldowns picks it up; nothing to lay out
    elseif prefix == "FRM" then
        forms[guid] = tonumber(value) or 0
    elseif prefix == "SPE" then
        local spell, castTime = strsplit(",", value)
        spell, castTime = tonumber(spell), tonumber(castTime) or 0
        if spell and spell ~= 6603 and castTime > 0 and castTime < 99998 then
            casts[guid] = { spell = spell, start = GetTime(), duration = castTime / 1000 }
        end
        if active and guid == followed and spell then
            FlashSpell(spell)
        end
        return
    else
        return
    end

    if guid == followed then
        dirty = true
    end
end

------------------------------------------------------------------------------
-- ticking
------------------------------------------------------------------------------

local since = 0
panel:SetScript("OnUpdate", function(self, elapsed)
    if not active then
        return
    end

    HideArenaFrames()

    if dirty then
        dirty = false
        LayoutBars()
    end

    local now = GetTime()
    EachButton(function(b)
        if b.flashUntil > 0 then
            local left = b.flashUntil - now
            if left <= 0 then
                b.flashUntil = 0
                b.flash:SetAlpha(0)
            else
                b.flash:SetAlpha(left / 0.35)
            end
        end
    end)

    since = since + elapsed
    if since >= 0.1 then
        since = 0
        RefreshCooldowns()
        RefreshHeader()
        LayoutPicker()

        -- The follow ends by itself on a world reload: start it again.
        if CenturionSpectate and followed and not CenturionSpectate("?") then
            CenturionSpectate(followed)
        end
    end
end)

panel:SetScript("OnHide", function()
    if active and not suppressHide then
        CenturionReplaySpectate_Stop()
    end
end)

stopButton:SetScript("OnClick", function()
    CenturionReplaySpectate_Stop()
end)

-- A restarted replay: the actors come back under the same guids, so
-- spectating picks up the same player once their frame exists again.
local events = CreateFrame("Frame")
events:RegisterEvent("PLAYER_ENTERING_WORLD")
events:SetScript("OnEvent", function()
    resumeGuid = nil
end)

local resumeTick = 0
events:SetScript("OnUpdate", function(self, elapsed)
    if not resumeGuid or active then
        return
    end
    resumeTick = resumeTick + elapsed
    if resumeTick < 0.5 then
        return
    end
    resumeTick = 0
    if specingEnabled and ATPlayers and ATPlayers[resumeGuid] then
        CenturionReplaySpectate_Start(resumeGuid)
    end
end)
