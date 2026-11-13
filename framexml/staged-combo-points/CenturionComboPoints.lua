-- CenturionComboPoints.lua - Unfinished Business (Assassination talent, spell 81500).
--
-- With the talent a rogue's combo points are its own, not its target's: the
-- server keeps the count through target changes and target deaths. It still
-- tells the client which unit "holds" them - the current hostile selection, or
-- the rogue itself while nothing hostile is selected - because Wow.exe only
-- lets a finisher go at that unit (GetComboPoints 0x611670 -> 0x513920 compares
-- the asked-for GUID with the stored combo target). So the count is read from
-- the target first and from the player second.
--
-- For a rogue with the talent the points are drawn in a row under the player
-- frame, and the stock target-frame pips (ComboFrame) stay hidden. Everyone
-- else keeps the stock ComboFrame untouched.

local UNFINISHED_BUSINESS = 81500
local SCALE = 1.15
local PIP_STEP = 15          -- 12 px pip + 3 px gap, in the row's own units
-- Row centre under the health/mana bars (x 106..225 of PlayerFrame), top just
-- below the frame's lower border. Checked in a render of the client's art.
local ROW_X, ROW_Y = 165.5, -70

local owned = false
local lastPoints = 0

local row = CreateFrame("Frame", "CenturionPlayerComboFrame", PlayerFrame)
row:SetScale(SCALE)
row:SetWidth(PIP_STEP * MAX_COMBO_POINTS - 3)
row:SetHeight(16)
row:SetPoint("TOP", PlayerFrame, "TOPLEFT", ROW_X / SCALE, ROW_Y / SCALE)
row:Hide()

for i = 1, MAX_COMBO_POINTS do
	local pip = CreateFrame("Frame", "CenturionPlayerComboPoint" .. i, row, "ComboPointTemplate")
	pip:SetPoint("TOPLEFT", row, "TOPLEFT", (i - 1) * PIP_STEP, 0)
	_G["CenturionPlayerComboPoint" .. i .. "Highlight"]:SetAlpha(0)
	_G["CenturionPlayerComboPoint" .. i .. "Shine"]:SetAlpha(0)
end

local function KnowsUnfinishedBusiness()
	local wanted = GetSpellInfo(UNFINISHED_BUSINESS)
	if not wanted then
		return false
	end
	for tab = 1, GetNumTalentTabs() do
		for i = 1, GetNumTalents(tab) do
			local name, _, _, _, rank = GetTalentInfo(tab, i)
			if name == wanted then
				return (rank or 0) > 0
			end
		end
	end
	return false
end

local function Active()
	return owned and PlayerFrame.unit == "player"
end

local function CurrentPoints()
	local points = GetComboPoints("player", "target")
	if points == 0 then
		points = GetComboPoints("player", "player")
	end
	return points
end

local function ClearPips()
	for i = 1, MAX_COMBO_POINTS do
		_G["CenturionPlayerComboPoint" .. i .. "Highlight"]:SetAlpha(0)
		_G["CenturionPlayerComboPoint" .. i .. "Shine"]:SetAlpha(0)
	end
end

local function UpdateRow()
	local points = Active() and CurrentPoints() or 0
	if points == 0 then
		ClearPips()
		row:Hide()
		lastPoints = 0
		return
	end

	if not row:IsShown() then
		row:Show()
		UIFrameFadeIn(row, COMBOFRAME_FADE_IN)
	end

	for i = 1, MAX_COMBO_POINTS do
		local pip = _G["CenturionPlayerComboPoint" .. i]
		local highlight = _G["CenturionPlayerComboPoint" .. i .. "Highlight"]
		local shine = _G["CenturionPlayerComboPoint" .. i .. "Shine"]
		pip:Show()
		if i <= points then
			if i > lastPoints then
				-- the stock pip animation: highlight fades in, then a shine flashes
				local fadeInfo = {}
				fadeInfo.mode = "IN"
				fadeInfo.timeToFade = COMBOFRAME_HIGHLIGHT_FADE_IN
				fadeInfo.finishedFunc = ComboPointShineFadeIn
				fadeInfo.finishedArg1 = shine
				UIFrameFade(highlight, fadeInfo)
			end
		else
			if ENABLE_COLORBLIND_MODE == "1" then
				pip:Hide()
			end
			highlight:SetAlpha(0)
			shine:SetAlpha(0)
		end
	end
	lastPoints = points
end

-- The stock target-frame pips stand down while the row is in charge.
local StockComboFrameUpdate = ComboFrame_Update
function ComboFrame_Update()
	if Active() then
		ComboPoint1Highlight:SetAlpha(0)
		ComboPoint1Shine:SetAlpha(0)
		ComboFrame:Hide()
		COMBO_FRAME_LAST_NUM_POINTS = 0
	else
		StockComboFrameUpdate()
	end
	UpdateRow()
end

row:RegisterEvent("PLAYER_ENTERING_WORLD")
row:RegisterEvent("PLAYER_TALENT_UPDATE")
row:RegisterEvent("ACTIVE_TALENT_GROUP_CHANGED")
row:RegisterEvent("CHARACTER_POINTS_CHANGED")
row:RegisterEvent("SPELLS_CHANGED")
row:RegisterEvent("UNIT_COMBO_POINTS")
row:RegisterEvent("PLAYER_TARGET_CHANGED")
row:RegisterEvent("UNIT_ENTERED_VEHICLE")
row:RegisterEvent("UNIT_EXITED_VEHICLE")
row:SetScript("OnEvent", function(self, event, unit)
	if (event == "UNIT_COMBO_POINTS" or event == "UNIT_ENTERED_VEHICLE" or event == "UNIT_EXITED_VEHICLE")
		and unit ~= "player" then
		return
	end
	if event ~= "UNIT_COMBO_POINTS" and event ~= "PLAYER_TARGET_CHANGED" then
		owned = KnowsUnfinishedBusiness()
	end
	ComboFrame_Update()
end)
