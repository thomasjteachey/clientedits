NUM_DISPLAYED_BATTLEGROUNDS = 5;

local PVPBATTLEGROUND_TEXTURELIST = {};
PVPBATTLEGROUND_TEXTURELIST[1] = "Interface\\PVPFrame\\PvpBg-AlteracValley";
PVPBATTLEGROUND_TEXTURELIST[2] = "Interface\\PVPFrame\\PvpBg-WarsongGulch";
PVPBATTLEGROUND_TEXTURELIST[3] = "Interface\\PVPFrame\\PvpBg-ArathiBasin";
PVPBATTLEGROUND_TEXTURELIST[7] = "Interface\\PVPFrame\\PvpBg-EyeOfTheStorm";
PVPBATTLEGROUND_TEXTURELIST[9] = "Interface\\PVPFrame\\PvpBg-StrandOfTheAncients";
PVPBATTLEGROUND_TEXTURELIST[30] = "Interface\\PVPFrame\\PvpBg-IsleOfConquest";
PVPBATTLEGROUND_TEXTURELIST[32] = "Interface\\PVPFrame\\PvpRandomBg";
PVPBATTLEGROUND_TEXTURELIST[31] = "Interface\\PVPFrame\\PvpBg-Battlefield";

-- Legionnaire+ custom battleground frame art
-- Twin Peaks should use the same PvP frame art/behavior as Warsong Gulch.
PVPBATTLEGROUND_TEXTURELIST[108] = PVPBATTLEGROUND_TEXTURELIST[2];

-- Battle for Gilneas.
PVPBATTLEGROUND_TEXTURELIST[120] = "Interface\\PVPFrame\\PvpBg-Gilneas";






function PVPBattleground_UpdateBattlegrounds()
	local frame;
	local localizedName, canEnter, isHoliday;
	local tempString, BGindex, isBig;
	
	local offset = FauxScrollFrame_GetOffset(PVPBattlegroundFrameTypeScrollFrame);
	local currentFrameNum = -offset + 1;
	local numBGs = 0;
	
	for i=1,GetNumBattlegroundTypes() do
		frame = _G["BattlegroundType"..currentFrameNum];
		
		localizedName, canEnter, isHoliday = GetBattlegroundInfo(i);
		tempString = localizedName;
		if ( localizedName and canEnter ) then
			if ( frame ) then
				frame.BGindex = i;
				frame.localizedName = localizedName;
				if ( not PVPBattlegroundFrame.selectedBG ) then
					PVPBattlegroundFrame.selectedBG = i;
				end
				frame:Enable();
				if ( isHoliday ) then
					tempString = tempString.." ("..BATTLEGROUND_HOLIDAY..")";
				end
			
				frame.title:SetText(tempString);
				frame:Show();
				if ( i == PVPBattlegroundFrame.selectedBG ) then
					frame:LockHighlight();
				else
					frame:UnlockHighlight();
				end
			end
			currentFrameNum = currentFrameNum + 1;
			numBGs = numBGs + 1;
		end
	end
	
	if ( currentFrameNum <= NUM_DISPLAYED_BATTLEGROUNDS ) then
		isBig = true;	--Espand the highlight to cover where the scroll bar usually is.
	end
	
	for i=1,NUM_DISPLAYED_BATTLEGROUNDS do
		frame = _G["BattlegroundType"..i];
		if ( isBig ) then
			frame:SetWidth(315);
		else
			frame:SetWidth(295);
		end
	end
	
	for i=currentFrameNum,NUM_DISPLAYED_BATTLEGROUNDS do
		frame = _G["BattlegroundType"..i];
		frame:Hide();
	end
	
	PVPBattleground_UpdateQueueStatus();
	
	PVPBattlegroundFrame_UpdateGroupAvailable();
	FauxScrollFrame_Update(PVPBattlegroundFrameTypeScrollFrame, numBGs, NUM_DISPLAYED_BATTLEGROUNDS, 16);
