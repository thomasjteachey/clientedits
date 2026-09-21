CHARACTER_SELECT_ROTATION_START_X = nil;
CHARACTER_SELECT_INITIAL_FACING = nil;

CHARACTER_ROTATION_CONSTANT = 0.6;

MAX_CHARACTERS_DISPLAYED = 10;
MAX_CHARACTERS_PER_REALM = 10;


function CharacterSelect_OnLoad(self)
	self:SetSequence(0);
	self:SetCamera(0);

	self.createIndex = 0;
	self.selectedIndex = 0;
	self.selectLast = 0;
	self.currentModel = nil;
	self:RegisterEvent("ADDON_LIST_UPDATE");
	self:RegisterEvent("CHARACTER_LIST_UPDATE");
	self:RegisterEvent("UPDATE_SELECTED_CHARACTER");
	self:RegisterEvent("SELECT_LAST_CHARACTER");
	self:RegisterEvent("SELECT_FIRST_CHARACTER");
	self:RegisterEvent("SUGGEST_REALM");
	self:RegisterEvent("FORCE_RENAME_CHARACTER");

	-- CharacterSelect:SetModel("Interface\\Glues\\Models\\UI_Orc\\UI_Orc.m2");

	-- local fogInfo = CharModelFogInfo["ORC"];
	-- CharacterSelect:SetFogColor(fogInfo.r, fogInfo.g, fogInfo.b);
	-- CharacterSelect:SetFogNear(0);
	-- CharacterSelect:SetFogFar(fogInfo.far);

	SetCharSelectModelFrame("CharacterSelect");

	-- Color edit box backdrops
	local backdropColor = DEFAULT_TOOLTIP_COLOR;
	CharacterSelectCharacterFrame:SetBackdropBorderColor(backdropColor[1], backdropColor[2], backdropColor[3]);
	CharacterSelectCharacterFrame:SetBackdropColor(backdropColor[4], backdropColor[5], backdropColor[6], 0.85);
	
end

function CharacterSelect_OnShow()
	-- request account data times from the server (so we know if we should refresh keybindings, etc...)
	ReadyForAccountDataTimes()
	
	local CurrentModel = CharacterSelect.currentModel;

	if ( CurrentModel ) then
		PlayGlueAmbience(GlueAmbienceTracks[strupper(CurrentModel)], 4.0);
	end

	UpdateAddonButton();

	local serverName, isPVP, isRP = GetServerName();
	local connected = IsConnectedToServer();
	local serverType = "";
	if ( serverName ) then
		if( not connected ) then
			serverName = serverName.."\n("..SERVER_DOWN..")";
		end
		if ( isPVP ) then
			if ( isRP ) then
				serverType = RPPVP_PARENTHESES;
			else
				serverType = PVP_PARENTHESES;
			end
		elseif ( isRP ) then
			serverType = RP_PARENTHESES;
		end
		CharSelectRealmName:SetText(serverName.." "..serverType);
		CharSelectRealmName:Show();
	else
		CharSelectRealmName:Hide();
	end

	if ( connected ) then
		GetCharacterListUpdate();
	else
		UpdateCharacterList();
	end

	-- Gameroom billing stuff (For Korea and China only)
	if ( SHOW_GAMEROOM_BILLING_FRAME ) then
		local paymentPlan, hasFallBackBillingMethod, isGameRoom = GetBillingPlan();
		if ( paymentPlan == 0 ) then
			-- No payment plan
			GameRoomBillingFrame:Hide();
			CharacterSelectRealmSplitButton:ClearAllPoints();
			CharacterSelectRealmSplitButton:SetPoint("TOP", CharacterSelectLogo, "BOTTOM", 0, -5);
		else
			local billingTimeLeft = GetBillingTimeRemaining();
			-- Set default text for the payment plan
			local billingText = _G["BILLING_TEXT"..paymentPlan];
			if ( paymentPlan == 1 ) then
				-- Recurring account
				billingTimeLeft = ceil(billingTimeLeft/(60 * 24));
				if ( billingTimeLeft == 1 ) then
					billingText = BILLING_TIME_LEFT_LAST_DAY;
				end
			elseif ( paymentPlan == 2 ) then
				-- Free account
				if ( billingTimeLeft < (24 * 60) ) then
					billingText = format(BILLING_FREE_TIME_EXPIRE, billingTimeLeft.." "..MINUTES_ABBR);
				end				
			elseif ( paymentPlan == 3 ) then
				-- Fixed but not recurring
				if ( isGameRoom == 1 ) then
					if ( billingTimeLeft <= 30 ) then
						billingText = BILLING_GAMEROOM_EXPIRE;
					else
						billingText = format(BILLING_FIXED_IGR, MinutesToTime(billingTimeLeft, 1));
					end
				else
					-- personal fixed plan
					if ( billingTimeLeft < (24 * 60) ) then
						billingText = BILLING_FIXED_LASTDAY;
					else
						billingText = format(billingText, MinutesToTime(billingTimeLeft));
					end	
				end
			elseif ( paymentPlan == 4 ) then
				-- Usage plan
				if ( isGameRoom == 1 ) then
					-- game room usage plan
					if ( billingTimeLeft <= 600 ) then
						billingText = BILLING_GAMEROOM_EXPIRE;
					else
						billingText = BILLING_IGR_USAGE;
					end
				else
					-- personal usage plan
					if ( billingTimeLeft <= 30 ) then
						billingText = BILLING_TIME_LEFT_30_MINS;
					else
						billingText = format(billingText, billingTimeLeft);
					end
				end
			end
			-- If fallback payment method add a note that says so
			if ( hasFallBackBillingMethod == 1 ) then
				billingText = billingText.."\n\n"..BILLING_HAS_FALLBACK_PAYMENT;
			end
			GameRoomBillingFrameText:SetText(billingText);
			GameRoomBillingFrame:SetHeight(GameRoomBillingFrameText:GetHeight() + 26);
			GameRoomBillingFrame:Show();
			CharacterSelectRealmSplitButton:ClearAllPoints();
			CharacterSelectRealmSplitButton:SetPoint("TOP", GameRoomBillingFrame, "BOTTOM", 0, -10);
		end
	end
	
	if( IsTrialAccount() ) then
		CharacterSelectUpgradeAccountButton:Show();
	else
		CharacterSelectUpgradeAccountButton:Hide();
	end

	-- fadein the character select ui
	GlueFrameFadeIn(CharacterSelectUI, CHARACTER_SELECT_FADE_IN)

	RealmSplitCurrentChoice:Hide();
	RequestRealmSplitInfo();

	--Clear out the addons selected item
	GlueDropDownMenu_SetSelectedValue(AddonCharacterDropDown, ALL);
