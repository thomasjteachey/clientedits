NUM_ALWAYS_UP_UI_FRAMES = 0;
NUM_EXTENDED_UI_FRAMES = 0;
MAX_WORLDSTATE_SCORE_BUTTONS = 20;
MAX_NUM_STAT_COLUMNS = 7;
WORLDSTATESCOREFRAME_BASE_COLUMNS = 6;
WORLDSTATESCOREFRAME_PADDING = 35;
WORLDSTATESCOREFRAME_COLUMN_SPACING = 76;
WORLDSTATECOREFRAME_BUTTON_TEXT_OFFSET = -28;

WORLDSTATEALWAYSUPFRAME_TIMESINCELAST = -25;
WORLDSTATEALWAYSUPFRAME_TIMESINCESTART = 0;
WORLDSTATEALWAYSUPFRAME_TIMETORUN = 60;
WORLDSTATEALWAYSUPFRAME_DEFAULTINTERVAL = 5;

WORLDSTATEALWAYSUPFRAME_SUSPENDEDCHATFRAMES = {};

local inBattleground = false;

--
FILTERED_BG_CHAT_ADD_GLOBALS = { "ERR_RAID_MEMBER_ADDED_S", "ERR_BG_PLAYER_JOINED_SS" };
FILTERED_BG_CHAT_SUBTRACT_GLOBALS = { "ERR_RAID_MEMBER_REMOVED_S", "ERR_BG_PLAYER_LEFT_S" };

--Filtered at the end of BGs only
FILTERED_BG_CHAT_END_GLOBALS = { "LOOT_ITEM", "LOOT_ITEM_MULTIPLE", "CREATED_ITEM", "CREATED_ITEM_MULTIPLE", "ERR_RAID_MEMBER_REMOVED_S", "ERR_BG_PLAYER_LEFT_S" };

FILTERED_BG_CHAT_ADD = {};
FILTERED_BG_CHAT_SUBTRACT = {};
FILTERED_BG_CHAT_END = {};

ADDED_PLAYERS = {};
SUBTRACTED_PLAYERS = {};

CLASS_BUTTONS = {
	["WARRIOR"]	= {0, 0.25, 0, 0.25},
	["MAGE"]		= {0.25, 0.49609375, 0, 0.25},
	["ROGUE"]		= {0.49609375, 0.7421875, 0, 0.25},
	["DRUID"]		= {0.7421875, 0.98828125, 0, 0.25},
	["HUNTER"]		= {0, 0.25, 0.25, 0.5},
	["SHAMAN"]	 	= {0.25, 0.49609375, 0.25, 0.5},
	["PRIEST"]		= {0.49609375, 0.7421875, 0.25, 0.5},
	["WARLOCK"]	= {0.7421875, 0.98828125, 0.25, 0.5},
	["PALADIN"]		= {0, 0.25, 0.5, 0.75},
	["DEATHKNIGHT"]	= {0.25, 0.49609375, 0.5, 0.75},
};


ExtendedUI = {};

-- Always up stuff (i.e. capture the flag indicators)
function WorldStateAlwaysUpFrame_OnLoad(self)
	self:RegisterEvent("UPDATE_WORLD_STATES");
	self:RegisterEvent("UPDATE_BATTLEFIELD_SCORE");
	WorldStateAlwaysUpFrame_Update();
	self:RegisterEvent("PLAYER_ENTERING_WORLD");

	self:RegisterEvent("ZONE_CHANGED");
	self:RegisterEvent("ZONE_CHANGED_INDOORS");
	self:RegisterEvent("ZONE_CHANGED_NEW_AREA");
	self:RegisterEvent("PLAYER_ENTERING_BATTLEGROUND");

	self:RegisterEvent("WORLD_STATE_UI_TIMER_UPDATE");
	self:RegisterEvent("CHAT_MSG_ADDON");
	self:RegisterEvent("CHAT_MSG_WHISPER");
		
	FILTERED_BG_CHAT_ADD = {};
	FILTERED_BG_CHAT_SUBTRACT = {};
	FILTERED_BG_CHAT_END = {};
	
	local chatString;
	for _, str in next, FILTERED_BG_CHAT_ADD_GLOBALS do	
		chatString = _G[str];
		if ( chatString ) then
			chatString = string.gsub(chatString, "%[", "%%[");
			chatString = string.gsub(chatString, "%]", "%%]");
			chatString = string.gsub(chatString, "%%s", "(.-)")
			tinsert(FILTERED_BG_CHAT_ADD, chatString);
		end
	end	
	
	local chatString;
	for _, str in next, FILTERED_BG_CHAT_SUBTRACT_GLOBALS do	
		chatString = _G[str];
		if ( chatString ) then
			chatString = string.gsub(chatString, "%[", "%%[");
			chatString = string.gsub(chatString, "%]", "%%]");
			chatString = string.gsub(chatString, "%%s", "(.-)")
			tinsert(FILTERED_BG_CHAT_SUBTRACT, chatString);
		end
	end
	
	for _, str in next, FILTERED_BG_CHAT_END_GLOBALS do
		chatString = _G[str];
		if ( chatString ) then
			chatString = string.gsub(chatString, "%[", "%%[");
			chatString = string.gsub(chatString, "%]", "%%]");
			chatString = string.gsub(chatString, "%%s", "(.-)");
			tinsert(FILTERED_BG_CHAT_END, chatString);
		end
	end

end

-- Centurion CTF flag-color override.
-- Real architecture used here:
--   * MSG_BATTLEGROUND_PLAYER_POSITIONS stays stock and provides live X/Y.
--   * CWSG addon sync provides physical flag identity/state only.
--   * FrameXML draws the stock moving marker position, but replaces only the token.
--
-- Server stock position order is deterministic:
--   marker 1 = physical Alliance flag carrier, if present
--   marker 2 = physical Horde flag carrier, if present
-- When both flags are carried, this lets us know without proximity guessing.
local CENTURION_CTF_BGS = {
	["Warsong Gulch"] = true,
	["Twin Peaks"] = true,
};

CENTURION_WSG_AUTH = CENTURION_WSG_AUTH or {
	Prefix = "CWSG",
	AllianceCarrier = nil,
	HordeCarrier = nil,
	AllianceState = "BASE",
	HordeState = "BASE",
	AllianceX = nil,
	AllianceY = nil,
	HordeX = nil,
	HordeY = nil,
	HasFull = false,
	LastRequest = 0,
};

local function CenturionCTF_NormalizeState(state)
	if ( state == "PLAYER" or state == "GROUND" or state == "WAIT" or state == "BASE" ) then
		return state;
	end
	return "BASE";
end

local function CenturionCTF_ParseFloat(value)
	if ( not value or value == "" ) then
		return nil;
	end
	return tonumber(value);
end

local function CenturionCTF_Split(message, delimiter)
	local result = {};
	local start = 1;
	local delimStart, delimEnd = string.find(message or "", delimiter, start, true);

	while delimStart do
		table.insert(result, string.sub(message, start, delimStart - 1));
		start = delimEnd + 1;
		delimStart, delimEnd = string.find(message, delimiter, start, true);
	end

	table.insert(result, string.sub(message or "", start));
	return result;
end

local function CenturionCTF_IsActive()
	local instanceName, instanceType = GetInstanceInfo();
	if ( instanceType == "pvp" and instanceName and CENTURION_CTF_BGS[instanceName] ) then
		return true;
	end

	local realZone = GetRealZoneText and GetRealZoneText();
	if ( realZone and CENTURION_CTF_BGS[realZone] ) then
		return true;
	end

	local zone = GetZoneText and GetZoneText();
	if ( zone and CENTURION_CTF_BGS[zone] ) then
		return true;
	end

	return false;
end

local function CenturionCTF_IsAuthReady()
	return CENTURION_WSG_AUTH and CENTURION_WSG_AUTH.HasFull and CenturionCTF_IsActive();
end

local function CenturionCTF_NormalizeIconPath(icon)
	icon = string.lower(tostring(icon or ""));
	icon = string.gsub(icon, "\\", "/");
	return icon;
end

local function CenturionCTF_TokenFromPath(value)
	local path = CenturionCTF_NormalizeIconPath(value);
	if ( string.find(path, "allianceflag") ) then
		return "AllianceFlag";
	end
	if ( string.find(path, "hordeflag") ) then
		return "HordeFlag";
	end
	return nil;
end

local function CenturionCTF_IsFlagOut(state)
	return state == "PLAYER" or state == "GROUND";
end