end

function PVPBattleground_UpdateInfo(BGindex)
	if ( type(BGindex) ~= "number" ) then
		BGindex = PVPBattlegroundFrame.selectedBG;
	end
	
	local BGname, canEnter, isHoliday, isRandom, BattleGroundID = GetBattlegroundInfo(BGindex);

	
	if(PVPBATTLEGROUND_TEXTURELIST[BattleGroundID]) then
		PVPBattlegroundFrameBGTex:SetTexture(PVPBATTLEGROUND_TEXTURELIST[BattleGroundID]);
	end
	
	if ( isRandom or isHoliday ) then
		PVPBattleground_UpdateRandomInfo();
		PVPBattlegroundFrameInfoScrollFrameChildFrameRewardsInfo:Show();
		PVPBattlegroundFrameInfoScrollFrameChildFrameDescription:Hide();
	else
		local mapName, mapDescription, maxGroup = GetBattlefieldInfo();
		if ( mapDescription ~= PVPBattlegroundFrameInfoScrollFrameChildFrameDescription:GetText() ) then
			PVPBattlegroundFrameInfoScrollFrameChildFrameDescription:SetText(mapDescription);
			PVPBattlegroundFrameInfoScrollFrame:SetVerticalScroll(0);
		end
		
		PVPBattlegroundFrameInfoScrollFrameChildFrameRewardsInfo:Hide();
		PVPBattlegroundFrameInfoScrollFrameChildFrameDescription:Show();
	end

end

function PVPBattleground_GetSelectedBattlegroundInfo()
	return GetBattlegroundInfo(PVPBattlegroundFrame.selectedBG);
end

function PVPBattleground_UpdateRandomInfo()
	PVPQueue_UpdateRandomInfo(PVPBattlegroundFrameInfoScrollFrameChildFrameRewardsInfo, PVPBattleground_GetSelectedBattlegroundInfo);
end

function PVPBattleground_UpdateQueueStatus()
	local queueStatus, queueMapName, queueInstanceID, frame;
	for i=1, NUM_DISPLAYED_BATTLEGROUNDS do
		frame = _G["BattlegroundType"..i];
		frame.status:Hide();
	end
	local factionTexture = "Interface\\PVPFrame\\PVP-Currency-"..UnitFactionGroup("player");
	for i=1, MAX_BATTLEFIELD_QUEUES do
		queueStatus, queueMapName, queueInstanceID = GetBattlefieldStatus(i);
		if ( queueStatus ~= "none" ) then
			for j=1, NUM_DISPLAYED_BATTLEGROUNDS do
				local frame = _G["BattlegroundType"..j];
				if ( frame.localizedName == queueMapName ) then
					if ( queueStatus == "queued" ) then
						frame.status.texture:SetTexture(factionTexture);
						frame.status.texture:SetTexCoord(0.0, 1.0, 0.0, 1.0);
						frame.status.tooltip = BATTLEFIELD_QUEUE_STATUS;
						frame.status:Show();
					elseif ( queueStatus == "confirm" ) then
						frame.status.texture:SetTexture("Interface\\CharacterFrame\\UI-StateIcon");
						frame.status.texture:SetTexCoord(0.45, 0.95, 0.0, 0.5);
						frame.status.tooltip = BATTLEFIELD_CONFIRM_STATUS;
						frame.status:Show();
					end
				end
			end
		end
	end
end

function PVPBattleground_ResetInfo()	
	RequestBattlegroundInstanceInfo(PVPBattlegroundFrame.selectedBG);
	
	PVPBattleground_UpdateInfo();
end