end

function CharacterSelect_OnHide()
	CharacterDeleteDialog:Hide();
	CharacterRenameDialog:Hide();
	if ( DeclensionFrame ) then
		DeclensionFrame:Hide();
	end
	SERVER_SPLIT_STATE_PENDING = -1;
end

function CharacterSelect_OnUpdate(elapsed)
	if ( SERVER_SPLIT_STATE_PENDING > 0 ) then
		CharacterSelectRealmSplitButton:Show();

		if ( SERVER_SPLIT_CLIENT_STATE > 0 ) then
			RealmSplit_SetChoiceText();
			RealmSplitPending:SetPoint("TOP", RealmSplitCurrentChoice, "BOTTOM", 0, -10);
		else
			RealmSplitPending:SetPoint("TOP", CharacterSelectRealmSplitButton, "BOTTOM", 0, 0);
			RealmSplitCurrentChoice:Hide();
		end

		if ( SERVER_SPLIT_STATE_PENDING > 1 ) then
			CharacterSelectRealmSplitButton:Disable();
			CharacterSelectRealmSplitButtonGlow:Hide();
			RealmSplitPending:SetText( SERVER_SPLIT_PENDING );
		else
			CharacterSelectRealmSplitButton:Enable();
			CharacterSelectRealmSplitButtonGlow:Show();
			local datetext = SERVER_SPLIT_CHOOSE_BY.."\n"..SERVER_SPLIT_DATE;
			RealmSplitPending:SetText( datetext );
		end

		if ( SERVER_SPLIT_SHOW_DIALOG and not GlueDialog:IsShown() ) then
			SERVER_SPLIT_SHOW_DIALOG = false;
			local dialogString = format(SERVER_SPLIT,SERVER_SPLIT_DATE);
			if ( SERVER_SPLIT_CLIENT_STATE > 0 ) then
				local serverChoice = RealmSplit_GetFormatedChoice(SERVER_SPLIT_REALM_CHOICE);
				local stringWithDate = format(SERVER_SPLIT,SERVER_SPLIT_DATE);
				dialogString = stringWithDate.."\n\n"..serverChoice;
				GlueDialog_Show("SERVER_SPLIT_WITH_CHOICE", dialogString);
			else
				GlueDialog_Show("SERVER_SPLIT", dialogString);
			end
		end
	else
		CharacterSelectRealmSplitButton:Hide();
	end

	-- Account Msg stuff
	if ( (ACCOUNT_MSG_NUM_AVAILABLE > 0) and not GlueDialog:IsShown() ) then
		if ( ACCOUNT_MSG_HEADERS_LOADED ) then
			if ( ACCOUNT_MSG_BODY_LOADED ) then
				local dialogString = AccountMsg_GetHeaderSubject( ACCOUNT_MSG_CURRENT_INDEX ).."\n\n"..AccountMsg_GetBody();
				GlueDialog_Show("ACCOUNT_MSG", dialogString);
			end
		end
	end
end

function CharacterSelect_OnKeyDown(self,key)
	if ( key == "ESCAPE" ) then
		CharacterSelect_Exit();
	elseif ( key == "ENTER" ) then
		CharacterSelect_EnterWorld();
	elseif ( key == "PRINTSCREEN" ) then
		Screenshot();
	elseif ( key == "UP" or key == "LEFT" ) then
		local numChars = GetNumCharacters();
		if ( numChars > 1 ) then
			if ( self.selectedIndex > 1 ) then
				CharacterSelect_SelectCharacter(self.selectedIndex - 1);
			else
				CharacterSelect_SelectCharacter(numChars);
			end
		end
	elseif ( arg1 == "DOWN" or arg1 == "RIGHT" ) then
		local numChars = GetNumCharacters();
		if ( numChars > 1 ) then
			if ( self.selectedIndex < GetNumCharacters() ) then
				CharacterSelect_SelectCharacter(self.selectedIndex + 1);
			else
				CharacterSelect_SelectCharacter(1);
			end
		end
	end