local function CenturionCTF_ResetAuthIfNotCTF()
	if ( CenturionCTF_IsActive() ) then
		return;
	end

	if ( CENTURION_WSG_AUTH ) then
		CENTURION_WSG_AUTH.AllianceCarrier = nil;
		CENTURION_WSG_AUTH.HordeCarrier = nil;
		CENTURION_WSG_AUTH.AllianceState = "BASE";
		CENTURION_WSG_AUTH.HordeState = "BASE";
		CENTURION_WSG_AUTH.AllianceX = nil;
		CENTURION_WSG_AUTH.AllianceY = nil;
		CENTURION_WSG_AUTH.HordeX = nil;
		CENTURION_WSG_AUTH.HordeY = nil;
		CENTURION_WSG_AUTH.HasFull = false;
	end
end

local function CenturionCTF_ShouldClearWorldStateUI()
	if ( CenturionCTF_IsActive() ) then
		return false;
	end

	-- Do not clear generic BG worldstate UI such as AB/AV/EotS.
	local _, instanceType = IsInInstance();
	if ( instanceType == "pvp" ) then
		return false;
	end

	-- Do not clear outdoor/world-PvP objective UI.
	if ( GetNumWorldStateUI and GetNumWorldStateUI() > 0 ) then
		return false;
	end

	return true;
end

local function CenturionCTF_ClearWorldStateUIIfInactive()
	if ( not CenturionCTF_ShouldClearWorldStateUI() ) then
		return;
	end

	-- Do not hide WorldStateAlwaysUpFrame itself here.  The stock update path
	-- does not reliably re-show the parent after it has been force-hidden, which
	-- can suppress AB/AV/WSG/etc worldstate UI until reload.
	for i = 1, NUM_ALWAYS_UP_UI_FRAMES or 0 do
		local frame = _G["AlwaysUpFrame"..i];
		if ( frame ) then
			frame:Hide();
		end

		local dynamicButton = _G["AlwaysUpFrame"..i.."DynamicIconButton"];
		if ( dynamicButton ) then
			dynamicButton:Hide();
		end

		local dynamicIcon = _G["AlwaysUpFrame"..i.."DynamicIconButtonIcon"];
		if ( dynamicIcon ) then
			dynamicIcon:SetTexture(nil);
			dynamicIcon:Hide();
		end

		local flash = _G["AlwaysUpFrame"..i.."Flash"];
		if ( flash ) then
			UIFrameFlashStop(flash);
			flash:Hide();
		end

		local flashTexture = _G["AlwaysUpFrame"..i.."FlashTexture"];
		if ( flashTexture ) then
			flashTexture:SetTexture(nil);
		end
	end

	for i = 1, NUM_EXTENDED_UI_FRAMES or 0 do
		local frame = _G["WorldStateCaptureBar"..i];
		if ( frame ) then
			frame:Hide();
		end
	end
end

local function CenturionCTF_RequestFull()
	if ( not SendAddonMessage or not CenturionCTF_IsActive() ) then
		return;
	end

	local now = GetTime and GetTime() or 0;
	if ( CENTURION_WSG_AUTH.LastRequest and (now - CENTURION_WSG_AUTH.LastRequest) < 2.0 ) then
		return;
	end

	CENTURION_WSG_AUTH.LastRequest = now;
	SendAddonMessage("CWSGREQ", "FULL", "BATTLEGROUND");
end

local function CenturionCTF_RefreshFlagFrames()
	if ( WorldStateAlwaysUpFrame_Update ) then
		WorldStateAlwaysUpFrame_Update();
	end
	if ( WorldMapFrame and WorldMapFrame:IsShown() and WorldMapFrame_UpdateBattlefieldFlags ) then
		WorldMapFrame_UpdateBattlefieldFlags();
	end
	if ( BattlefieldMinimap and BattlefieldMinimap:IsShown() and BattlefieldMinimap_UpdateFlags ) then
		BattlefieldMinimap_UpdateFlags();
	end
end

local function CenturionCTF_ApplyFullPayload(payload)
	local parts = CenturionCTF_Split(payload, ":");
	if ( parts[1] ~= "FULL" ) then
		return false;
	end

	local allianceCarrier = parts[2];
	local hordeCarrier = parts[3];

	if ( allianceCarrier == "" ) then allianceCarrier = nil; end
	if ( hordeCarrier == "" ) then hordeCarrier = nil; end

	CENTURION_WSG_AUTH.AllianceCarrier = allianceCarrier;
	CENTURION_WSG_AUTH.HordeCarrier = hordeCarrier;
	CENTURION_WSG_AUTH.AllianceState = CenturionCTF_NormalizeState(parts[4]);
	CENTURION_WSG_AUTH.HordeState = CenturionCTF_NormalizeState(parts[5]);
	CENTURION_WSG_AUTH.AllianceX = CenturionCTF_ParseFloat(parts[6]);
	CENTURION_WSG_AUTH.AllianceY = CenturionCTF_ParseFloat(parts[7]);
	CENTURION_WSG_AUTH.HordeX = CenturionCTF_ParseFloat(parts[8]);
	CENTURION_WSG_AUTH.HordeY = CenturionCTF_ParseFloat(parts[9]);
	CENTURION_WSG_AUTH.HasFull = true;

	CenturionCTF_RefreshFlagFrames();
	return true;
end

local function CenturionCTF_ApplySidePayload(payload)
	local parts = CenturionCTF_Split(payload, ":");
	local side = parts[1];
	local action = parts[2];

	if ( side ~= "A" and side ~= "H" ) then
		return false;
	end
	if ( not action or action == "" ) then
		return false;
	end

	local state;
	if ( action == "PICKUP" ) then
		state = "PLAYER";
	elseif ( action == "DROP" ) then
		state = "GROUND";
	elseif ( action == "RETURN" ) then
		state = "BASE";
	elseif ( action == "CAPTURE" or action == "WAIT" ) then
		state = "WAIT";
	else
		return false;
	end

	if ( side == "A" ) then
		if ( action == "PICKUP" ) then
			CENTURION_WSG_AUTH.AllianceCarrier = parts[3] ~= "" and parts[3] or nil;
			CENTURION_WSG_AUTH.AllianceX = CenturionCTF_ParseFloat(parts[4]);
			CENTURION_WSG_AUTH.AllianceY = CenturionCTF_ParseFloat(parts[5]);
		elseif ( action == "DROP" ) then
			CENTURION_WSG_AUTH.AllianceCarrier = nil;
			CENTURION_WSG_AUTH.AllianceX = CenturionCTF_ParseFloat(parts[3]);
			CENTURION_WSG_AUTH.AllianceY = CenturionCTF_ParseFloat(parts[4]);
		else
			CENTURION_WSG_AUTH.AllianceCarrier = nil;
			CENTURION_WSG_AUTH.AllianceX = nil;
			CENTURION_WSG_AUTH.AllianceY = nil;
		end
		CENTURION_WSG_AUTH.AllianceState = state;
	else
		if ( action == "PICKUP" ) then
			CENTURION_WSG_AUTH.HordeCarrier = parts[3] ~= "" and parts[3] or nil;
			CENTURION_WSG_AUTH.HordeX = CenturionCTF_ParseFloat(parts[4]);
			CENTURION_WSG_AUTH.HordeY = CenturionCTF_ParseFloat(parts[5]);
		elseif ( action == "DROP" ) then
			CENTURION_WSG_AUTH.HordeCarrier = nil;
			CENTURION_WSG_AUTH.HordeX = CenturionCTF_ParseFloat(parts[3]);
			CENTURION_WSG_AUTH.HordeY = CenturionCTF_ParseFloat(parts[4]);
		else
			CENTURION_WSG_AUTH.HordeCarrier = nil;
			CENTURION_WSG_AUTH.HordeX = nil;
			CENTURION_WSG_AUTH.HordeY = nil;
		end
		CENTURION_WSG_AUTH.HordeState = state;
	end

	CENTURION_WSG_AUTH.HasFull = true;
	CenturionCTF_RefreshFlagFrames();
	return true;
end

local function CenturionCTF_HandlePayload(payload)
	if ( not payload or payload == "" ) then
		return false;
	end

	if ( string.sub(payload, 1, 5) == "FULL:" ) then
		return CenturionCTF_ApplyFullPayload(payload);
	end

	return CenturionCTF_ApplySidePayload(payload);
end

function CenturionCTF_HandleAddonMessage(prefix, message, channel, sender)
	if ( prefix == "CWSG" ) then
		return CenturionCTF_HandlePayload(message);
	end

	-- Some server builds deliver the prefix and payload as one tab-delimited chat body.
	if ( type(prefix) == "string" ) then
		local pfx, payload = string.match(prefix, "^(CWSG)\t(.+)$");
		if ( pfx and payload ) then
			return CenturionCTF_HandlePayload(payload);
		end
	end

	return false;
end

function CenturionCTF_HandleChatMessage(message, sender)
	if ( type(message) ~= "string" ) then
		return false;
	end

	local pfx, payload = string.match(message, "^(CWSG)\t(.+)$");
	if ( pfx and payload ) then
		return CenturionCTF_HandlePayload(payload);
	end

	return false;