function PVPBattlegroundButton_OnClick(self)
	local offset = FauxScrollFrame_GetOffset(PVPBattlegroundFrameTypeScrollFrame);
	local id = self:GetID() + offset;

	for i=1,NUM_DISPLAYED_BATTLEGROUNDS do
		if ( id == i + offset ) then
			_G["BattlegroundType"..i]:LockHighlight();
		else
			_G["BattlegroundType"..i]:UnlockHighlight();
		end
	end
	
	if ( self.BGindex == PVPBattlegroundFrame.selectedBG ) then
		return;
	end
	
	PVPBattlegroundFrame.selectedBG = self.BGindex;
	
	PVPBattleground_ResetInfo();
	
	PVPBattleground_UpdateJoinButton();
end

function PVPBattleground_UpdateJoinButton()
	local mapName, mapDescription, maxGroup = GetBattlefieldInfo();
	
	if ( maxGroup and maxGroup == 5 ) then
		PVPBattlegroundFrameGroupJoinButton:SetText(JOIN_AS_PARTY);
	else
		PVPBattlegroundFrameGroupJoinButton:SetText(JOIN_AS_GROUP);		
	end
end

function PVPBattlegroundFrameJoinButton_OnClick(self)
	local joinAsGroup;
	if ( self == PVPBattlegroundFrameGroupJoinButton ) then
		joinAsGroup = true;
	end
	
	JoinBattlefield(0, joinAsGroup);
end

function PVPBattlegroundFrame_OnLoad(self)
	self:RegisterEvent("PVPQUEUE_ANYWHERE_SHOW");
	self:RegisterEvent("NPC_PVPQUEUE_ANYWHERE");
	self:RegisterEvent("UPDATE_BATTLEFIELD_STATUS");
	self:RegisterEvent("PVPQUEUE_ANYWHERE_UPDATE_AVAILABLE");
	self:RegisterEvent("PLAYER_ENTERING_WORLD");
	self:RegisterEvent("PARTY_MEMBERS_CHANGED");
	
	PanelTemplates_SetTab(PVPParentFrame, 1);
	PVPBattlegroundFrame_UpdateVisible();
	
	BattlegroundType1:Click();
end

function PVPBattlegroundFrame_OnEvent(self, event, ...)
	if ( event == "PVPQUEUE_ANYWHERE_SHOW" or event == "NPC_PVPQUEUE_ANYWHERE") then
		self.currentData = true;
		PVPBattleground_UpdateBattlegrounds();
		if ( self.selectedBG ) then
			PVPBattleground_UpdateInfo();
		end
		if ( event == "NPC_PVPQUEUE_ANYWHERE" ) then
			ShowUIPanel(PVPParentFrame);
			PVPFrame_SetJustBG(true);
		end
	elseif ( event == "UPDATE_BATTLEFIELD_STATUS" ) then
		PVPBattleground_UpdateQueueStatus();
	elseif ( event == "PVPQUEUE_ANYWHERE_UPDATE_AVAILABLE" or event == "PLAYER_ENTERING_WORLD" ) then
		self:UnregisterEvent("PLAYER_ENTERING_WORLD");
		
		FauxScrollFrame_SetOffset(PVPBattlegroundFrameTypeScrollFrame, 0);
		FauxScrollFrame_OnVerticalScroll(PVPBattlegroundFrameTypeScrollFrame, 0, 16, PVPBattleground_UpdateBattlegrounds); --We may be changing brackets, so we don't want someone to see an outdated version of the data.
		if ( self.selectedBG ) then
			PVPBattleground_ResetInfo();
			PVPBattleground_UpdateJoinButton();
		end
		PVPBattlegroundFrame_UpdateVisible();
	elseif ( event == "PARTY_MEMBERS_CHANGED" ) then
		PVPBattlegroundFrame_UpdateGroupAvailable();
	end
end

function PVPBattlegroundFrame_OnShow(self)
	-- CENTURION: the Gurubashi chest toggle takes Wintergrasp's place once the
	-- server has answered for it.
	if ( IsInInstance() or CenturionPVPToggles_GurubashiChestAnswered() ) then
		WintergraspTimer:Hide();
	else
		WintergraspTimer:Show();
	end
	CenturionPVPToggles_Query();
	
	SortBGList();
	
	PVPBattleground_UpdateBattlegrounds();
	RequestBattlegroundInstanceInfo(self.selectedBG or 1);