end

function CharacterSelect_OnEvent(self, event, ...)
	if ( event == "ADDON_LIST_UPDATE" ) then
		UpdateAddonButton();
	elseif ( event == "CHARACTER_LIST_UPDATE" ) then
		UpdateCharacterList();
		CharSelectCharacterName:SetText(GetCharacterInfo(self.selectedIndex));
	elseif ( event == "UPDATE_SELECTED_CHARACTER" ) then
		local index = ...;
		if ( index == 0 ) then
			CharSelectCharacterName:SetText("");
		else
			CharSelectCharacterName:SetText(GetCharacterInfo(index));
			self.selectedIndex = index;
		end
		UpdateCharacterSelection(self);
	elseif ( event == "SELECT_LAST_CHARACTER" ) then
		self.selectLast = 1;
	elseif ( event == "SELECT_FIRST_CHARACTER" ) then
		CharacterSelect_SelectCharacter(1, 1);
	elseif ( event == "SUGGEST_REALM" ) then
		local category, id = ...;
		local name = GetRealmInfo(category, id);
		if ( name ) then
			SetGlueScreen("charselect");
			ChangeRealm(category, id);
		else
			if ( RealmList:IsShown() ) then
				RealmListUpdate();
			else
				RealmList:Show();
			end
		end
	elseif ( event == "FORCE_RENAME_CHARACTER" ) then
		local message = ...;
		CharacterRenameDialog:Show();
		CharacterRenameText1:SetText(_G[message]);
	end
end

function CharacterSelect_UpdateModel(self)
	UpdateSelectionCustomizationScene();
	self:AdvanceTime();
end

function UpdateCharacterSelection(self)
	for i=1, MAX_CHARACTERS_DISPLAYED, 1 do
		_G["CharSelectCharacterButton"..i]:UnlockHighlight();
	end

	local index = self.selectedIndex;
	if ( (index > 0) and (index <= MAX_CHARACTERS_DISPLAYED) )then
		_G["CharSelectCharacterButton"..index]:LockHighlight();
	end
end

function UpdateCharacterList()
	local numChars = GetNumCharacters();
	local index = 1;
	local coords;
	for i=1, numChars, 1 do
		local name, race, class, level, zone, sex, ghost, PCC, PRC, PFC = GetCharacterInfo(i);
		local button = _G["CharSelectCharacterButton"..index];
		if ( not name ) then
			button:SetText("ERROR - Tell Jeremy");
		else
			if ( not zone ) then
				zone = "";
			end
			_G["CharSelectCharacterButton"..index.."ButtonTextName"]:SetText(name);
			if( ghost ) then
				_G["CharSelectCharacterButton"..index.."ButtonTextInfo"]:SetFormattedText(CHARACTER_SELECT_INFO_GHOST, level, class);
			else
				_G["CharSelectCharacterButton"..index.."ButtonTextInfo"]:SetFormattedText(CHARACTER_SELECT_INFO, level, class);
			end
			_G["CharSelectCharacterButton"..index.."ButtonTextLocation"]:SetText(zone);
		end
		button:Show();

		-- setup paid service buttons
		_G["CharSelectCharacterCustomize"..index]:Hide();
		_G["CharSelectRaceChange"..index]:Hide();
		_G["CharSelectFactionChange"..index]:Hide();
		if ( PFC ) then
			_G["CharSelectFactionChange"..index]:Show();
		elseif ( PRC ) then
			_G["CharSelectRaceChange"..index]:Show();
		elseif ( PCC ) then
			_G["CharSelectCharacterCustomize"..index]:Show();
		end

		index = index + 1;
		if ( index > MAX_CHARACTERS_DISPLAYED ) then
			break;
		end
	end

	if ( numChars == 0 ) then
		CharacterSelectDeleteButton:Disable();
		CharSelectEnterWorldButton:Disable();
	else
		CharacterSelectDeleteButton:Enable();
		CharSelectEnterWorldButton:Enable();
	end

	CharacterSelect.createIndex = 0;
	CharSelectCreateCharacterButton:Hide();	
	
	local connected = IsConnectedToServer();
	for i=index, MAX_CHARACTERS_DISPLAYED, 1 do
		local button = _G["CharSelectCharacterButton"..index];
		if ( (CharacterSelect.createIndex == 0) and (numChars < MAX_CHARACTERS_PER_REALM) ) then
			CharacterSelect.createIndex = index;
			if ( connected ) then
				--If can create characters position and show the create button
				CharSelectCreateCharacterButton:SetID(index);
				--CharSelectCreateCharacterButton:SetPoint("TOP", button, "TOP", 0, -5);
				CharSelectCreateCharacterButton:Show();	
			end
		end
		_G["CharSelectCharacterCustomize"..index]:Hide();
		_G["CharSelectFactionChange"..index]:Hide();
		_G["CharSelectRaceChange"..index]:Hide();
		button:Hide();
		index = index + 1;
	end

	if ( numChars == 0 ) then
		CharacterSelect.selectedIndex = 0;
		CharacterSelect_SelectCharacter(CharacterSelect.selectedIndex, 1);
		return;
	end

	if ( CharacterSelect.selectLast == 1 ) then
		CharacterSelect.selectLast = 0;
		CharacterSelect_SelectCharacter(numChars, 1);
		return;
	end

	if ( (CharacterSelect.selectedIndex == 0) or (CharacterSelect.selectedIndex > numChars) ) then
		CharacterSelect.selectedIndex = 1;
	end
	CharacterSelect_SelectCharacter(CharacterSelect.selectedIndex, 1);
