-- CENTURION_GurubashiTimer: the hourly Gurubashi chest's clock on the Battlegrounds tab,
-- beside the "Gurubashi chest" checkbox (PVPBattlegroundFrame.xml/.lua in patch-Y).
--
-- Asks the server with the addon message CCGAMEREQ GURUTIMER and is answered
-- CCGAME GURUTIMER:<out>:<seconds>. out = 1: the chest is up and disappears in
-- <seconds>. out = 0: the next hourly check is in <seconds>, when a chest appears
-- if anybody with the checkbox ticked is in Stranglethorn Vale. A server that does
-- not answer shows no timer, and neither does a client without the checkbox.

local REQUEST_PREFIX = "CCGAMEREQ";
local ANSWER_PREFIX = "CCGAME";
local REQUERY_SECONDS = 30;
local RETRY_SECONDS = 3;

local checkbox = PVPBattlegroundFrameGurubashiChest;
local label = PVPBattlegroundFrameGurubashiChestText;
if ( not checkbox or not label or not PVPBattlegroundFrame ) then
	return;
end

if ( CENTURION_GURUBASHI_CHEST_TOOLTIP ) then
	CENTURION_GURUBASHI_CHEST_TOOLTIP = CENTURION_GURUBASHI_CHEST_TOOLTIP.."\n\nThe timer counts down to the next chest, checked every hour on the hour, or while one is up, to when it disappears.";
end

local chestUp, endsAt, lastAsked = false, nil, nil;

-- Parented to the checkbox, so it hides with it until the server has answered for the toggle.
local timer = checkbox:CreateFontString("CenturionGurubashiTimerText", "ARTWORK", "GameFontHighlightSmall");
timer:SetPoint("RIGHT", label, "LEFT", -8, 0);
timer:Hide();

local function Ask()
	local name = UnitName("player");
	if ( SendAddonMessage and name ) then
		SendAddonMessage(REQUEST_PREFIX, "GURUTIMER", "WHISPER", name);
		lastAsked = GetTime();
	end
end

local function Clock(seconds)
	seconds = math.floor(seconds + 0.5);
	if ( seconds < 0 ) then
		seconds = 0;
	end
	return string.format("%d:%02d", math.floor(seconds / 60), seconds % 60);
end

local function Refresh()
	if ( not endsAt ) then
		timer:Hide();
		return;
	end

	local now = GetTime();
	local left = endsAt - now;
	if ( chestUp ) then
		timer:SetText("Chest up "..Clock(left));
		timer:SetTextColor(0.1, 1.0, 0.1);
	else
		timer:SetText("Next chest "..Clock(left));
		timer:SetTextColor(1.0, 1.0, 1.0);
	end
	timer:Show();

	-- Looted, despawned or just spawned: the clock ran out or went stale, so ask
	-- again - but not every frame while the server catches up.
	if ( not lastAsked or (left <= 0 and now - lastAsked >= RETRY_SECONDS) or now - lastAsked >= REQUERY_SECONDS ) then
		Ask();
	end
end

-- A child of the Battlegrounds tab: its OnUpdate only runs while the tab is open.
local driver = CreateFrame("Frame", nil, PVPBattlegroundFrame);
local sinceRefresh = 0;
driver:SetScript("OnUpdate", function(self, elapsed)
	sinceRefresh = sinceRefresh + elapsed;
	if ( sinceRefresh >= 0.25 ) then
		sinceRefresh = 0;
		Refresh();
	end
end);
driver:RegisterEvent("CHAT_MSG_ADDON");
driver:SetScript("OnEvent", function(self, event, prefix, message)
	if ( prefix ~= ANSWER_PREFIX or type(message) ~= "string" ) then
		return;
	end
	local up, seconds = string.match(message, "^GURUTIMER:(%d):(%d+)$");
	if ( up ) then
		chestUp = (up == "1");
		endsAt = GetTime() + tonumber(seconds);
		Refresh();
	end
end);
PVPBattlegroundFrame:HookScript("OnShow", Ask);