end

local function CenturionCTF_GetAlwaysUpRowTeam(rowIndex, icon)
	local lowerIcon = CenturionCTF_NormalizeIconPath(icon);

	if ( string.find(lowerIcon, "pvp%-alliance") ) then
		return "ALLIANCE_TEAM";
	end
	if ( string.find(lowerIcon, "pvp%-horde") ) then
		return "HORDE_TEAM";
	end

	if ( rowIndex == 1 ) then
		return "ALLIANCE_TEAM";
	elseif ( rowIndex == 2 ) then
		return "HORDE_TEAM";
	end

	return nil;
end

local function CenturionWSG_GetAlwaysUpToken(rowIndex, icon, state, dynamicIcon)
	local rowTeam = CenturionCTF_GetAlwaysUpRowTeam(rowIndex, icon);
	if ( not rowTeam ) then
		return nil;
	end

	local lowerDynamicIcon = CenturionCTF_NormalizeIconPath(dynamicIcon);
	local isFlagDynamicIcon = string.find(lowerDynamicIcon, "hordeflag") or string.find(lowerDynamicIcon, "allianceflag");

	if ( not CenturionCTF_IsActive() and not isFlagDynamicIcon ) then
		return nil;
	end

	if ( tonumber(state) == 2 ) then
		if ( rowTeam == "ALLIANCE_TEAM" ) then
			return "HordeFlag";
		elseif ( rowTeam == "HORDE_TEAM" ) then
			return "AllianceFlag";
		end
	end

	if ( CenturionCTF_IsAuthReady() ) then
		if ( rowTeam == "ALLIANCE_TEAM" and CENTURION_WSG_AUTH.HordeState == "PLAYER" ) then
			return "HordeFlag";
		elseif ( rowTeam == "HORDE_TEAM" and CENTURION_WSG_AUTH.AllianceState == "PLAYER" ) then
			return "AllianceFlag";
		end
	end

	return "";
end

local function CenturionCTF_GetSingleHeldTokenFromWorldStates()
	local found = nil;
	local count = 0;
	local numUI = GetNumWorldStateUI and GetNumWorldStateUI() or 0;

	for i = 1, numUI do
		local uiType, state, text, icon, dynamicIcon = GetWorldStateUIInfo(i);
		local token = CenturionCTF_TokenFromPath(dynamicIcon);
		if ( token and tonumber(state) == 2 ) then
			found = token;
			count = count + 1;
		end
	end

	if ( count == 1 ) then
		return found;
	end
	return nil;
end

function CenturionWSG_GetFixedTokens(battlefieldFlags)
	local fixed = {};
	if ( not battlefieldFlags or not CenturionCTF_IsActive() ) then
		return fixed;
	end

	local stockCount = 0;
	for _, rawInfo in ipairs(battlefieldFlags) do
		if ( rawInfo.stock ) then
			stockCount = stockCount + 1;
		end
	end

	-- Authoritative CWSG entries are only used for dropped flags. Their token
	-- is already the physical flag identity, so never override it by order.
	for i, rawInfo in ipairs(battlefieldFlags) do
		if ( rawInfo.authoritative and rawInfo.token ) then
			fixed[i] = rawInfo.token;
		end
	end

	-- If both stock carrier markers are present, the stock server packet order is:
	--   first stock marker  = physical Alliance flag carrier
	--   second stock marker = physical Horde flag carrier
	-- This is not proximity guessing. It is the deterministic packet order.
	if ( stockCount >= 2 ) then
		local seenStock = 0;
		for i, rawInfo in ipairs(battlefieldFlags) do
			if ( rawInfo.stock ) then
				seenStock = seenStock + 1;
				if ( seenStock == 1 ) then
					fixed[i] = "AllianceFlag";
				elseif ( seenStock == 2 ) then
					fixed[i] = "HordeFlag";
				else
					fixed[i] = CenturionCTF_TokenFromPath(rawInfo.token) or rawInfo.token;
				end
			end
		end
		return fixed;
	end

	-- If only one stock carrier marker exists, CWSG state tells us which
	-- physical flag is currently on a player. Dropped flags, if any, are
	-- appended separately as authoritative entries above.
	if ( stockCount == 1 ) then
		local stockIndex = nil;
		for i, rawInfo in ipairs(battlefieldFlags) do
			if ( rawInfo.stock ) then
				stockIndex = i;
				break;
			end
		end

		if ( stockIndex ) then
			if ( CenturionCTF_IsAuthReady() ) then
				local aPlayer = CENTURION_WSG_AUTH.AllianceState == "PLAYER";
				local hPlayer = CENTURION_WSG_AUTH.HordeState == "PLAYER";

				if ( aPlayer and not hPlayer ) then
					fixed[stockIndex] = "AllianceFlag";
					return fixed;
				elseif ( hPlayer and not aPlayer ) then
					fixed[stockIndex] = "HordeFlag";
					return fixed;
				end
			end

			local singleHeldToken = CenturionCTF_GetSingleHeldTokenFromWorldStates();
			if ( singleHeldToken ) then
				fixed[stockIndex] = singleHeldToken;
				return fixed;
			end

			fixed[stockIndex] = CenturionCTF_TokenFromPath(battlefieldFlags[stockIndex].token) or battlefieldFlags[stockIndex].token;
		end

		return fixed;
	end

	-- No stock carrier markers. This is normal when flags are only dropped;
	-- authoritative dropped entries already have tokens.
	return fixed;
end

-- Kept only for compatibility with older patched files. We intentionally do not
-- draw separate authoritative markers here, because stock GetBattlefieldFlagPosition()
-- already provides the live moving X/Y. CWSG is identity/state only.
function CenturionWSG_GetAuthoritativeVisibleFlags()
	local flags = {};

	if ( not CenturionCTF_IsAuthReady() ) then
		return flags;
	end

	local function AddDropped(side, token, state, x, y)
		-- Use CWSG only for dropped flags. Live carrier movement comes from
		-- stock GetBattlefieldFlagPosition(), so we don't create stale duplicate
		-- CWSG markers while a player is moving with the flag.
		if ( state == "GROUND" and x and y ) then
			table.insert(flags, {
				side = side,
				token = token,
				state = state,
				x = x,
				y = y,
				authoritative = true,
			});
		end
	end

	AddDropped("A", "AllianceFlag", CENTURION_WSG_AUTH.AllianceState, CENTURION_WSG_AUTH.AllianceX, CENTURION_WSG_AUTH.AllianceY);
	AddDropped("H", "HordeFlag",    CENTURION_WSG_AUTH.HordeState,    CENTURION_WSG_AUTH.HordeX,    CENTURION_WSG_AUTH.HordeY);

	return flags;
end

function Centurion_GetFixedWSGFlagToken(markerX, markerY, defaultToken)
	local fixed = CenturionWSG_GetFixedTokens({ { x = markerX, y = markerY, token = defaultToken } });
	return fixed and fixed[1] or defaultToken;
end



function WorldStateAlwaysUpFrame_OnEvent(self, event, ...)
	if ( event == "CHAT_MSG_ADDON" ) then
		local prefix, message, channel, sender = ...;
		if ( CenturionCTF_HandleAddonMessage and CenturionCTF_HandleAddonMessage(prefix, message, channel, sender) ) then
			return;
		end
	elseif ( event == "CHAT_MSG_WHISPER" ) then
		local message, sender = ...;
		if ( CenturionCTF_HandleChatMessage and CenturionCTF_HandleChatMessage(message, sender) ) then
			return;
		end
	elseif ( event == "PLAYER_ENTERING_WORLD" ) then
		WorldStateFrame_ToggleBattlefieldMinimap();
		WorldStateAlwaysUpFrame_StopBGChatFilter(self);
		CenturionCTF_ResetAuthIfNotCTF();
		CenturionCTF_ClearWorldStateUIIfInactive();
		if ( CenturionCTF_IsActive() ) then
			WorldStateAlwaysUpFrame_Update();
			CenturionCTF_RequestFull();
		elseif ( GetNumWorldStateUI and GetNumWorldStateUI() > 0 ) then
			WorldStateAlwaysUpFrame_Update();
		end
	elseif ( event == "PLAYER_ENTERING_BATTLEGROUND" ) then
		WorldStateAlwaysUpFrame_StartBGChatFilter(self);
		CenturionCTF_RequestFull();
	else
		if ( event == "UPDATE_WORLD_STATES" or event == "ZONE_CHANGED" or event == "ZONE_CHANGED_INDOORS" or event == "ZONE_CHANGED_NEW_AREA" ) then
			CenturionCTF_ResetAuthIfNotCTF();
			CenturionCTF_ClearWorldStateUIIfInactive();
			if ( CenturionCTF_IsActive() and CENTURION_WSG_AUTH and not CENTURION_WSG_AUTH.HasFull ) then
				CenturionCTF_RequestFull();
			end
		end

		if ( CenturionCTF_IsActive() or (GetNumWorldStateUI and GetNumWorldStateUI() > 0) ) then
			WorldStateAlwaysUpFrame_Update();
		else
			CenturionCTF_ClearWorldStateUIIfInactive();
		end
	end