end

function CharacterSelectButton_OnClick(self)
	local id = self:GetID();
	if ( id ~= CharacterSelect.selectedIndex ) then
		CharacterSelect_SelectCharacter(id);
	end
end

function CharacterSelectButton_OnDoubleClick(self)
	local id = self:GetID();
	if ( id ~= CharacterSelect.selectedIndex ) then
		CharacterSelect_SelectCharacter(id);
	end
	CharacterSelect_EnterWorld();
end

function CharacterSelect_TabResize(self)
	local buttonMiddle = _G[self:GetName().."Middle"];
	local buttonMiddleDisabled = _G[self:GetName().."MiddleDisabled"];
	local width = self:GetTextWidth() - 8;
	local leftWidth = _G[self:GetName().."Left"]:GetWidth();
	buttonMiddle:SetWidth(width);
	buttonMiddleDisabled:SetWidth(width);
	self:SetWidth(width + (2 * leftWidth));
end

function CharacterSelect_SelectCharacter(id, noCreate)
	if ( id == CharacterSelect.createIndex ) then
		if ( not noCreate ) then
			PlaySound("gsCharacterSelectionCreateNew");
			SetGlueScreen("charcreate");
		end
	else
		CharacterSelect.currentModel = GetSelectBackgroundModel(id);
		SetBackgroundModel(CharacterSelect,CharacterSelect.currentModel);

		SelectCharacter(id);
	end
end

function CharacterDeleteDialog_OnShow()
	local name, race, class, level = GetCharacterInfo(CharacterSelect.selectedIndex);
	CharacterDeleteText1:SetFormattedText(CONFIRM_CHAR_DELETE, name, level, class);
	CharacterDeleteBackground:SetHeight(16 + CharacterDeleteText1:GetHeight() + CharacterDeleteText2:GetHeight() + 23 + CharacterDeleteEditBox:GetHeight() + 8 + CharacterDeleteButton1:GetHeight() + 16);
	CharacterDeleteButton1:Disable();
end

function CharacterSelect_EnterWorld()
	PlaySound("gsCharacterSelectionEnterWorld");
	StopGlueAmbience();
	EnterWorld();
end

function CharacterSelect_Exit()
	PlaySound("gsCharacterSelectionExit");
	DisconnectFromServer();
	SetGlueScreen("login");
end

function CharacterSelect_AccountOptions()
	PlaySound("gsCharacterSelectionAcctOptions");
end

function CharacterSelect_TechSupport()
	PlaySound("gsCharacterSelectionAcctOptions");
	LaunchURL(TECH_SUPPORT_URL);
end

function CharacterSelect_Delete()
	PlaySound("gsCharacterSelectionDelCharacter");
	if ( CharacterSelect.selectedIndex > 0 ) then
		CharacterDeleteDialog:Show();
	end
end

function CharacterSelect_ChangeRealm()
	PlaySound("gsCharacterSelectionDelCharacter");
	RequestRealmList(1);
end

function CharacterSelectFrame_OnMouseDown(button)
	if ( button == "LeftButton" ) then
		CHARACTER_SELECT_ROTATION_START_X = GetCursorPosition();
		CHARACTER_SELECT_INITIAL_FACING = GetCharacterSelectFacing();
	end
end

function CharacterSelectFrame_OnMouseUp(button)
	if ( button == "LeftButton" ) then
		CHARACTER_SELECT_ROTATION_START_X = nil
	end
end

function CharacterSelectFrame_OnUpdate()
	if ( CHARACTER_SELECT_ROTATION_START_X ) then
		local x = GetCursorPosition();
		local diff = (x - CHARACTER_SELECT_ROTATION_START_X) * CHARACTER_ROTATION_CONSTANT;
		CHARACTER_SELECT_ROTATION_START_X = GetCursorPosition();
		SetCharacterSelectFacing(GetCharacterSelectFacing() + diff);
	end
end

function CharacterSelectRotateRight_OnUpdate(self)
	if ( self:GetButtonState() == "PUSHED" ) then
		SetCharacterSelectFacing(GetCharacterSelectFacing() + CHARACTER_FACING_INCREMENT);
	end
end

function CharacterSelectRotateLeft_OnUpdate(self)
	if ( self:GetButtonState() == "PUSHED" ) then
		SetCharacterSelectFacing(GetCharacterSelectFacing() - CHARACTER_FACING_INCREMENT);
	end
end

function CharacterSelect_ManageAccount()
	PlaySound("gsCharacterSelectionAcctOptions");
	LaunchURL(AUTH_NO_TIME_URL);
end

function RealmSplit_GetFormatedChoice(formatText)
	if ( SERVER_SPLIT_CLIENT_STATE == 1 ) then
		realmChoice = SERVER_SPLIT_SERVER_ONE;
	else
		realmChoice = SERVER_SPLIT_SERVER_TWO;
	end
	return format(formatText, realmChoice);