end

function PVPBattlegroundFrame_OnHide(self)
	CloseBattlefield();
end

function PVPBattlegroundFrame_UpdateVisible()
	for i=1, GetNumBattlegroundTypes() do
		local _, canEnter = GetBattlegroundInfo(i);
		if ( canEnter ) then
			if ( not PVPFrame_IsJustBG() ) then
				PVPParentFrameTab1:Show();
				PVPParentFrameTab2:Show();
			end
			return;
		end
	end
	PVPParentFrameTab1:Click();
	PVPParentFrameTab1:Hide();
	PVPParentFrameTab2:Hide();
end

function PVPBattlegroundFrame_UpdateGroupAvailable()
	if ( ((GetNumPartyMembers() > 0) or (GetNumRaidMembers() > 0)) and IsPartyLeader() ) then
		-- If this is true then can join as a group
		PVPBattlegroundFrameGroupJoinButton:Enable();
	else
		PVPBattlegroundFrameGroupJoinButton:Disable();
	end
end

function WintergraspTimer_OnLoad(self)
	self.canQueue = false;
	self.tooltip = PVPBATTLEGROUND_WINTERGRASPTIMER_CANNOT_QUEUE;
	self.texture:SetTexCoord(0.0, 1.0, 0.0, 0.5);
end

function WintergraspTimer_OnUpdate(self, elapsed)
	local nextBattleTime = GetWintergraspWaitTime();
	if ( nextBattleTime and nextBattleTime > 60 ) then
		self.text:SetFormattedText(PVPBATTLEGROUND_WINTERGRASPTIMER, SecondsToTime(nextBattleTime, true));
	elseif ( nextBattleTime and nextBattleTime > 0 ) then
		self.text:SetFormattedText(PVPBATTLEGROUND_WINTERGRASPTIMER, SecondsToTime(nextBattleTime, false));
	else
		self.text:SetFormattedText(PVPBATTLEGROUND_WINTERGRASPTIMER, WINTERGRASP_IN_PROGRESS);
	end

	local canQueue = CanQueueForWintergrasp();
	if ( self.canQueue ~= canQueue ) then
		-- simple safeguard so we're not doing a bunch of unnecessary work for each OnUpdate
		if ( canQueue ) then
			self.tooltip = PVPBATTLEGROUND_WINTERGRASPTIMER_CAN_QUEUE;
			self.texture:SetTexCoord(0.0, 1.0, 0.5, 1.0);
		else
			self.tooltip = PVPBATTLEGROUND_WINTERGRASPTIMER_CANNOT_QUEUE;
			self.texture:SetTexCoord(0.0, 1.0, 0.0, 0.5);
		end
		self.canQueue = canQueue;
	end
end

-- CENTURION: Battlegrounds-tab toggles. Both stay hidden until the server
-- answers, so a realm (or a server build) without them keeps the stock tab.
--   CCGAMEREQ TQUEUE[:0|1]    -> CCGAME TQUEUE:<on>:<locked>            tournament queue
--                             -> CCGAME TQUEUEWHY:<why>:<level>:<rules> why it reads so,
--                                                                       and what a match does
--   CCGAMEREQ GURUCHEST[:0|1] -> CCGAME GURUCHEST:<on>                  Gurubashi hourly chest
--
-- TQUEUEWHY arrives on a line of its own so that a client built before it
-- existed still matches the TQUEUE line whole; one that never hears it shows
-- the tournament queue without the explanation rather than not at all.