end


function WorldStateAlwaysUpFrame_Update()
	local numUI = GetNumWorldStateUI();
	if ( numUI > 0 and WorldStateAlwaysUpFrame ) then
		WorldStateAlwaysUpFrame:Show();
	end
	local name, frame, frameText, frameDynamicIcon, frameIcon, frameFlash, flashTexture, frameDynamicButton;
	local extendedUI, extendedUIState1, extendedUIState2, extendedUIState3, uiInfo; 
	local uiType, text, icon, state, dynamicIcon, tooltip, dynamicTooltip, flash, relative;
	local inInstance, instanceType = IsInInstance();
	local alwaysUpShown = 1;
	local extendedUIShown = 1;
	for i=1, numUI do
		uiType, state, text, icon, dynamicIcon, tooltip, dynamicTooltip, extendedUI, extendedUIState1, extendedUIState2, extendedUIState3 = GetWorldStateUIInfo(i);
		if ( (uiType ~= 1) or ((WORLD_PVP_OBJECTIVES_DISPLAY == "1") or (WORLD_PVP_OBJECTIVES_DISPLAY == "2" and IsSubZonePVPPOI()) or (instanceType == "pvp")) ) then
			if ( state > 0 ) then
				-- Handle always up frames and extended ui's completely differently
				if ( extendedUI ~= "" ) then
					-- extendedUI
					uiInfo = ExtendedUI[extendedUI]
					name = uiInfo.name..extendedUIShown;
					if ( extendedUIShown > NUM_EXTENDED_UI_FRAMES ) then
						frame = uiInfo.create(extendedUIShown);
						NUM_EXTENDED_UI_FRAMES = extendedUIShown;
					else
						frame = _G[name];
					end
					uiInfo.update(extendedUIShown, extendedUIState1, extendedUIState2, extendedUIState3);
					frame:Show();
					extendedUIShown = extendedUIShown + 1;
				else
					-- Always Up
					name = "AlwaysUpFrame"..alwaysUpShown;
					if ( alwaysUpShown > NUM_ALWAYS_UP_UI_FRAMES ) then
						frame = CreateFrame("Frame", name, WorldStateAlwaysUpFrame, "WorldStateAlwaysUpTemplate");
						NUM_ALWAYS_UP_UI_FRAMES = alwaysUpShown;
					else
						frame = _G[name];
					end
					if ( alwaysUpShown == 1 ) then
						frame:SetPoint("TOP", WorldStateAlwaysUpFrame, -23 , -20);
					else
						relative = _G["AlwaysUpFrame"..(alwaysUpShown - 1)];
						frame:SetPoint("TOP", relative, "BOTTOM");
					end
					frameText = _G[name.."Text"];
					frameIcon = _G[name.."Icon"];
					frameDynamicIcon = _G[name.."DynamicIconButtonIcon"];
					frameFlash = _G[name.."Flash"];
					flashTexture = _G[name.."FlashTexture"];
					frameDynamicButton = _G[name.."DynamicIconButton"];

					frameText:SetText(text);
					frameIcon:SetTexture(icon);

					local fixedDynamicIcon = dynamicIcon;
					local fixedTooltip = dynamicTooltip;
					local authToken = CenturionWSG_GetAlwaysUpToken(alwaysUpShown, icon, state, dynamicIcon);
					if ( authToken ~= nil ) then
						if ( authToken == "" ) then
							fixedDynamicIcon = "";
							fixedTooltip = nil;
						else
							fixedDynamicIcon = "Interface\\WorldStateFrame\\"..authToken;
							fixedTooltip = dynamicTooltip;
						end
					end

					local isCenturionCTFOverride = (authToken ~= nil);

					if ( fixedDynamicIcon ~= "" ) then
						frameDynamicIcon:SetTexture(fixedDynamicIcon);
						frameDynamicIcon:Show();
					else
						frameDynamicIcon:SetTexture(nil);
						frameDynamicIcon:Hide();
					end

					-- The stock white flash is exactly what makes the icon look wrong here.
					-- For Centurion CTF overrides, show/hide the carried flag icon plainly.
					flash = nil;
					if ( fixedDynamicIcon ~= "" and not isCenturionCTFOverride ) then
						flash = fixedDynamicIcon.."Flash";
					end
					flashTexture:SetTexture(flash);
					UIFrameFlashStop(frameFlash);

					frameDynamicButton.tooltip = fixedTooltip;
					if ( fixedDynamicIcon ~= "" ) then
						frameDynamicButton:Show();
					else
						frameDynamicButton:Hide();
					end

					if ( fixedDynamicIcon ~= "" and state == 2 and not isCenturionCTFOverride ) then
						UIFrameFlash(frameFlash, 0.5, 0.5, -1);
					end
					alwaysUpShown = alwaysUpShown + 1;
				end	
				if ( icon ~= "" ) then
					frame.tooltip = tooltip;
				else
					frame.tooltip = nil;
				end
				frame:Show();
			end
		end
	end
	for i=alwaysUpShown, NUM_ALWAYS_UP_UI_FRAMES do
		frame = _G["AlwaysUpFrame"..i];
		frame:Hide();
	end
	for i=extendedUIShown, NUM_EXTENDED_UI_FRAMES do
		frame = _G["WorldStateCaptureBar"..i];
		if ( frame ) then
			frame:Hide();
		end
	end
end

function WorldStateAlwaysUpFrame_OnUpdate(self, elapsed)
	WORLDSTATEALWAYSUPFRAME_TIMESINCELAST = WORLDSTATEALWAYSUPFRAME_TIMESINCELAST + elapsed;
	WORLDSTATEALWAYSUPFRAME_TIMESINCESTART = WORLDSTATEALWAYSUPFRAME_TIMESINCESTART + elapsed;
	if ( WORLDSTATEALWAYSUPFRAME_TIMESINCELAST >= WORLDSTATEALWAYSUPFRAME_DEFAULTINTERVAL ) then		
		local subtractedPlayers, playerString = 0;
		
		for i in next, SUBTRACTED_PLAYERS do 
			if ( not playerString ) then
				playerString = i;
			else
				playerString = playerString .. PLAYER_LIST_DELIMITER .. i;
			end
			
			subtractedPlayers = subtractedPlayers + 1;
		end

		local message, info;
		
		if ( subtractedPlayers > 0 ) then
			info = ChatTypeInfo["SYSTEM"];
			if ( subtractedPlayers > 1 and subtractedPlayers <= 3 ) then
				message = ERR_PLAYERLIST_LEFT_BATTLE;
				DEFAULT_CHAT_FRAME:AddMessage(string.format(message, subtractedPlayers, playerString), info.r, info.g, info.b, info.id);
			elseif ( subtractedPlayers > 3 ) then
				message = ERR_PLAYERS_LEFT_BATTLE_D;
				DEFAULT_CHAT_FRAME:AddMessage(string.format(message, subtractedPlayers), info.r, info.g, info.b, info.id);
			else
				message = ERR_PLAYER_LEFT_BATTLE_D;
				DEFAULT_CHAT_FRAME:AddMessage(string.format(message, playerString), info.r, info.g, info.b, info.id);
			end

			for i in next, SUBTRACTED_PLAYERS do
				SUBTRACTED_PLAYERS[i] = nil;
			end
		end
		
		local addedPlayers, playerString = 0;
		for i in next, ADDED_PLAYERS do
			if ( not playerString ) then
				playerString = i;
			else
				playerString = playerString .. PLAYER_LIST_DELIMITER .. i;
			end
			
			addedPlayers = addedPlayers + 1;
		end
		
		
		if ( addedPlayers > 0 ) then
			info = ChatTypeInfo["SYSTEM"];
			if ( addedPlayers > 1 and addedPlayers <= 3 ) then
				message = ERR_PLAYERLIST_JOINED_BATTLE;
				DEFAULT_CHAT_FRAME:AddMessage(string.format(message, addedPlayers, playerString), info.r, info.g, info.b, info.id);
			elseif ( addedPlayers > 3 ) then
				message = ERR_PLAYERS_JOINED_BATTLE_D;
				DEFAULT_CHAT_FRAME:AddMessage(string.format(message, addedPlayers), info.r, info.g, info.b, info.id);
			else
				message = ERR_PLAYER_JOINED_BATTLE_D;
				DEFAULT_CHAT_FRAME:AddMessage(string.format(message, playerString), info.r, info.g, info.b, info.id);
			end

			for i in next, ADDED_PLAYERS do
				ADDED_PLAYERS[i] = nil;
			end
		end
		
		WORLDSTATEALWAYSUPFRAME_TIMESINCELAST = 0;
	elseif ( WORLDSTATEALWAYSUPFRAME_TIMESINCESTART >= WORLDSTATEALWAYSUPFRAME_TIMETORUN ) then
		WORLDSTATEALWAYSUPFRAME_TIMESINCELAST = WORLDSTATEALWAYSUPFRAME_DEFAULTINTERVAL;
		WorldStateAlwaysUpFrame_OnUpdate(self, 0);
		self:SetScript("OnUpdate", nil);
	end