end

function RealmSplit_SetChoiceText()
	RealmSplitCurrentChoice:SetText( RealmSplit_GetFormatedChoice(SERVER_SPLIT_CURRENT_CHOICE) );
	RealmSplitCurrentChoice:Show();
end

function CharacterSelect_PaidServiceOnClick(self, button, down, service)
	PAID_SERVICE_CHARACTER_ID = self:GetID();
	PAID_SERVICE_TYPE = service;
	PlaySound("gsCharacterSelectionCreateNew");
	SetGlueScreen("charcreate");
end

function CharacterSelect_DeathKnightSwap(self)
	if ( CharacterSelect.currentModel == "DEATHKNIGHT" ) then
		if (self.currentModel ~= "DEATHKNIGHT") then
			self.currentModel = "DEATHKNIGHT";
			self:SetNormalTexture("Interface\\Glues\\Common\\Glue-Panel-Button-Up-Blue");
			self:SetPushedTexture("Interface\\Glues\\Common\\Glue-Panel-Button-Down-Blue");
			self:SetHighlightTexture("Interface\\Glues\\Common\\Glue-Panel-Button-Highlight-Blue");
		end
	else
		if (self.currentModel == "DEATHKNIGHT") then
			self.currentModel = nil;
			self:SetNormalTexture("Interface\\Glues\\Common\\Glue-Panel-Button-Up");
			self:SetPushedTexture("Interface\\Glues\\Common\\Glue-Panel-Button-Down");
			self:SetHighlightTexture("Interface\\Glues\\Common\\Glue-Panel-Button-Highlight");
		end
	end
end

-- CENTURION: up to 20 characters with a scroll bar, World/Tournament badges and a
-- player-arranged order on the Centurion realms. Other realms keep the stock
-- behaviour: 10 characters, no badges, no reordering.
--
-- The client keeps its characters in the order the server sent them; every
-- index below (selectedIndex, button IDs, GetCharacterInfo) is that client index.
-- CharacterSelectCenturion.order maps display position -> client index, so a
-- move shows at once, and the server (which saves the order) sends the same
-- order the next time the list is fetched.
CENTURION_CHARACTERS_PER_REALM = 20;
-- The server sends a tournament character's zone as this area (AreaTable 4658):
-- the character list has no other field the glue screens can read.
CENTURION_TOURNAMENT_LIST_ZONE = "Argent Tournament Grounds";
CENTURION_BADGE_TOURNAMENT = "Tournament";
-- A world character's name on the list (the old "World" badge's blue).
CENTURION_WORLD_NAME_COLOR = { r = 0.45, g = 0.75, b = 1.0 };
-- GlueFontNormal's own gold, which the name FontString starts out in.
CENTURION_NAME_COLOR = { r = 1.0, g = 0.82, b = 0.0 };
CENTURION_MOVE_UP = "Move up\n(Shift+Up)";
CENTURION_MOVE_DOWN = "Move down\n(Shift+Down)";

CharacterSelectCenturion = { order = {}, offset = 0 };

local CENTURION_SELECT_REALMS = { ["centurion"] = true, ["centuriondev"] = true };

function CharacterSelect_IsCenturionRealm()
	if ( not GetServerName ) then
		return false;
	end
	local serverName = GetServerName();
	return type(serverName) == "string" and CENTURION_SELECT_REALMS[string.lower(serverName)] == true;
end

function CharacterSelect_MaxCharacters()
	if ( CharacterSelect_IsCenturionRealm() ) then
		return CENTURION_CHARACTERS_PER_REALM;
	end
	return MAX_CHARACTERS_PER_REALM;
end

function CharacterSelect_CanReorder()
	return CharacterSelect_IsCenturionRealm() and CenturionGlueRequest ~= nil and #CharacterSelectCenturion.order > 1;
end

-- Rows in use: the create button sits where the tenth row would be.
function CharacterSelect_VisibleRows()
	if ( CharSelectCreateCharacterButton:IsShown() ) then
		return MAX_CHARACTERS_DISPLAYED - 1;
	end
	return MAX_CHARACTERS_DISPLAYED;
end

function CharacterSelect_DisplayPosition(index)
	for position, clientIndex in ipairs(CharacterSelectCenturion.order) do
		if ( clientIndex == index ) then
			return position;
		end
	end
	return nil;
end

function UpdateCharacterList()
	local numChars = GetNumCharacters();
	local state = CharacterSelectCenturion;
	state.order = {};
	for i = 1, numChars do
		state.order[i] = i;
	end

	if ( numChars == 0 ) then
		CharacterSelectDeleteButton:Disable();
		CharSelectEnterWorldButton:Disable();
	else
		CharacterSelectDeleteButton:Enable();
		CharSelectEnterWorldButton:Enable();
	end

	CharacterSelect.createIndex = 0;
	CharSelectCreateCharacterButton:Hide();
	if ( numChars < CharacterSelect_MaxCharacters() ) then
		-- Past every character index, so selecting a character never means "create".
		CharacterSelect.createIndex = numChars + 1;
		if ( IsConnectedToServer() ) then
			CharSelectCreateCharacterButton:SetID(CharacterSelect.createIndex);
			CharSelectCreateCharacterButton:Show();
		end
	end

	CharacterSelect_UpdateRows();

	if ( numChars == 0 ) then
		CharacterSelect.selectedIndex = 0;
		CharacterSelect_SelectCharacter(CharacterSelect.selectedIndex, 1);
		return;
	end

	if ( CharacterSelect.selectLast == 1 ) then
		CharacterSelect.selectLast = 0;
		CharacterSelect_SelectCharacter(numChars, 1);
		return;
	end

	if ( (CharacterSelect.selectedIndex == 0) or (CharacterSelect.selectedIndex > numChars) ) then
		CharacterSelect.selectedIndex = 1;
	end
	CharacterSelect_SelectCharacter(CharacterSelect.selectedIndex, 1);
