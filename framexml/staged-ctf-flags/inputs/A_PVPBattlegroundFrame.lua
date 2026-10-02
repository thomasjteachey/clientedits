NUM_DISPLAYED_BATTLEGROUNDS = 5;

-- BarracksPlus: keep the main PvP window portrait from showing as a hollow frame.
-- PVPFrame.xml is loaded immediately before this file, but the custom FrameXML
-- pack only overrides the battleground/arena/battlefield frames.  If the main
-- PvP parent portrait is missing or blank, create/force it here using artwork
-- that is known to exist in the 3.3.5 client.
local BPLUS_PVP_FRAME_PORTRAIT_TEXTURE = "Interface\\BattlefieldFrame\\UI-Battlefield-Icon";

local function BPlus_SetPVPFramePortraitTexture(portrait)
	if ( not portrait ) then
		return;
	end

	portrait:SetTexture(BPLUS_PVP_FRAME_PORTRAIT_TEXTURE);
	portrait:SetTexCoord(0, 1, 0, 1);
	portrait:SetAlpha(1);
	portrait:Show();
end

local function BPlus_FixPVPFramePortrait()
	local portrait = _G["PVPParentFramePortrait"] or _G["PVPFramePortrait"];

	if ( not portrait and PVPParentFrame ) then
		portrait = PVPParentFrame:CreateTexture("PVPParentFramePortrait", "BACKGROUND");
		portrait:SetWidth(60);
		portrait:SetHeight(60);
		portrait:SetPoint("TOPLEFT", PVPParentFrame, "TOPLEFT", 7, -6);
	end

	BPlus_SetPVPFramePortraitTexture(portrait);
	BPlus_SetPVPFramePortraitTexture(_G["PVPParentFramePortrait"]);
	BPlus_SetPVPFramePortraitTexture(_G["PVPFramePortrait"]);
end

local BPlusPVPFramePortraitFixHooked;
local function BPlus_HookPVPFramePortraitFix()
	BPlus_FixPVPFramePortrait();

	if ( BPlusPVPFramePortraitFixHooked ) then
		return;
	end
	BPlusPVPFramePortraitFixHooked = true;

	if ( PVPParentFrame and PVPParentFrame.HookScript ) then
		PVPParentFrame:HookScript("OnShow", BPlus_FixPVPFramePortrait);
	end
	if ( PVPFrame and PVPFrame.HookScript ) then
		PVPFrame:HookScript("OnShow", BPlus_FixPVPFramePortrait);
	end
	if ( PVPParentFrameTab1 and PVPParentFrameTab1.HookScript ) then
		PVPParentFrameTab1:HookScript("OnClick", BPlus_FixPVPFramePortrait);
	end
	if ( PVPParentFrameTab2 and PVPParentFrameTab2.HookScript ) then
		PVPParentFrameTab2:HookScript("OnClick", BPlus_FixPVPFramePortrait);
	end
end

local BPlusPVPFramePortraitFixFrame = CreateFrame("Frame");
BPlusPVPFramePortraitFixFrame:RegisterEvent("PLAYER_LOGIN");
BPlusPVPFramePortraitFixFrame:RegisterEvent("PLAYER_ENTERING_WORLD");
BPlusPVPFramePortraitFixFrame:SetScript("OnEvent", BPlus_HookPVPFramePortraitFix);


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
	if ( IsInInstance() ) then
		WintergraspTimer:Hide();
	else
		WintergraspTimer:Show();
	end
	
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