end

function WorldStateAlwaysUpFrame_StartBGChatFilter (self)
	inBattleground = true;
	
	-- Reset the OnUpdate timer variables
	WORLDSTATEALWAYSUPFRAME_TIMESINCELAST = -25;
	WORLDSTATEALWAYSUPFRAME_TIMESINCESTART = 0;
	
	ChatFrame_AddMessageEventFilter("CHAT_MSG_SYSTEM", WorldStateAlwaysUpFrame_FilterChatMsgSystem);
	ChatFrame_AddMessageEventFilter("CHAT_MSG_LOOT", WorldStateAlwaysUpFrame_FilterChatMsgLoot);
	
	self:SetScript("OnUpdate", WorldStateAlwaysUpFrame_OnUpdate);
end

function WorldStateAlwaysUpFrame_StopBGChatFilter (self)
	inBattleground = false;
	
	ChatFrame_RemoveMessageEventFilter("CHAT_MSG_SYSTEM", WorldStateAlwaysUpFrame_FilterChatMsgSystem);
	ChatFrame_RemoveMessageEventFilter("CHAT_MSG_LOOT", WorldStateAlwaysUpFrame_FilterChatMsgLoot);
	
	for i in next, ADDED_PLAYERS do
		ADDED_PLAYERS[i] = nil;
	end
	
	for i in next, SUBTRACTED_PLAYERS do
		SUBTRACTED_PLAYERS[i] = nil;
	end
	
	self:SetScript("OnUpdate", nil);
end

function WorldStateAlwaysUpFrame_FilterChatMsgSystem (self, event, ...)
	local playerName;
	
	local message = ...;
	
	if ( GetBattlefieldWinner() ) then
		-- Filter out leaving messages when the battleground is over.
		for i, str in next, FILTERED_BG_CHAT_SUBTRACT do
			playerName = string.match(message, str);
			if ( playerName ) then
				return true;
			end
		end
	elseif ( WORLDSTATEALWAYSUPFRAME_TIMESINCESTART < WORLDSTATEALWAYSUPFRAME_TIMETORUN ) then
		-- Filter out leaving and joining messages when the battleground starts.
		for i, str in next, FILTERED_BG_CHAT_ADD do
			playerName = string.match(message, str);
			if ( playerName ) then
				-- Trim realm names
				playerName = string.match(playerName, "([^%-]+)%-?.*");
				ADDED_PLAYERS[playerName] = true;
				return true;
			end
		end
		
		for i, str in next, FILTERED_BG_CHAT_SUBTRACT do
			playerName = string.match(message, str);
			if ( playerName ) then
				playerName = string.match(playerName, "([^%-]+)%-?.*");
				SUBTRACTED_PLAYERS[playerName] = true;
				return true;
			end
		end
	end
	return false;
end

local matchString = string.gsub(LOOT_ITEM_CREATED_SELF, "%%s%.", ".+")

function WorldStateAlwaysUpFrame_FilterChatMsgLoot (self, event, ...)
	if ( GetBattlefieldWinner() ) then
		local message = ...;
		-- Suppress loot messages for other players at the end of battlefields and arenas
		if ( not string.match(message, matchString) ) then
			return true;
		end
	end
	
	return false;
end

function WorldStateFrame_ToggleBattlefieldMinimap()
	local _, instanceType = IsInInstance();
	if ( instanceType ~= "pvp" and instanceType ~= "none" ) then
		if ( BattlefieldMinimap and BattlefieldMinimap:IsShown() ) then
			BattlefieldMinimap:Hide();
		end
		return;
	end

	if ( WorldStateFrame_CanShowBattlefieldMinimap() ) then
		if ( not BattlefieldMinimap ) then
			BattlefieldMinimap_LoadUI();
		end
		BattlefieldMinimap:Show();
	end
end

function WorldStateFrame_CanShowBattlefieldMinimap()
	local _, instanceType = IsInInstance();

	if ( instanceType == "pvp" ) then
		return GetCVar("showBattlefieldMinimap") == "1";
	end

	if ( instanceType == "none" ) then
		return GetCVar("showBattlefieldMinimap") == "2";
	end

	return false;
end

-- UI Specific functions
function CaptureBar_Create(id)
	local frame = CreateFrame("Frame", "WorldStateCaptureBar"..id, UIParent, "WorldStateCaptureBarTemplate");
	return frame;
end

function CaptureBar_Update(id, value, neutralPercent)
	local position = 25 + 124*(1 - value/100);
	local bar = _G["WorldStateCaptureBar"..id];
	local barSize = 121;
	if ( not bar.oldValue ) then
		bar.oldValue = position;
	end
	-- Show an arrow in the direction the bar is moving
	if ( position < bar.oldValue ) then
		_G["WorldStateCaptureBar"..id.."IndicatorLeft"]:Show();
		_G["WorldStateCaptureBar"..id.."IndicatorRight"]:Hide();
	elseif ( position > bar.oldValue ) then
		_G["WorldStateCaptureBar"..id.."IndicatorLeft"]:Hide();
		_G["WorldStateCaptureBar"..id.."IndicatorRight"]:Show();
	else
		_G["WorldStateCaptureBar"..id.."IndicatorLeft"]:Hide();
		_G["WorldStateCaptureBar"..id.."IndicatorRight"]:Hide();
	end
	-- Figure out if the ticker is in neutral territory or on a faction's side
	if ( value > (50 + neutralPercent/2) ) then
		_G["WorldStateCaptureBar"..id.."LeftIconHighlight"]:Show();
		_G["WorldStateCaptureBar"..id.."RightIconHighlight"]:Hide();
	elseif ( value < (50 - neutralPercent/2) ) then
		_G["WorldStateCaptureBar"..id.."LeftIconHighlight"]:Hide();
		_G["WorldStateCaptureBar"..id.."RightIconHighlight"]:Show();
	else
		_G["WorldStateCaptureBar"..id.."LeftIconHighlight"]:Hide();
		_G["WorldStateCaptureBar"..id.."RightIconHighlight"]:Hide();
	end
	-- Setup the size of the neutral bar
	local middleBar = _G["WorldStateCaptureBar"..id.."MiddleBar"];
	local leftLine = _G["WorldStateCaptureBar"..id.."LeftLine"];
	if ( neutralPercent == 0 ) then
		middleBar:SetWidth(1);
		leftLine:Hide();
	else
		middleBar:SetWidth(neutralPercent/100*barSize);
		leftLine:Show();
	end

	bar.oldValue = position;
	_G["WorldStateCaptureBar"..id.."Indicator"]:SetPoint("CENTER", "WorldStateCaptureBar"..id, "LEFT", position, 0);
end


-- This has to be after all the functions are loaded
ExtendedUI["CAPTUREPOINT"] = {
	name = "WorldStateCaptureBar",
	create = CaptureBar_Create,
	update = CaptureBar_Update,
	onHide = CaptureBar_Hide,
}

-------------- FINAL SCORE FUNCTIONS ---------------

function WorldStateScoreFrame_OnLoad(self)
	self:RegisterEvent("UPDATE_BATTLEFIELD_SCORE");
	self:RegisterEvent("UPDATE_WORLD_STATES");
	self:RegisterEvent("PLAYER_ENTERING_WORLD");

	-- Tab Handling code
	PanelTemplates_SetNumTabs(self, 3);

	UIDropDownMenu_Initialize( ScorePlayerDropDown, ScorePlayerDropDown_Initialize, "MENU");
end