end

function CharacterSelect_UpdateRows()
	local state = CharacterSelectCenturion;
	-- The client's list can change under the order table (a refresh in progress,
	-- which can report a selection before CHARACTER_LIST_UPDATE arrives): start
	-- again from the server's order rather than point at characters that are gone.
	if ( #state.order ~= GetNumCharacters() ) then
		state.order = {};
		for i = 1, GetNumCharacters() do
			state.order[i] = i;
		end
	end
	local numChars = #state.order;
	local rows = CharacterSelect_VisibleRows();
	local maxOffset = math.max(0, numChars - rows);
	state.offset = math.max(0, math.min(state.offset, maxOffset));
	local centurion = CharacterSelect_IsCenturionRealm();

	for row = 1, MAX_CHARACTERS_DISPLAYED do
		local button = _G["CharSelectCharacterButton"..row];
		local customize = _G["CharSelectCharacterCustomize"..row];
		local raceChange = _G["CharSelectRaceChange"..row];
		local factionChange = _G["CharSelectFactionChange"..row];
		local mode = _G["CharSelectCharacterButton"..row.."ButtonTextMode"];
		customize:Hide();
		raceChange:Hide();
		factionChange:Hide();

		local index = nil;
		if ( row <= rows ) then
			index = state.order[state.offset + row];
		end

		local name, race, class, level, zone, sex, ghost, PCC, PRC, PFC;
		if ( index ) then
			name, race, class, level, zone, sex, ghost, PCC, PRC, PFC = GetCharacterInfo(index);
		end

		-- No data (mid-refresh) draws nothing. The stock screen wrote
		-- "ERROR - Tell Jeremy" into the button's own text here, which then stayed
		-- under the real name once the data arrived.
		if ( index and name ) then
			button:SetText("");
			button:SetID(index);
			customize:SetID(index);
			raceChange:SetID(index);
			factionChange:SetID(index);
			mode:Hide();

			local tournament = centurion and zone == CENTURION_TOURNAMENT_LIST_ZONE;
			if ( tournament or not zone ) then
				-- The marker below takes a tournament character's zone line.
				zone = "";
			end
			local nameText = _G["CharSelectCharacterButton"..row.."ButtonTextName"];
			nameText:SetText(name);
			-- Rows are reused, so every row sets its colour: blue for a world
			-- character on the Centurion realms, the stock gold otherwise.
			if ( centurion and not tournament ) then
				nameText:SetTextColor(CENTURION_WORLD_NAME_COLOR.r, CENTURION_WORLD_NAME_COLOR.g, CENTURION_WORLD_NAME_COLOR.b);
			else
				nameText:SetTextColor(CENTURION_NAME_COLOR.r, CENTURION_NAME_COLOR.g, CENTURION_NAME_COLOR.b);
			end
			if ( ghost ) then
				_G["CharSelectCharacterButton"..row.."ButtonTextInfo"]:SetFormattedText(CHARACTER_SELECT_INFO_GHOST, level, class);
			else
				_G["CharSelectCharacterButton"..row.."ButtonTextInfo"]:SetFormattedText(CHARACTER_SELECT_INFO, level, class);
			end
			_G["CharSelectCharacterButton"..row.."ButtonTextLocation"]:SetText(zone);
			if ( tournament ) then
				mode:SetText(CENTURION_BADGE_TOURNAMENT);
				mode:SetTextColor(1.0, 0.55, 0.2);
				mode:Show();
			end
			button:Show();

			if ( PFC ) then
				factionChange:Show();
			elseif ( PRC ) then
				raceChange:Show();
			elseif ( PCC ) then
				customize:Show();
			end
		else
			button:Hide();
		end
	end

	local bar = CharSelectCharacterScrollBar;
	if ( numChars > rows ) then
		state.settingScroll = true;
		bar:SetMinMaxValues(0, maxOffset);
		bar:SetValueStep(1);
		bar:SetValue(state.offset);
		state.settingScroll = nil;
		if ( state.offset > 0 ) then
			CharSelectCharacterScrollBarScrollUpButton:Enable();
		else
			CharSelectCharacterScrollBarScrollUpButton:Disable();
		end
		if ( state.offset < maxOffset ) then
			CharSelectCharacterScrollBarScrollDownButton:Enable();
		else
			CharSelectCharacterScrollBarScrollDownButton:Disable();
		end
		bar:Show();
	else
		bar:Hide();
	end

	CharacterSelect_HighlightRows();
end

-- Highlights the selected character's row if it is on screen. Never scrolls: the
-- list redraws after every scroll, and scrolling here would snap the view back.
function CharacterSelect_HighlightRows()
	local selectedRow = nil;
	for row = 1, MAX_CHARACTERS_DISPLAYED do
		local button = _G["CharSelectCharacterButton"..row];
		button:UnlockHighlight();
		if ( button:IsShown() and CharacterSelect.selectedIndex > 0 and button:GetID() == CharacterSelect.selectedIndex ) then
			button:LockHighlight();
			selectedRow = button;
		end
	end
	CharacterSelect_UpdateMoveButtons(selectedRow);
end

-- Brings a character's row into view. Only for a new selection or a move.
function CharacterSelect_ScrollToIndex(index)
	local position = CharacterSelect_DisplayPosition(index);
	if ( not position ) then
		return;
	end
	local state = CharacterSelectCenturion;
	local rows = CharacterSelect_VisibleRows();
	if ( position <= state.offset ) then
		state.offset = position - 1;
	elseif ( position > state.offset + rows ) then
		state.offset = position - rows;
	end
end

-- The stock screen calls this whenever the selected character changes
-- (UPDATE_SELECTED_CHARACTER): clicks, the keyboard, a new character.
function UpdateCharacterSelection(self)
	CharacterSelect_ScrollToIndex(self.selectedIndex);
	CharacterSelect_UpdateRows();
end

function CharacterSelect_UpdateMoveButtons(selectedRow)
	if ( not selectedRow or not CharacterSelect_CanReorder() ) then
		CharSelectMoveUpButton:Hide();
		CharSelectMoveDownButton:Hide();
		return;
	end

	local position = CharacterSelect_DisplayPosition(selectedRow:GetID()) or 0;
	CharSelectMoveUpButton:ClearAllPoints();
	-- Right of the info lines (180 wide), left of the scroll bar, under the badge.
	CharSelectMoveUpButton:SetPoint("TOPRIGHT", selectedRow, "TOPRIGHT", -52, -21);
	CharSelectMoveDownButton:ClearAllPoints();
	CharSelectMoveDownButton:SetPoint("TOP", CharSelectMoveUpButton, "BOTTOM", 0, 2);
	if ( position > 1 ) then
		CharSelectMoveUpButton:Enable();
	else
		CharSelectMoveUpButton:Disable();
	end
	if ( position < #CharacterSelectCenturion.order ) then
		CharSelectMoveDownButton:Enable();
	else
		CharSelectMoveDownButton:Disable();
	end
	CharSelectMoveUpButton:Show();
	CharSelectMoveDownButton:Show();
end

function CharacterSelect_MoveSelected(delta)
	if ( not CharacterSelect_CanReorder() ) then
		return;
	end

	local order = CharacterSelectCenturion.order;
	local position = CharacterSelect_DisplayPosition(CharacterSelect.selectedIndex);
	local target = position and position + delta;
	if ( not target or target < 1 or target > #order ) then
		return;
	end

	order[position], order[target] = order[target], order[position];
	local names = {};
	for i, index in ipairs(order) do
		local name = GetCharacterInfo(index);
		if ( not name ) then
			order[position], order[target] = order[target], order[position];
			return;
		end
		names[i] = name;
	end

	CenturionGlueRequest("ORDER\t"..table.concat(names, ","));
	PlaySound("igMainMenuOptionCheckBoxOn");
	CharacterSelect_ScrollToIndex(CharacterSelect.selectedIndex);
	CharacterSelect_UpdateRows();
end

function CharacterSelect_ScrollBy(delta)
	CharacterSelectCenturion.offset = CharacterSelectCenturion.offset + delta;
	CharacterSelect_UpdateRows();
end

function CharacterSelect_OnMouseWheel(delta)
	if ( CharSelectCharacterScrollBar:IsShown() ) then
		CharacterSelect_ScrollBy(-delta);
	end
end

function CharacterSelect_OnScrollValueChanged(self, value)
	if ( CharacterSelectCenturion.settingScroll ) then
		return;
	end
	local offset = math.floor(value + 0.5);
	if ( offset ~= CharacterSelectCenturion.offset ) then
		CharacterSelectCenturion.offset = offset;
		CharacterSelect_UpdateRows();
	end
end

function CharacterSelect_OnKeyDown(self, key)
	if ( key == "ESCAPE" ) then
		CharacterSelect_Exit();
	elseif ( key == "ENTER" ) then
		CharacterSelect_EnterWorld();
	elseif ( key == "PRINTSCREEN" ) then
		Screenshot();
	elseif ( key == "UP" or key == "LEFT" or key == "DOWN" or key == "RIGHT" ) then
		local delta = 1;
		if ( key == "UP" or key == "LEFT" ) then
			delta = -1;
		end
		if ( IsShiftKeyDown() and (key == "UP" or key == "DOWN") and CharacterSelect_CanReorder() ) then
			CharacterSelect_MoveSelected(delta);
			return;
		end
		local order = CharacterSelectCenturion.order;
		local count = #order;
		if ( count > 1 ) then
			local position = (CharacterSelect_DisplayPosition(self.selectedIndex) or 1) + delta;
			if ( position < 1 ) then
				position = count;
			elseif ( position > count ) then
				position = 1;
			end
			CharacterSelect_SelectCharacter(order[position]);
		end
	end
end

local _origCharacterSelect_OnLoad_Centurion = CharacterSelect_OnLoad;
function CharacterSelect_OnLoad(self)
	_origCharacterSelect_OnLoad_Centurion(self);
	CharacterSelectCharacterFrame:EnableMouseWheel(true);
	CharacterSelectCharacterFrame:SetScript("OnMouseWheel", function(frame, delta)
		CharacterSelect_OnMouseWheel(delta);
	end);
	for row = 1, MAX_CHARACTERS_DISPLAYED do
		_G["CharSelectCharacterButton"..row]:EnableMouseWheel(true);
	end
	CharSelectCharacterScrollBar:EnableMouseWheel(true);
end

-- The rename prompt takes the whole name, first AND last, and a last name is
-- required (Miscellaneous/Surnames.h on the server). The last name box starts
-- out holding the character's current one, so changing only the first name is
-- still one box's worth of typing. It reaches the server the way the create
-- screen's does - CenturionGlueRequest("SURNAME\t<new name>\t<surname>") just
-- before RenameCharacter - and the server checks the new pair as a whole.
CENTURION_RENAME_SURNAME_REQUIRED = "Your character needs a last name.";
CENTURION_RENAME_SURNAME_INVALID = "That is not a valid last name: letters only, 2 to 12 of them, and no more than two of the same letter in a row.";

-- CharacterRenameEditBox's own anchor: TOP of CharacterRenameText2's BOTTOM, (0, -5).
local CENTURION_RENAME_Y = -5;
-- Lower, to make room for the two labels above the boxes.
local CENTURION_RENAME_Y_PAIRED = -20;
-- Half of (box 130 + gap 28), so the pair straddles the centre.
local CENTURION_RENAME_X_PAIRED = -79;

function CharacterRename_SurnamesEnabled()
	return CenturionGlueRequest ~= nil and CharacterSelect_IsCenturionRealm();
end

function CharacterRename_UpdateLayout()
	if ( not CharacterRenameSurnameEditBox ) then
		return;
	end

	local paired = CharacterRename_SurnamesEnabled();
	CharacterRenameEditBox:ClearAllPoints();
	if ( paired ) then
		CharacterRenameEditBox:SetPoint("TOP", CharacterRenameText2, "BOTTOM", CENTURION_RENAME_X_PAIRED, CENTURION_RENAME_Y_PAIRED);
		CharacterRenameEditBox:SetScript("OnTabPressed", function() CharacterRenameSurnameEditBox:SetFocus(); end);
		-- The list shows "First Last"; offer the Last back.
		local current = GetCharacterInfo(CharacterSelect.selectedIndex);
		local surname = type(current) == "string" and strmatch(current, "^%S+%s+(%S+)") or "";
		CharacterRenameSurnameEditBox:SetText(surname or "");
		CharacterRenameSurnameEditBox:Show();
	else
		CharacterRenameEditBox:SetPoint("TOP", CharacterRenameText2, "BOTTOM", 0, CENTURION_RENAME_Y);
		CharacterRenameEditBox:SetScript("OnTabPressed", nil);
		CharacterRenameSurnameEditBox:Hide();
	end

	-- An earlier refusal may have replaced the instructions.
	CharacterRenameText2:SetText(CHAR_RENAME_INSTRUCTIONS);
	CharacterRenameText2:SetTextColor(1.0, 0.82, 0.0);
	CharacterRenameEditBox:SetFocus();
end

-- The prompt is opened from here and nowhere else.
local _origCharacterSelect_OnEvent_Surname = CharacterSelect_OnEvent;
function CharacterSelect_OnEvent(self, event, ...)
	_origCharacterSelect_OnEvent_Surname(self, event, ...);
	if ( event == "FORCE_RENAME_CHARACTER" ) then
		CharacterRename_UpdateLayout();
	end
end

-- Both the OKAY button and Enter in either box end up here. Returning nothing
-- keeps the prompt open, which is what the stock callers do on a refusal.
local _origRenameCharacter_Surname = RenameCharacter;
function RenameCharacter(index, name, ...)
	if ( CharacterRename_SurnamesEnabled() and CharacterRenameSurnameEditBox and CharacterRenameSurnameEditBox:IsShown() ) then
		local surname = string.gsub(CharacterRenameSurnameEditBox:GetText() or "", "^%s*(.-)%s*$", "%1");
		local problem;
		if ( surname == "" ) then
			problem = CENTURION_RENAME_SURNAME_REQUIRED;
		elseif ( CharacterCreate_SurnameIsValid and not CharacterCreate_SurnameIsValid(surname) ) then
			problem = CENTURION_RENAME_SURNAME_INVALID;
		end
		if ( problem ) then
			-- Said inside the prompt: a glue dialog could open underneath it.
			CharacterRenameText2:SetText(problem);
			CharacterRenameText2:SetTextColor(1.0, 0.3, 0.3);
			CharacterRenameSurnameEditBox:SetFocus();
			return nil;
		end
		CenturionGlueRequest("SURNAME\t"..(name or "").."\t"..surname);
	end
	return _origRenameCharacter_Surname(index, name, ...);
end
-- END CENTURION character list