-- Tournament::QueueLockReason
local TQUEUE_WHY_FREE = 0;
local TQUEUE_WHY_TOURNAMENT_CHARACTER = 1;
local TQUEUE_WHY_BELOW_LEVEL = 2;
local TQUEUE_WHY_FORCED = 3;
local TQUEUE_WHY_BUSY = 4;
-- Tournament::QueueRuleFlags
local TQUEUE_RULE_LOADOUT = 1;
local TQUEUE_RULE_BAN_CONSUMABLES = 2;

CENTURION_TOURNAMENT_QUEUE_TITLE = "Tournament queue";
-- No line for a tournament character and none for a sixty: the header below
-- already says every match is a tournament match, and saying it twice was the
-- verbose half of this tooltip. What is left only ever shows with the forcing
-- rule off (FREE, BUSY) or to somebody not there yet (BELOW_LEVEL).
CENTURION_TOURNAMENT_QUEUE_WHY = {
	[TQUEUE_WHY_FREE] = "Yours to set, while you are out of every queue and match.",
	[TQUEUE_WHY_BELOW_LEVEL] = "World characters join the tournament at level %d. Until then your battlegrounds are ordinary ones.",
	[TQUEUE_WHY_BUSY] = "Leave every queue and match to change this.",
};
CENTURION_TOURNAMENT_QUEUE_RULES_HEADER = "At level %d, all matches are considered tournament matches, and the following apply:";
-- The bullet is U+2022: FRIZQT__.TTF, which is what a tooltip is drawn in, has
-- the glyph (checked in the client's own locale-enUS.MPQ copy), so it renders
-- rather than boxing.
CENTURION_TOURNAMENT_QUEUE_RULE_GEAR = "• Outlawed gear, underleveled gear, and empty slots are replaced with tournament starter gear.";
CENTURION_TOURNAMENT_QUEUE_RULE_CRUNCH = "• Epic equipment above item level 72 and epic weapons above 67 are crunched down.";
CENTURION_TOURNAMENT_QUEUE_RULE_BACK = "• Your own gear is returned when the battleground or arena ends.";
-- Not gated on the loadout: the armour lock and the talent rule hold in a
-- tournament match whether or not the gear swap is switched on.
CENTURION_TOURNAMENT_QUEUE_RULE_EQUIP = "• You cannot change out or unequip any equipment besides weapons, trinkets, and shields.";
CENTURION_TOURNAMENT_QUEUE_RULE_FOOD = "• Your food, water, bandages, ammunition, and spell reagents aren't consumed on use.";
CENTURION_TOURNAMENT_QUEUE_RULE_BAGS = "• Nothing else from your bags works - only the special PvP consumables.";
CENTURION_TOURNAMENT_QUEUE_RULE_TALENTS = "• Changing talents is strictly forbidden.";
CENTURION_GURUBASHI_CHEST_TOOLTIP = "Gurubashi chest\n\nWhen the hourly Gurubashi Arena chest appears, everyone in Stranglethorn Vale is pulled into the Battle Ring to fight for it.\n\nUncheck to stay where you are and not count towards the chest.";

local CenturionPVPToggles = { gurubashiChestAnswered = false, queueMinLevel = 60 };

local function CenturionPVPToggles_Send(message)
	local playerName = UnitName("player");
	if ( SendAddonMessage and playerName ) then
		SendAddonMessage("CCGAMEREQ", message, "WHISPER", playerName);
	end
end

function CenturionPVPToggles_Query()
	CenturionPVPToggles_Send("TQUEUE");
	CenturionPVPToggles_Send("GURUCHEST");
end

function CenturionPVPToggles_GurubashiChestAnswered()
	return CenturionPVPToggles.gurubashiChestAnswered;
end

local function CenturionPVPToggles_SetLabelEnabled(button, enabled)
	local text = _G[button:GetName().."Text"];
	if ( not text ) then
		return;
	end
	if ( enabled ) then
		text:SetTextColor(NORMAL_FONT_COLOR.r, NORMAL_FONT_COLOR.g, NORMAL_FONT_COLOR.b);
	else
		text:SetTextColor(GRAY_FONT_COLOR.r, GRAY_FONT_COLOR.g, GRAY_FONT_COLOR.b);
	end
end

function CenturionPVPToggles_SetChecked(button, checked)
	if ( checked ) then
		button:SetChecked(1);
	else
		button:SetChecked(nil);
	end
end

-- Locked is this box's ordinary state - a tournament character never leaves the
-- tournament queue and neither does anybody at the tournament level - so being
-- read is most of what it is for. A disabled button hears no mouse unless it is
-- told to, hence the flag: the box greys out, stops taking clicks, and still
-- answers the cursor with the whole deal.
function CenturionTournamentQueue_OnLoad(self)
	if ( self.SetMotionScriptsWhileDisabled ) then
		self:SetMotionScriptsWhileDisabled(true);
		self.centurionCanDisable = true;
	end
	-- Without that call the box is left live and merely dressed as locked (grey
	-- label, clicks put back in OnClick), because a box that cannot be read is
	-- worse than one that looks clickable.
end

local function CenturionPVPToggles_HasRule(rules, rule)
	return rules and math.floor(rules / rule) % 2 == 1;
end

function CenturionTournamentQueue_OnEnter(self)
	GameTooltip:SetOwner(self, "ANCHOR_BOTTOMRIGHT", 5, 40);
	GameTooltip:SetText(CENTURION_TOURNAMENT_QUEUE_TITLE, 1, 1, 1);

	local why = CenturionPVPToggles.queueWhy;
	local whyText = why and CENTURION_TOURNAMENT_QUEUE_WHY[why];
	if ( whyText ) then
		if ( why == TQUEUE_WHY_BELOW_LEVEL ) then
			whyText = format(whyText, CenturionPVPToggles.queueMinLevel);
		end
		GameTooltip:AddLine(" ");
		GameTooltip:AddLine(whyText, HIGHLIGHT_FONT_COLOR.r, HIGHLIGHT_FONT_COLOR.g, HIGHLIGHT_FONT_COLOR.b, 1);
	end

	-- Only the rules this realm is actually running: the server says which.
	local rules = CenturionPVPToggles.queueRules;
	if ( rules ) then
		GameTooltip:AddLine(" ");
		GameTooltip:AddLine(format(CENTURION_TOURNAMENT_QUEUE_RULES_HEADER, CenturionPVPToggles.queueMinLevel),
			HIGHLIGHT_FONT_COLOR.r, HIGHLIGHT_FONT_COLOR.g, HIGHLIGHT_FONT_COLOR.b, 1);
		if ( CenturionPVPToggles_HasRule(rules, TQUEUE_RULE_LOADOUT) ) then
			GameTooltip:AddLine(CENTURION_TOURNAMENT_QUEUE_RULE_GEAR, NORMAL_FONT_COLOR.r, NORMAL_FONT_COLOR.g, NORMAL_FONT_COLOR.b, 1);
			GameTooltip:AddLine(CENTURION_TOURNAMENT_QUEUE_RULE_CRUNCH, NORMAL_FONT_COLOR.r, NORMAL_FONT_COLOR.g, NORMAL_FONT_COLOR.b, 1);
			GameTooltip:AddLine(CENTURION_TOURNAMENT_QUEUE_RULE_BACK, NORMAL_FONT_COLOR.r, NORMAL_FONT_COLOR.g, NORMAL_FONT_COLOR.b, 1);
		end
		GameTooltip:AddLine(CENTURION_TOURNAMENT_QUEUE_RULE_EQUIP, NORMAL_FONT_COLOR.r, NORMAL_FONT_COLOR.g, NORMAL_FONT_COLOR.b, 1);
		GameTooltip:AddLine(CENTURION_TOURNAMENT_QUEUE_RULE_FOOD, NORMAL_FONT_COLOR.r, NORMAL_FONT_COLOR.g, NORMAL_FONT_COLOR.b, 1);
		if ( CenturionPVPToggles_HasRule(rules, TQUEUE_RULE_BAN_CONSUMABLES) ) then
			GameTooltip:AddLine(CENTURION_TOURNAMENT_QUEUE_RULE_BAGS, NORMAL_FONT_COLOR.r, NORMAL_FONT_COLOR.g, NORMAL_FONT_COLOR.b, 1);
		end
		GameTooltip:AddLine(CENTURION_TOURNAMENT_QUEUE_RULE_TALENTS, NORMAL_FONT_COLOR.r, NORMAL_FONT_COLOR.g, NORMAL_FONT_COLOR.b, 1);
	end

	GameTooltip:Show();
end

function CenturionTournamentQueue_OnClick(self)
	-- Belt and braces for a client that lets a disabled box be clicked anyway:
	-- put it back where the server left it and say nothing.
	if ( CenturionPVPToggles.queueLocked ) then
		CenturionPVPToggles_SetChecked(self, CenturionPVPToggles.queueOn);
		return;
	end

	if ( self:GetChecked() ) then
		CenturionPVPToggles_Send("TQUEUE:1");
	else
		CenturionPVPToggles_Send("TQUEUE:0");
	end
end

function CenturionGurubashiChest_OnClick(self)
	if ( self:GetChecked() ) then
		CenturionPVPToggles_Send("GURUCHEST:1");
	else
		CenturionPVPToggles_Send("GURUCHEST:0");
	end
end

local function CenturionPVPToggles_OnEvent(self, event, prefix, message)
	if ( event == "PLAYER_ENTERING_WORLD" ) then
		CenturionPVPToggles_Query();
		return;
	end
	if ( prefix ~= "CCGAME" or type(message) ~= "string" ) then
		return;
	end

	local on, locked = string.match(message, "^TQUEUE:(%d):(%d)$");
	if ( on ) then
		local button = PVPBattlegroundFrameTournamentQueue;
		CenturionPVPToggles.queueOn = (on == "1");
		CenturionPVPToggles.queueLocked = (locked == "1");
		CenturionPVPToggles_SetChecked(button, CenturionPVPToggles.queueOn);
		if ( CenturionPVPToggles.queueLocked and button.centurionCanDisable ) then
			button:Disable();
		else
			button:Enable();
		end
		CenturionPVPToggles_SetLabelEnabled(button, not CenturionPVPToggles.queueLocked);
		button:Show();
		return;
	end

	local why, minLevel, rules = string.match(message, "^TQUEUEWHY:(%d+):(%d+):(%d+)$");
	if ( why ) then
		CenturionPVPToggles.queueWhy = tonumber(why);
		CenturionPVPToggles.queueMinLevel = tonumber(minLevel);
		CenturionPVPToggles.queueRules = tonumber(rules);
		-- Redraw whatever the cursor is already sitting on.
		if ( GameTooltip:IsOwned(PVPBattlegroundFrameTournamentQueue) ) then
			CenturionTournamentQueue_OnEnter(PVPBattlegroundFrameTournamentQueue);
		end
		return;
	end

	on = string.match(message, "^GURUCHEST:(%d)$");
	if ( on ) then
		CenturionPVPToggles.gurubashiChestAnswered = true;
		CenturionPVPToggles_SetChecked(PVPBattlegroundFrameGurubashiChest, on == "1");
		PVPBattlegroundFrameGurubashiChest:Show();
		WintergraspTimer:Hide();
	end
end

local CenturionPVPTogglesFrame = CreateFrame("Frame");
CenturionPVPTogglesFrame:RegisterEvent("PLAYER_ENTERING_WORLD");
CenturionPVPTogglesFrame:RegisterEvent("CHAT_MSG_ADDON");
CenturionPVPTogglesFrame:SetScript("OnEvent", CenturionPVPToggles_OnEvent);
-- END CENTURION toggles