function WorldStateScoreFrame_Update()
	local isArena, isRegistered = IsActiveBattlefieldArena();

	if ( isArena ) then
		-- Hide unused tabs
		WorldStateScoreFrameTab1:Hide();
		WorldStateScoreFrameTab2:Hide();
		WorldStateScoreFrameTab3:Hide();
	
		-- Hide unused columns
		WorldStateScoreFrameTeam:Hide();
		WorldStateScoreFrameDeaths:Hide();
		WorldStateScoreFrameHK:Hide();

		-- Reanchor some columns.
		WorldStateScoreFrameDamageDone:SetPoint("LEFT", "WorldStateScoreFrameKB", "RIGHT", -5, 0);
		if ( isRegistered ) then
			WorldStateScoreFrameTeam:Show();
			WorldStateScoreFrameHonorGained:Show();
			WorldStateScoreFrameHonorGainedText:SetText(SCORE_RATING_CHANGE);
			WorldStateScoreFrameHonorGained.sortType = "team";
			WorldStateScoreFrameHonorGained.tooltip = RATING_CHANGE_TOOLTIP;
			WorldStateScoreFrameTeamSkill:Show();
			WorldStateScoreFrameTeamSkillText:SetText(SCORE_TEAM_SKILL);
			WorldStateScoreFrameTeamSkill.sortType = "team";
			WorldStateScoreFrameTeamSkill.tooltip = TEAM_SKILL_TOOLTIP;
			WorldStateScoreFrameKB:SetPoint("LEFT", "WorldStateScoreFrameTeam", "RIGHT", -10, 0);
		else
			WorldStateScoreFrameHonorGained:Hide();
			WorldStateScoreFrameTeamSkill:Hide();
			WorldStateScoreFrameKB:SetPoint("LEFT", "WorldStateScoreFrameName", "RIGHT", 4, 0);
		end
	else
		-- Show Tabs
		WorldStateScoreFrameTab1:Show();
		WorldStateScoreFrameTab2:Hide();
		WorldStateScoreFrameTab3:Hide();

		WorldStateScoreFrameTeam:Hide();
		WorldStateScoreFrameTeamSkill:Hide();
		WorldStateScoreFrameDeaths:Show();
		WorldStateScoreFrameHK:Show();
		WorldStateScoreFrameHonorGained.sortType = "cp";
		WorldStateScoreFrameHonorGainedText:SetText("Team");
		WorldStateScoreFrameHonorGained.tooltip = "Team";
		WorldStateScoreFrameHKText:SetText(SCORE_HONORABLE_KILLS);
		WorldStateScoreFrameHonorGained:Show();
		-- Reanchor some columns.
		WorldStateScoreFrameDamageDone:SetPoint("LEFT", "WorldStateScoreFrameHK", "RIGHT", -5, 0);
		WorldStateScoreFrameKB:SetPoint("LEFT", "WorldStateScoreFrameName", "RIGHT", 4, 0);
	end

	--Show the frame if its hidden and there is a victor
	local battlefieldWinner = GetBattlefieldWinner(); 
	if ( battlefieldWinner ) then
		-- Show the final score frame, set textures etc.
		ShowUIPanel(WorldStateScoreFrame);
		if ( isArena ) then
			WorldStateScoreFrameLeaveButton:SetText(LEAVE_ARENA);
			WorldStateScoreFrameTimerLabel:SetText(TIME_TO_PORT_ARENA);
		else
			WorldStateScoreFrameLeaveButton:SetText(LEAVE_BATTLEGROUND);				
			WorldStateScoreFrameTimerLabel:SetText(TIME_TO_PORT);
		end
		
		WorldStateScoreFrameLeaveButton:Show();
		WorldStateScoreFrameTimerLabel:Show();
		WorldStateScoreFrameTimer:Show();

		-- Show winner
		if ( isArena ) then
			if ( isRegistered ) then
				local teamName = GetBattlefieldTeamInfo(battlefieldWinner);
				if ( teamName ) then
					WorldStateScoreWinnerFrameText:SetFormattedText(VICTORY_TEXT_ARENA_WINS, teamName);			
				else
					WorldStateScoreWinnerFrameText:SetText(VICTORY_TEXT_ARENA_DRAW);							
				end
			else
				WorldStateScoreWinnerFrameText:SetText(_G["VICTORY_TEXT_ARENA"..battlefieldWinner]);
			end
			if ( battlefieldWinner == 0 ) then
				-- Green Team won
				WorldStateScoreWinnerFrameLeft:SetVertexColor(0.19, 0.57, 0.11);
				WorldStateScoreWinnerFrameRight:SetVertexColor(0.19, 0.57, 0.11);
				WorldStateScoreWinnerFrameText:SetVertexColor(0.1, 1.0, 0.1);	
			else		
				-- Gold Team won
				WorldStateScoreWinnerFrameLeft:SetVertexColor(0.85, 0.71, 0.26);
				WorldStateScoreWinnerFrameRight:SetVertexColor(0.85, 0.71, 0.26);
				WorldStateScoreWinnerFrameText:SetVertexColor(1, 0.82, 0);	
			end
		else
			WorldStateScoreWinnerFrameText:SetText(_G["VICTORY_TEXT"..battlefieldWinner]);
			if ( battlefieldWinner == 0 ) then
				-- Horde won
				WorldStateScoreWinnerFrameLeft:SetVertexColor(0.52, 0.075, 0.18);
				WorldStateScoreWinnerFrameRight:SetVertexColor(0.5, 0.075, 0.18);
				WorldStateScoreWinnerFrameText:SetVertexColor(1.0, 0.1, 0.1);
			else
				-- Alliance won
				WorldStateScoreWinnerFrameLeft:SetVertexColor(0.11, 0.26, 0.51);
				WorldStateScoreWinnerFrameRight:SetVertexColor(0.11, 0.26, 0.51);
				WorldStateScoreWinnerFrameText:SetVertexColor(0, 0.68, 0.94);	
			end
		end
		WorldStateScoreWinnerFrame:Show();
	else
		WorldStateScoreWinnerFrame:Hide();
		WorldStateScoreFrameLeaveButton:Hide();
		WorldStateScoreFrameTimerLabel:Hide();
		WorldStateScoreFrameTimer:Hide();
	end
	
	-- Update buttons
	local numScores = GetNumBattlefieldScores();

	local scoreButton, buttonIcon, buttonClass, buttonName, buttonNameText, nameButton, buttonKills, buttonKillingBlows, buttonDeaths, buttonHonorGained, buttonTeamSkill, buttonFaction, columnButtonText, columnButtonIcon, buttonFactionLeft, buttonFactionRight, buttonDamage, buttonHealing, buttonTeam;
	local name, kills, killingBlows, honorableKills, deaths, honorGained, faction, rank, race, class, classToken, damageDone, healingDone;
	local teamName, teamRating, newTeamRating, teamSkill;
	local index;
	local columnData;

        -- ScrollFrame update
	local hasScrollBar;
	if ( numScores > MAX_WORLDSTATE_SCORE_BUTTONS ) then
		hasScrollBar = 1;
		WorldStateScoreScrollFrame:Show();
	else
		WorldStateScoreScrollFrame:Hide();
        end
	FauxScrollFrame_Update(WorldStateScoreScrollFrame, numScores, MAX_WORLDSTATE_SCORE_BUTTONS, 16 );

	-- Setup Columns
	local text, icon, tooltip, columnButton;
	local numStatColumns = GetNumBattlefieldStats();
	local columnButton, columnButtonText, columnTextButton, columnIcon;
	local honorGainedAnchorFrame = "WorldStateScoreFrameHealingDone";
	for i=1, MAX_NUM_STAT_COLUMNS do
		if ( i <= numStatColumns ) then
			text, icon, tooltip = GetBattlefieldStatInfo(i);
			columnButton = _G["WorldStateScoreColumn"..i];
			columnButtonText = _G["WorldStateScoreColumn"..i.."Text"];
			columnButtonText:SetText(text);
			columnButton.icon = icon;
			columnButton.tooltip = tooltip;
			
			columnTextButton = _G["WorldStateScoreButton1Column"..i.."Text"];

			if ( icon ~= "" ) then
				columnTextButton:SetPoint("CENTER", "WorldStateScoreColumn"..i, "CENTER", 6, WORLDSTATECOREFRAME_BUTTON_TEXT_OFFSET);
			else
				columnTextButton:SetPoint("CENTER", "WorldStateScoreColumn"..i, "CENTER", -1, WORLDSTATECOREFRAME_BUTTON_TEXT_OFFSET);
			end

			
			if ( i == numStatColumns ) then
				honorGainedAnchorFrame = "WorldStateScoreColumn"..i;
			end
		
			_G["WorldStateScoreColumn"..i]:Show();
		else
			_G["WorldStateScoreColumn"..i]:Hide();
		end
	end
	
	if ( isArena and isRegistered ) then
		WorldStateScoreFrameTeamSkill:SetPoint("LEFT", honorGainedAnchorFrame, "RIGHT", 10, 0);
		honorGainedAnchorFrame = "WorldStateScoreFrameTeamSkill";
	end
	
	-- Anchor the bonus honor gained to the last column shown
	WorldStateScoreFrameHonorGained:SetPoint("LEFT", honorGainedAnchorFrame, "RIGHT", 5, 0);
	
	-- Last button shown is what the player count anchors to
	local lastButtonShown = "WorldStateScoreButton1";
	local teamDataFailed, coords;

	for i=1, MAX_WORLDSTATE_SCORE_BUTTONS do
		-- Need to create an index adjusted by the scrollframe offset
		index = FauxScrollFrame_GetOffset(WorldStateScoreScrollFrame) + i;
		scoreButton = _G["WorldStateScoreButton"..i];
		if ( hasScrollBar ) then
			scoreButton:SetWidth(WorldStateScoreFrame.scrollBarButtonWidth);
		else
			scoreButton:SetWidth(WorldStateScoreFrame.buttonWidth);
		end
		if ( index <= numScores ) then
			buttonClass = _G["WorldStateScoreButton"..i.."ClassButtonIcon"];
			buttonName = _G["WorldStateScoreButton"..i.."Name"];
			buttonNameText = _G["WorldStateScoreButton"..i.."NameText"];
			buttonTeam =  _G["WorldStateScoreButton"..i.."Team"];
			buttonKills = _G["WorldStateScoreButton"..i.."HonorableKills"];
			buttonKillingBlows = _G["WorldStateScoreButton"..i.."KillingBlows"];
			buttonDeaths = _G["WorldStateScoreButton"..i.."Deaths"];
			buttonDamage = _G["WorldStateScoreButton"..i.."Damage"];
			buttonHealing = _G["WorldStateScoreButton"..i.."Healing"];
			buttonTeamSkill = _G["WorldStateScoreButton"..i.."TeamSkill"];
			buttonHonorGained = _G["WorldStateScoreButton"..i.."HonorGained"];
			buttonFactionLeft = _G["WorldStateScoreButton"..i.."FactionLeft"];
			buttonFactionRight = _G["WorldStateScoreButton"..i.."FactionRight"];
			
			name, killingBlows, honorableKills, deaths, honorGained, faction, rank, race, class, classToken, damageDone, healingDone = GetBattlefieldScore(index);
			-- Non-arena MSG_PVP_LOG_DATA never sends a faction byte over the
			-- wire, so the client's native faction guess for this row is
			-- derived from the player's race, which is wrong on cross-faction
			-- battlegrounds (a player's home faction need not match their BG
			-- team). Re-derive team from the honor sign instead.
			if ( not isArena ) then
				if ( honorGained > 0 ) then
					faction = 0;
				else
					faction = 1;
				end
			end
			if ( classToken ) then
				coords = CLASS_BUTTONS[classToken];
				buttonClass:SetTexture("Interface\\WorldStateFrame\\Icons-Classes");
				buttonClass:SetTexCoord(coords[1], coords[2], coords[3], coords[4]);
				buttonClass:Show();
			else
				buttonClass:Hide();
			end
			
			buttonNameText:SetText(name);
			if ( not race ) then
				race = "";
			end
			if ( not class ) then
				class = "";
			end
			buttonName.name = name;
			buttonName.tooltip = race.." "..class;
			_G["WorldStateScoreButton"..i.."ClassButton"].tooltip = class;
			buttonKillingBlows:SetText(killingBlows);
			buttonDamage:SetText(damageDone);
			buttonHealing:SetText(healingDone);
			teamDataFailed = 0;
			teamName, teamRating, newTeamRating, teamSkill = GetBattlefieldTeamInfo(faction);

			if ( not teamRating ) then
				teamDataFailed = 1;
			end
			
			if ( not newTeamRating ) then
				teamDataFailed = 1;
			end

			if ( isArena ) then
				if ( isRegistered ) then
					buttonTeam:SetText(teamName);
					buttonTeam:Show();
					buttonTeamSkill:SetText(teamSkill);
					buttonTeamSkill:Show();
					if ( teamDataFailed == 1 ) then
						buttonHonorGained:SetText("-------");
					else
						buttonHonorGained:SetText(tostring(newTeamRating - teamRating));
					end
					buttonHonorGained:Show();
				else
					buttonTeamSkill:Hide();
					buttonHonorGained:Hide();
					buttonTeam:Hide();
				end
				buttonKills:Hide();
				buttonDeaths:Hide();
			else
				buttonKills:SetText(honorableKills);
				buttonDeaths:SetText(deaths);
				-- faction here is the cross-faction-corrected team (0 = Horde,
				-- 1 = Alliance, set above from the honorGained sign), not the
				-- player's native race faction. Show it as a plain letter.
				if ( faction ) then
					buttonHonorGained:SetText(faction == 0 and "H" or "A");
				else
					buttonHonorGained:SetText("");
				end
				buttonTeam:Hide();
				buttonTeamSkill:Hide();
				buttonKills:Show();
				buttonDeaths:Show();
				buttonHonorGained:Show();
			end
			
			for j=1, MAX_NUM_STAT_COLUMNS do
				columnButtonText = _G["WorldStateScoreButton"..i.."Column"..j.."Text"];
				columnButtonIcon = _G["WorldStateScoreButton"..i.."Column"..j.."Icon"];
				if ( j <= numStatColumns ) then
					-- If there's an icon then move the icon left and format the text with an "x" in front
					columnData = GetBattlefieldStatData(index, j);
					if ( _G["WorldStateScoreColumn"..j].icon ~= "" ) then
						if ( columnData > 0 ) then
							columnButtonText:SetFormattedText(FLAG_COUNT_TEMPLATE, columnData);
							columnButtonIcon:SetTexture(_G["WorldStateScoreColumn"..j].icon..faction);
							columnButtonIcon:Show();
						else
							columnButtonText:SetText("");
							columnButtonIcon:Hide();
						end
						
					else
						columnButtonText:SetText(columnData);
						columnButtonIcon:Hide();
					end
					columnButtonText:Show();
				else
					columnButtonText:Hide();
					columnButtonIcon:Hide();
				end
			end
			if ( faction ) then
				if ( faction == 0 ) then
					if ( isArena ) then
						-- Green Team 
						buttonFactionLeft:SetVertexColor(0.19, 0.57, 0.11);
						buttonFactionRight:SetVertexColor(0.19, 0.57, 0.11);
						buttonNameText:SetVertexColor(0.1, 1.0, 0.1);	
					else
						-- Horde
						buttonFactionLeft:SetVertexColor(0.52, 0.075, 0.18);
						buttonFactionRight:SetVertexColor(0.5, 0.075, 0.18);
						buttonNameText:SetVertexColor(1.0, 0.1, 0.1);
					end
				else
					if ( isArena ) then
						-- Gold Team 
						buttonFactionLeft:SetVertexColor(0.85, 0.71, 0.26);
						buttonFactionRight:SetVertexColor(0.85, 0.71, 0.26);
						buttonNameText:SetVertexColor(1, 0.82, 0);	
					else
						-- Alliance 
						buttonFactionLeft:SetVertexColor(0.11, 0.26, 0.51);
						buttonFactionRight:SetVertexColor(0.11, 0.26, 0.51);
						buttonNameText:SetVertexColor(0, 0.68, 0.94);	
					end
				end
				if ( ( not isArena ) and ( name == UnitName("player") ) ) then
					buttonNameText:SetVertexColor(1.0, 0.82, 0);
				end
				buttonFactionLeft:Show();
				buttonFactionRight:Show();
			else
				buttonFactionLeft:Hide();
				buttonFactionRight:Hide();
			end
			lastButtonShown = scoreButton:GetName();
			scoreButton:Show();
		else
			scoreButton:Hide();
		end
	end

	-- Count number of players on each side
	local numHorde = 0;
	local numAlliance = 0;
	for i=1, numScores do
		name, killingBlows, honorableKills, deaths, honorGained, faction, rank, race, class = GetBattlefieldScore(i);
		if ( not isArena ) then
			if ( honorGained > 0 ) then
				faction = 0;
			else
				faction = 1;
			end
		end
		if ( faction ) then
			if ( faction == 0 ) then
				numHorde = numHorde + 1;
			else
				numAlliance = numAlliance + 1;
			end
		end
	end
	
	-- Set count text and anchor team count to last button shown
	WorldStateScorePlayerCount:Show();
	if ( numHorde > 0 and numAlliance > 0 ) then
		WorldStateScorePlayerCount:SetText(format(PLAYER_COUNT_ALLIANCE, numAlliance).." / "..format(PLAYER_COUNT_HORDE, numHorde));
	elseif ( numAlliance > 0 ) then
		WorldStateScorePlayerCount:SetFormattedText(PLAYER_COUNT_ALLIANCE, numAlliance);
	elseif ( numHorde > 0 ) then
		WorldStateScorePlayerCount:SetFormattedText(PLAYER_COUNT_HORDE, numHorde);
	else
		WorldStateScorePlayerCount:Hide();
	end
	if ( isArena ) then
		WorldStateScorePlayerCount:Hide();
	end

	WorldStateScorePlayerCount:SetPoint("TOPLEFT", lastButtonShown, "BOTTOMLEFT", 15, -6);
	WorldStateScoreBattlegroundRunTime:SetText(TIME_ELAPSED.." "..SecondsToTime(GetBattlefieldInstanceRunTime()/1000, 1));
	WorldStateScoreBattlegroundRunTime:SetPoint("TOPRIGHT", lastButtonShown, "BOTTOMRIGHT", -20, -7);
end

function WorldStateScoreFrame_Resize(width)
	local isArena, isRegistered = IsActiveBattlefieldArena();
	local columns = WORLDSTATESCOREFRAME_BASE_COLUMNS;
	local scrollBar = 37;
	local name;
	if ( not width ) then

		width = WORLDSTATESCOREFRAME_PADDING + WorldStateScoreFrameName:GetWidth() + WorldStateScoreFrameClass:GetWidth();

		if ( isArena ) then
			columns = 3;
			if ( isRegistered ) then
				columns = 5;
				width = width + WorldStateScoreFrameTeam:GetWidth();
			else
				width = width + 43;
			end
		end

		columns = columns + 1 + GetNumBattlefieldStats();
	
		width = width + (columns*WORLDSTATESCOREFRAME_COLUMN_SPACING);

		if ( WorldStateScoreScrollFrame:IsShown() ) then
			width = width + scrollBar;
		end
	end
	
	WorldStateScoreFrame:SetWidth(width);
	
	WorldStateScoreFrameTopBackground:SetWidth(WorldStateScoreFrame:GetWidth()-129);
	WorldStateScoreFrameTopBackground:SetTexCoord(0, WorldStateScoreFrameTopBackground:GetWidth()/256, 0, 1.0);
	WorldStateScoreFrame.scrollBarButtonWidth = WorldStateScoreFrame:GetWidth() - 165;
	WorldStateScoreFrame.buttonWidth = WorldStateScoreFrame:GetWidth() - 137;
	WorldStateScoreScrollFrame:SetWidth(WorldStateScoreFrame.scrollBarButtonWidth);

	-- Position Column data horizontally
	local buttonTeam, buttonTeamSkill, buttonKills, buttonKillingBlows, buttonDeaths, buttonDamage, buttonHealing, buttonHonorGained, buttonReturnedIcon, buttonCapturedIcon;
	for i=1, MAX_WORLDSTATE_SCORE_BUTTONS do
		if ( isRegistered ) then
			buttonTeam = _G["WorldStateScoreButton"..i.."Team"];
			buttonTeamSkill = _G["WorldStateScoreButton"..i.."TeamSkill"];
		end
		
		buttonKills = _G["WorldStateScoreButton"..i.."HonorableKills"];
		buttonKillingBlows = _G["WorldStateScoreButton"..i.."KillingBlows"];
		buttonDeaths = _G["WorldStateScoreButton"..i.."Deaths"];
		buttonDamage = _G["WorldStateScoreButton"..i.."Damage"];
		buttonHealing = _G["WorldStateScoreButton"..i.."Healing"];
		buttonHonorGained = _G["WorldStateScoreButton"..i.."HonorGained"];
		if ( i == 1 ) then
			if ( isRegistered ) then
				buttonTeam:SetPoint("LEFT", "WorldStateScoreFrameTeam", "LEFT", 0, WORLDSTATECOREFRAME_BUTTON_TEXT_OFFSET);
				buttonTeamSkill:SetPoint("CENTER", "WorldStateScoreFrameTeamSkill", "CENTER", 0, WORLDSTATECOREFRAME_BUTTON_TEXT_OFFSET);
			end
			buttonKills:SetPoint("CENTER", "WorldStateScoreFrameHK", "CENTER", 0, WORLDSTATECOREFRAME_BUTTON_TEXT_OFFSET);
			buttonKillingBlows:SetPoint("CENTER", "WorldStateScoreFrameKB", "CENTER", 0, WORLDSTATECOREFRAME_BUTTON_TEXT_OFFSET);
			buttonDeaths:SetPoint("CENTER", "WorldStateScoreFrameDeaths", "CENTER", 0, WORLDSTATECOREFRAME_BUTTON_TEXT_OFFSET);
			buttonDamage:SetPoint("CENTER", "WorldStateScoreFrameDamageDone", "CENTER", 0, WORLDSTATECOREFRAME_BUTTON_TEXT_OFFSET);
			buttonHealing:SetPoint("CENTER", "WorldStateScoreFrameHealingDone", "CENTER", 0, WORLDSTATECOREFRAME_BUTTON_TEXT_OFFSET);
			buttonHonorGained:SetPoint("CENTER", "WorldStateScoreFrameHonorGained", "CENTER", 0, WORLDSTATECOREFRAME_BUTTON_TEXT_OFFSET);
			for j=1, MAX_NUM_STAT_COLUMNS do
				_G["WorldStateScoreButton"..i.."Column"..j.."Text"]:SetPoint("CENTER", _G["WorldStateScoreColumn"..j], "CENTER", 0,  WORLDSTATECOREFRAME_BUTTON_TEXT_OFFSET);
			end
		else
			if ( isRegistered ) then
				buttonTeam:SetPoint("LEFT", "WorldStateScoreButton"..(i-1).."Team", "LEFT", 0,  -16);
				buttonTeamSkill:SetPoint("CENTER", "WorldStateScoreButton"..(i-1).."TeamSkill", "CENTER", 0, -16);
			end
			buttonKills:SetPoint("CENTER", "WorldStateScoreButton"..(i-1).."HonorableKills", "CENTER", 0, -16);
			buttonKillingBlows:SetPoint("CENTER", "WorldStateScoreButton"..(i-1).."KillingBlows", "CENTER", 0, -16);
			buttonDeaths:SetPoint("CENTER", "WorldStateScoreButton"..(i-1).."Deaths", "CENTER", 0, -16);
			buttonDamage:SetPoint("CENTER", "WorldStateScoreButton"..(i-1).."Damage", "CENTER", 0, -16);
			buttonHealing:SetPoint("CENTER", "WorldStateScoreButton"..(i-1).."Healing", "CENTER", 0, -16);
			buttonHonorGained:SetPoint("CENTER", "WorldStateScoreButton"..(i-1).."HonorGained", "CENTER", 0, -16);
			for j=1, MAX_NUM_STAT_COLUMNS do
				_G["WorldStateScoreButton"..i.."Column"..j.."Text"]:SetPoint("CENTER", "WorldStateScoreButton"..(i-1).."Column"..j.."Text", "CENTER", 0, -16);
			end
		end
	end
	return width;
end

function WorldStateScoreFrameTab_OnClick(tab)
	local faction = tab:GetID();
	PanelTemplates_SetTab(WorldStateScoreFrame, faction);
	if ( faction == 2 ) then
		faction = 1;
	elseif ( faction == 3 ) then
		faction = 0;
	else
		faction = nil;
	end
	WorldStateScoreFrameLabel:SetFormattedText(STAT_TEMPLATE, tab:GetText());
	SetBattlefieldScoreFaction(faction);
	PlaySound("igCharacterInfoTab");
end

function ToggleWorldStateScoreFrame()
	if ( WorldStateScoreFrame:IsShown() ) then
		HideUIPanel(WorldStateScoreFrame);
	else
		if ( not IsActiveBattlefieldArena() and MiniMapBattlefieldFrame.status == "active" ) then
			ShowUIPanel(WorldStateScoreFrame);
		end
	end
end

-- Report AFK feature
local AFK_PLAYER_CLICKED = nil;

function ScorePlayer_OnMouseUp(self, mouseButton)
	if ( mouseButton == "RightButton" ) then
		if ( not UnitIsUnit(self.name,"player") and UnitInRaid(self.name)) then
			AFK_PLAYER_CLICKED = self.name;
			ToggleDropDownMenu(1, nil, ScorePlayerDropDown, self:GetName(), 0, -5);
		end
	end
end

function ScorePlayerDropDown_OnClick()
	ReportPlayerIsPVPAFK(AFK_PLAYER_CLICKED);
	PlaySound("UChatScrollButton");
	AFK_PLAYER_CLICKED = nil;
end

function ScorePlayerDropDown_Cancel()
	AFK_PLAYER_CLICKED = nil;
	PlaySound("UChatScrollButton");
end

function ScorePlayerDropDown_Initialize()
	local info = UIDropDownMenu_CreateInfo();
	info.text = PVP_REPORT_AFK;
	info.func = ScorePlayerDropDown_OnClick;
	UIDropDownMenu_AddButton(info);

	info = UIDropDownMenu_CreateInfo();
	info.text = CANCEL;
	info.func = ScorePlayerDropDown_Cancel;
	UIDropDownMenu_AddButton(info);
end
