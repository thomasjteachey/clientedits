CHARACTER_FACING_INCREMENT = 2;
MAX_RACES = 10;
MAX_CLASSES_PER_RACE = 10;
NUM_CHAR_CUSTOMIZATIONS = 5;
MIN_CHAR_NAME_LENGTH = 2;
CHARACTER_CREATE_ROTATION_START_X = nil;
CHARACTER_CREATE_INITIAL_FACING = nil;

PAID_CHARACTER_CUSTOMIZATION = 1;
PAID_RACE_CHANGE = 2;
PAID_FACTION_CHANGE = 3;
PAID_SERVICE_CHARACTER_ID = nil;
PAID_SERVICE_TYPE = nil;

FACTION_BACKDROP_COLOR_TABLE = {
	["Alliance"] = {0.5, 0.5, 0.5, 0.09, 0.09, 0.19},
	["Horde"] = {0.5, 0.2, 0.2, 0.19, 0.05, 0.05},
};
FRAMES_TO_BACKDROP_COLOR = { 
	"CharacterCreateCharacterRace",
	"CharacterCreateCharacterClass",
--	"CharacterCreateCharacterFaction",
	"CharacterCreateNameEdit",
};
RACE_ICON_TCOORDS = {
	["HUMAN_MALE"]		= {0, 0.125, 0, 0.25},
	["DWARF_MALE"]		= {0.125, 0.25, 0, 0.25},
	["GNOME_MALE"]		= {0.25, 0.375, 0, 0.25},
	["NIGHTELF_MALE"]	= {0.375, 0.5, 0, 0.25},
	
	["TAUREN_MALE"]		= {0, 0.125, 0.25, 0.5},
	["SCOURGE_MALE"]	= {0.125, 0.25, 0.25, 0.5},
	["TROLL_MALE"]		= {0.25, 0.375, 0.25, 0.5},
	["ORC_MALE"]		= {0.375, 0.5, 0.25, 0.5},

	["HUMAN_FEMALE"]	= {0, 0.125, 0.5, 0.75},  
	["DWARF_FEMALE"]	= {0.125, 0.25, 0.5, 0.75},
	["GNOME_FEMALE"]	= {0.25, 0.375, 0.5, 0.75},
	["NIGHTELF_FEMALE"]	= {0.375, 0.5, 0.5, 0.75},
	
	["TAUREN_FEMALE"]	= {0, 0.125, 0.75, 1.0},   
	["SCOURGE_FEMALE"]	= {0.125, 0.25, 0.75, 1.0}, 
	["TROLL_FEMALE"]	= {0.25, 0.375, 0.75, 1.0}, 
	["ORC_FEMALE"]		= {0.375, 0.5, 0.75, 1.0}, 

	["BLOODELF_MALE"]	= {0.5, 0.625, 0.25, 0.5},
	["BLOODELF_FEMALE"]	= {0.5, 0.625, 0.75, 1.0}, 

	["DRAENEI_MALE"]	= {0.5, 0.625, 0, 0.25},
	["DRAENEI_FEMALE"]	= {0.5, 0.625, 0.5, 0.75}, 
};
CLASS_ICON_TCOORDS = {
	["WARRIOR"]	= {0, 0.25, 0, 0.25},
	["MAGE"]	= {0.25, 0.49609375, 0, 0.25},
	["ROGUE"]	= {0.49609375, 0.7421875, 0, 0.25},
	["DRUID"]	= {0.7421875, 0.98828125, 0, 0.25},
	["HUNTER"]	= {0, 0.25, 0.25, 0.5},
	["SHAMAN"]	= {0.25, 0.49609375, 0.25, 0.5},
	["PRIEST"]	= {0.49609375, 0.7421875, 0.25, 0.5},
	["WARLOCK"]	= {0.7421875, 0.98828125, 0.25, 0.5},
	["PALADIN"]	= {0, 0.25, 0.5, 0.75},
	["DEATHKNIGHT"]	= {0.25, 0.49609375, 0.5, 0.75},
};

function CharacterCreate_OnLoad(self)
	self:SetSequence(0);
	self:SetCamera(0);

	CharacterCreate.numRaces = 0;
	CharacterCreate.selectedRace = 0;
	CharacterCreate.numClasses = 0;
	CharacterCreate.selectedClass = 0;
	CharacterCreate.selectedGender = 0;

	SetCharCustomizeFrame("CharacterCreate");

	for i=1, NUM_CHAR_CUSTOMIZATIONS, 1 do
		_G["CharacterCustomizationButtonFrame"..i.."Text"]:SetText(_G["CHAR_CUSTOMIZATION"..i.."_DESC"]);
	end

	-- Color edit box backdrop
	local backdropColor = FACTION_BACKDROP_COLOR_TABLE["Alliance"];
	CharacterCreateNameEdit:SetBackdropBorderColor(backdropColor[1], backdropColor[2], backdropColor[3]);
	CharacterCreateNameEdit:SetBackdropColor(backdropColor[4], backdropColor[5], backdropColor[6]);
end

function CharacterCreate_OnShow()
	for i=1, MAX_CLASSES_PER_RACE, 1 do
		local button = _G["CharacterCreateClassButton"..i];
		button:Enable();
		SetButtonDesaturated(button, false)
	end
	for i=1, MAX_RACES, 1 do
		local button = _G["CharacterCreateRaceButton"..i];
		button:Enable();
		SetButtonDesaturated(button, false)
	end

	if ( PAID_SERVICE_TYPE ) then
		CustomizeExistingCharacter( PAID_SERVICE_CHARACTER_ID );
		CharacterCreateNameEdit:SetText( PaidChange_GetName() );
	else
		--randomly selects a combination
		ResetCharCustomize();
		CharacterCreateNameEdit:SetText("");
		CharCreateRandomizeButton:Show();
	end

	CharacterCreateEnumerateRaces(GetAvailableRaces());
	SetCharacterRace(GetSelectedRace());
	
	CharacterCreateEnumerateClasses(GetAvailableClasses());
	local_,_,index = GetSelectedClass();
	SetCharacterClass(index);

	SetCharacterGender(GetSelectedSex())
	
	-- Hair customization stuff
	CharacterCreate_UpdateHairCustomization();

	SetCharacterCreateFacing(-15);
	
	if ( ALLOW_RANDOM_NAME_BUTTON ) then
		CharacterCreateRandomName:Show();
	end
	
	-- setup customization
	CharacterChangeFixup();
end

function CharacterCreate_OnHide()
	PAID_SERVICE_CHARACTER_ID = nil;
	PAID_SERVICE_TYPE = nil;
end

function CharacterCreateFrame_OnMouseDown(button)
	if ( button == "LeftButton" ) then
		CHARACTER_CREATE_ROTATION_START_X = GetCursorPosition();
		CHARACTER_CREATE_INITIAL_FACING = GetCharacterCreateFacing();
	end
end

function CharacterCreateFrame_OnMouseUp(button)
	if ( button == "LeftButton" ) then
		CHARACTER_CREATE_ROTATION_START_X = nil
	end
end

function CharacterCreateFrame_OnUpdate()
	if ( CHARACTER_CREATE_ROTATION_START_X ) then
		local x = GetCursorPosition();
		local diff = (x - CHARACTER_CREATE_ROTATION_START_X) * CHARACTER_ROTATION_CONSTANT;
		CHARACTER_CREATE_ROTATION_START_X = GetCursorPosition();
		SetCharacterCreateFacing(GetCharacterCreateFacing() + diff);
	end
end

function CharacterCreateEnumerateRaces(...)
	CharacterCreate.numRaces = select("#", ...)/3;
	if ( CharacterCreate.numRaces > MAX_RACES ) then
		message("Too many races!  Update MAX_RACES");
		return;
	end
	local coords;
	local index = 1;
	local button;
	local gender;
	local selectedSex = GetSelectedSex();
	if ( selectedSex == SEX_MALE ) then
		gender = "MALE";
	elseif ( selectedSex == SEX_FEMALE ) then
		gender = "FEMALE";
	end
	for i=1, select("#", ...), 3 do
		coords = RACE_ICON_TCOORDS[strupper(select(i+1, ...).."_"..gender)];
		_G["CharacterCreateRaceButton"..index.."NormalTexture"]:SetTexCoord(coords[1], coords[2], coords[3], coords[4]);
		_G["CharacterCreateRaceButton"..index.."PushedTexture"]:SetTexCoord(coords[1], coords[2], coords[3], coords[4]);
		button = _G["CharacterCreateRaceButton"..index];
		button:Show();
		if ( select(i+2, ...) == 1 ) then
			button.enable = true;
			SetButtonDesaturated(button);
			button.name = select(i, ...)
			button.tooltip = select(i, ...);
		else
			button.enable = false;
			SetButtonDesaturated(button, 1);
			button.name = select(i, ...)
			button.tooltip = _G[strupper(select(i+1, ...).."_".."DISABLED")];
		end
		index = index + 1;
	end
	for i=CharacterCreate.numRaces + 1, MAX_RACES, 1 do
		_G["CharacterCreateRaceButton"..i]:Hide();
	end
end

function CharacterCreateEnumerateClasses(...)
	CharacterCreate.numClasses = select("#", ...)/3;
	if ( CharacterCreate.numClasses > MAX_CLASSES_PER_RACE ) then
		message("Too many classes!  Update MAX_CLASSES_PER_RACE");
		return;
	end
	local coords;
	local index = 1;
	local button;
	for i=1, select("#", ...), 3 do
		coords = CLASS_ICON_TCOORDS[strupper(select(i+1, ...))];
		_G["CharacterCreateClassButton"..index.."NormalTexture"]:SetTexCoord(coords[1], coords[2], coords[3], coords[4]);
		_G["CharacterCreateClassButton"..index.."PushedTexture"]:SetTexCoord(coords[1], coords[2], coords[3], coords[4]);
		button = _G["CharacterCreateClassButton"..index];
		button:Show();
		if ( (select(i+2, ...) == 1) and (IsRaceClassValid(CharacterCreate.selectedRace, index)) ) then
			button.enable = true;
			button:Enable();
			SetButtonDesaturated(button);
			button.name = select(i, ...)
			button.tooltip = select(i, ...);
			_G["CharacterCreateClassButton"..index.."DisableTexture"]:Hide();
		else
			button.enable = false;
			button:Disable();
			SetButtonDesaturated(button, 1);
			button.name = select(i, ...)
			button.tooltip = _G[strupper(select(i+1, ...).."_".."DISABLED")];
			_G["CharacterCreateClassButton"..index.."DisableTexture"]:Show();
		end
		index = index + 1;
	end
	for i=CharacterCreate.numClasses + 1, MAX_CLASSES_PER_RACE, 1 do
		_G["CharacterCreateClassButton"..i]:Hide();
	end
end

function SetCharacterRace(id)
	CharacterCreate.selectedRace = id;
	local selectedButton;
	for i=1, CharacterCreate.numRaces, 1 do
		local button = _G["CharacterCreateRaceButton"..i];
		if ( i == id ) then
			_G["CharacterCreateRaceButton"..i.."Text"]:SetText(button.name);
			button:SetChecked(1);
			selectedButton = button;
		else
			_G["CharacterCreateRaceButton"..i.."Text"]:SetText("");
			button:SetChecked(0);
		end
	end

	-- Set Faction
	local name, faction = GetFactionForRace(CharacterCreate.selectedRace);

	-- Set Race
	local race, fileString = GetNameForRace();

	CharacterCreateRaceLabel:SetText(race);
	fileString = strupper(fileString);
	if ( GetSelectedSex() == SEX_MALE ) then
		gender = "MALE";
	else
		gender = "FEMALE";
	end
	local coords = RACE_ICON_TCOORDS[fileString.."_"..gender];
	CharacterCreateRaceIcon:SetTexCoord(coords[1], coords[2], coords[3], coords[4]);
	local raceText = _G["RACE_INFO_"..fileString];
	local abilityIndex = 1;
	local tempText = _G["ABILITY_INFO_"..fileString..abilityIndex];
	abilityText = "";
	while ( tempText ) do
		abilityText = abilityText..tempText.."\n\n";
		abilityIndex = abilityIndex + 1;
		tempText = _G["ABILITY_INFO_"..fileString..abilityIndex];
	end

	CharacterCreateRaceScrollFrameScrollBar:SetValue(0);
	CharacterCreateRaceText:SetText(GetFlavorText("RACE_INFO_"..strupper(fileString), GetSelectedSex()).."|n|n");
	if ( abilityText and abilityText ~= "" ) then
		CharacterCreateRaceAbilityText:SetText(abilityText);
	else
		CharacterCreateRaceAbilityText:SetText("");
	end

	-- Set backdrop colors based on faction
	local backdropColor = FACTION_BACKDROP_COLOR_TABLE[faction];
	local frame;
	for index, value in pairs(FRAMES_TO_BACKDROP_COLOR) do
		frame = _G[value];
		frame:SetBackdropColor(backdropColor[4], backdropColor[5], backdropColor[6]);
	end
	CharacterCreateConfigurationBackground:SetVertexColor(backdropColor[4], backdropColor[5], backdropColor[6]);

	local backgroundFilename = GetCreateBackgroundModel();
	SetBackgroundModel(CharacterCreate, backgroundFilename);
end

function SetCharacterClass(id)
	CharacterCreate.selectedClass = id;
	for i=1, CharacterCreate.numClasses, 1 do
		local button = _G["CharacterCreateClassButton"..i];
		if ( i == id ) then
			CharacterCreateClassName:SetText(button.name);
			button:SetChecked(1);
		else
			button:SetChecked(0);
		end
	end
	
	local className, classFileName, _, tank, healer, damage = GetSelectedClass();
	local abilityIndex = 0;
	local tempText = _G["CLASS_INFO_"..classFileName..abilityIndex];
	abilityText = "";
	while ( tempText ) do
		abilityText = abilityText..tempText.."\n\n";
		abilityIndex = abilityIndex + 1;
		tempText = _G["CLASS_INFO_"..classFileName..abilityIndex];
	end
	local coords = CLASS_ICON_TCOORDS[classFileName];
	CharacterCreateClassIcon:SetTexCoord(coords[1], coords[2], coords[3], coords[4]);
	CharacterCreateClassLabel:SetText(className);
	CharacterCreateClassRolesText:SetText(abilityText);	
	CharacterCreateClassText:SetText(GetFlavorText("CLASS_"..strupper(classFileName), GetSelectedSex()).."|n|n");
	CharacterCreateClassScrollFrameScrollBar:SetValue(0);
end

function CharacterCreate_OnChar()
end

function CharacterCreate_OnKeyDown(key)
	if ( key == "ESCAPE" ) then
		CharacterCreate_Back();
	elseif ( key == "ENTER" ) then
		CharacterCreate_Okay();
	elseif ( key == "PRINTSCREEN" ) then
		Screenshot();
	end
end

function CharacterCreate_UpdateModel(self)
	UpdateCustomizationScene();
	self:AdvanceTime();
end

function CharacterCreate_Okay()
	if ( PAID_SERVICE_TYPE ) then
		GlueDialog_Show("CONFIRM_PAID_SERVICE");
	else
		CreateCharacter(CharacterCreateNameEdit:GetText());
	end
	PlaySound("gsCharacterCreationCreateChar");
end

function CharacterCreate_Back()
	PlaySound("gsCharacterCreationCancel");
	SetGlueScreen("charselect");
end

function CharacterClass_OnClick(id)
	PlaySound("gsCharacterCreationClass");
	local _,_,currClass = GetSelectedClass();
	if ( currClass ~= id and IsRaceClassValid(GetSelectedRace(), id) ) then
		SetSelectedClass(id);
		SetCharacterClass(id);
	 	SetCharacterRace(GetSelectedRace());
		CharacterChangeFixup();
	end
end

function CharacterRace_OnClick(self, id)
	PlaySound("gsCharacterCreationClass");
	if ( not self:GetChecked() ) then
		self:SetChecked(1);
		return;
	end
	if ( GetSelectedRace() ~= id ) then
		SetSelectedRace(id);
		SetCharacterRace(id);
		SetSelectedSex(GetSelectedSex());
		SetCharacterCreateFacing(-15);
		CharacterCreateEnumerateClasses(GetAvailableClasses());
		local _,_,classIndex = GetSelectedClass();
		if ( PAID_SERVICE_TYPE ) then
			classIndex = PaidChange_GetCurrentClassIndex();
		end
		SetCharacterClass(classIndex);
		
		-- Hair customization stuff
		CharacterCreate_UpdateHairCustomization();
			
		CharacterChangeFixup();
	end
end

function SetCharacterGender(sex)
	local gender;
	SetSelectedSex(sex);
	if ( sex == SEX_MALE ) then
		gender = "MALE";
		CharacterCreateGender:SetText(MALE);
		CharacterCreateGenderButtonMale:SetChecked(1);
		CharacterCreateGenderButtonFemale:SetChecked(nil);
	elseif ( sex == SEX_FEMALE ) then
		gender = "FEMALE";
		CharacterCreateGender:SetText(FEMALE);
		CharacterCreateGenderButtonMale:SetChecked(nil);
		CharacterCreateGenderButtonFemale:SetChecked(1);
	end

	-- Update race images to reflect gender
	CharacterCreateEnumerateRaces(GetAvailableRaces());
	CharacterCreateEnumerateClasses(GetAvailableClasses());
 	SetCharacterRace(GetSelectedRace());
	
	local _,_,classIndex = GetSelectedClass();
	if ( PAID_SERVICE_TYPE ) then
		classIndex = PaidChange_GetCurrentClassIndex();
	end
	SetCharacterClass(classIndex);

	CharacterCreate_UpdateHairCustomization();

	-- Update right hand race portrait to reflect gender change
	-- Set Race
	local race, fileString = GetNameForRace();
	CharacterCreateRaceLabel:SetText(race);
	fileString = strupper(fileString);
	local coords = RACE_ICON_TCOORDS[fileString.."_"..gender];
	CharacterCreateRaceIcon:SetTexCoord(coords[1], coords[2], coords[3], coords[4]);
	
	CharacterChangeFixup();
end

function CharacterCustomization_Left(id)
	PlaySound("gsCharacterCreationLook");
	CycleCharCustomization(id, -1);
end

function CharacterCustomization_Right(id)
	PlaySound("gsCharacterCreationLook");
	CycleCharCustomization(id, 1);
end

function CharacterCreate_Randomize()
	PlaySound("gsCharacterCreationLook");
	RandomizeCharCustomization();
end

function CharacterCreateRotateRight_OnUpdate(self)
	if ( self:GetButtonState() == "PUSHED" ) then
		SetCharacterCreateFacing(GetCharacterCreateFacing() + CHARACTER_FACING_INCREMENT);
	end
end

function CharacterCreateRotateLeft_OnUpdate(self)
	if ( self:GetButtonState() == "PUSHED" ) then
		SetCharacterCreateFacing(GetCharacterCreateFacing() - CHARACTER_FACING_INCREMENT);
	end
end

function CharacterCreate_UpdateHairCustomization()
	CharacterCustomizationButtonFrame3Text:SetText(_G["HAIR_"..GetHairCustomization().."_STYLE"]);
	CharacterCustomizationButtonFrame4Text:SetText(_G["HAIR_"..GetHairCustomization().."_COLOR"]);
	CharacterCustomizationButtonFrame5Text:SetText(_G["FACIAL_HAIR_"..GetFacialHairCustomization()]);		
end

function SetButtonDesaturated(button, desaturated, r, g, b)
	if ( not button ) then
		return;
	end
	local icon = button:GetNormalTexture();
	if ( not icon ) then
		return;
	end
	local shaderSupported = icon:SetDesaturated(desaturated);

	if ( not desaturated ) then
		r = 1.0;
		g = 1.0;
		b = 1.0;
	elseif ( not r or not shaderSupported ) then
		r = 0.5;
		g = 0.5;
		b = 0.5;
	end
	
	icon:SetVertexColor(r, g, b);
end

function GetFlavorText(tagname, sex)
	local primary, secondary;
	if ( sex == SEX_MALE ) then
		primary = "";
		secondary = "_FEMALE";
	else
		primary = "_FEMALE";
		secondary = "";
	end
	local text = _G[tagname..primary];
	if ( (text == nil) or (text == "") ) then
		text = _G[tagname..secondary];
	end
	return text;
end

function CharacterCreate_DeathKnightSwap(self)
	local _, classFilename = GetSelectedClass();
	if ( classFilename == "DEATHKNIGHT" ) then
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

function CharacterChangeFixup()
	if ( PAID_SERVICE_TYPE ) then
		for i=1, MAX_CLASSES_PER_RACE, 1 do
			if (CharacterCreate.selectedClass ~= i) then
				local button = _G["CharacterCreateClassButton"..i];
				button:Disable();
				SetButtonDesaturated(button, true)
			end
		end

		for i=1, MAX_RACES, 1 do
			local allow = false;
			if ( PAID_SERVICE_TYPE == PAID_FACTION_CHANGE ) then
				local faction = GetFactionForRace(PaidChange_GetCurrentRaceIndex());
				if ( (i == PaidChange_GetCurrentRaceIndex()) or ((GetFactionForRace(i) ~= faction) and (IsRaceClassValid(i,CharacterCreate.selectedClass))) ) then
					allow = true;
				end
			elseif ( PAID_SERVICE_TYPE == PAID_RACE_CHANGE ) then
				local faction = GetFactionForRace(PaidChange_GetCurrentRaceIndex());
				if ( (i == PaidChange_GetCurrentRaceIndex()) or ((GetFactionForRace(i) == faction) and (IsRaceClassValid(i,CharacterCreate.selectedClass))) ) then
					allow = true
				end
			elseif ( PAID_SERVICE_TYPE == PAID_CHARACTER_CUSTOMIZATION ) then
				if ( i == CharacterCreate.selectedRace ) then
					allow = true
				end
			end
			if (not allow) then
				local button = _G["CharacterCreateRaceButton"..i];
				button:Disable();
				SetButtonDesaturated(button, true)
			end
		end
	end
end



--[[
================================================================================
 Even 4+4 race columns without moving anchors (keeps 5+5 grid intact)
 - Populates Alliance into buttons 1..4
 - Populates Horde   into buttons 6..9
 - Hides buttons 5 and 10
 - Leaves CharacterCreate.numRaces = MAX_RACES so default loops keep working
 - DOES NOT re-anchor or reposition anything
================================================================================
]]
do
  local function upper(s) return (type(s)=="string") and string.upper(s) or "" end

  local ALLIANCE = { HUMAN=true, DWARF=true, NIGHTELF=true, GNOME=true, DRAENEI=true }
  local HORDE    = { ORC=true, TAUREN=true, TROLL=true, BLOODELF=true, SCOURGE=true, UNDEAD=true }

  local function isAlliance(fs) return ALLIANCE[upper(fs)] == true end
  local function isHorde(fs)    return HORDE[upper(fs)]    == true end

  local function applyTripletToButton(index, name, fileString, enabled, gender)
    local coords = RACE_ICON_TCOORDS[strupper((fileString or "") .. "_" .. gender)]
    _G["CharacterCreateRaceButton"..index.."NormalTexture"]:SetTexCoord(coords[1], coords[2], coords[3], coords[4])
    _G["CharacterCreateRaceButton"..index.."PushedTexture"]:SetTexCoord(coords[1], coords[2], coords[3], coords[4])
    local button = _G["CharacterCreateRaceButton"..index]
    button:Show()
    if ( enabled == 1 ) then
      button.enable = true
      SetButtonDesaturated(button)
      button.name = name
      button.tooltip = name
    else
      button.enable = false
      SetButtonDesaturated(button, 1)
      button.name = name
      button.tooltip = _G[strupper((fileString or "") .. "_" .. "DISABLED")]
    end
  end

  local _origEnum = CharacterCreateEnumerateRaces
  function CharacterCreateEnumerateRaces(...)
    -- Partition incoming triplets into alliance/horde, while skipping BE/Draenei
    local al, ho = {}, {}
    for i = 1, select('#', ...), 3 do
      local name, fileString, enabled = select(i, ...), select(i+1, ...), select(i+2, ...)
      local fsu = upper(fileString)
      if fsu ~= "BLOODELF" and fsu ~= "DRAENEI" then
        if isAlliance(fsu) then
          table.insert(al, name); table.insert(al, fileString); table.insert(al, enabled)
        elseif isHorde(fsu) then
          table.insert(ho, name); table.insert(ho, fileString); table.insert(ho, enabled)
        else
          -- Unknown grouping -> treat as alliance by default to avoid holes
          table.insert(al, name); table.insert(al, fileString); table.insert(al, enabled)
        end
      end
    end

    -- Always tell glue there are 10 slots; we will explicitly hide the unused two.
    CharacterCreate.numRaces = MAX_RACES or 10

    local gender = (GetSelectedSex() == SEX_MALE) and "MALE" or "FEMALE"

    -- Fill Alliance 1..4
    local idx = 1
    for i = 1, #al, 3 do
      if idx > 4 then break end
      applyTripletToButton(idx, al[i], al[i+1], al[i+2], gender)
      idx = idx + 1
    end
    -- Hide leftover alliance slots (if any)
    for i = idx, 4 do
      local b = _G["CharacterCreateRaceButton"..i]; if b then b:Hide() end
    end

    -- Hide slot 5 (the missing fifth alliance race)
    local b5 = _G["CharacterCreateRaceButton5"]; if b5 then b5:Hide() end

    -- Fill Horde 6..9
    idx = 6
    for i = 1, #ho, 3 do
      if idx > 9 then break end
      applyTripletToButton(idx, ho[i], ho[i+1], ho[i+2], gender)
      idx = idx + 1
    end
    -- Hide leftover horde slots 6..9 not filled
    for i = idx, 9 do
      local b = _G["CharacterCreateRaceButton"..i]; if b then b:Hide() end
    end

    -- Hide slot 10 (the missing fifth horde race)
    local b10 = _G["CharacterCreateRaceButton10"]; if b10 then b10:Hide() end
  end
end
-- End 4+4 population patch



-- BEGIN GPT PATCH v7
do
  local function upper(s) return (type(s)=="string") and string.upper(s) or "" end
  local ALLIANCE = { HUMAN=true, DWARF=true, NIGHTELF=true, GNOME=true, DRAENEI=true }
  local HORDE    = { ORC=true, TAUREN=true, TROLL=true, BLOODELF=true, SCOURGE=true, UNDEAD=true }

  -- Race mapping state
  local BTN_TO_RACE, RACE_TO_BTN = {}, {}
  local RACEIDX_TO_FILESTRING = {}
  local _randomizedAwayFromBElfOrDraenei = false

  local function applyRaceToButton(index, name, fileString, enabled, gender, realIndex)
    local key = string.upper((fileString or "") .. "_" .. gender)
    local coords = RACE_ICON_TCOORDS[key] or {0,1,0,1}
    local button = _G["CharacterCreateRaceButton"..index]; if not button then return end
    button:Show()
    button.raceIndex = realIndex
    BTN_TO_RACE[index] = realIndex
    RACE_TO_BTN[realIndex] = index
    RACEIDX_TO_FILESTRING[realIndex] = string.upper(fileString or "")

    _G[button:GetName().."NormalTexture"]:SetTexCoord(coords[1], coords[2], coords[3], coords[4])
    _G[button:GetName().."PushedTexture"]:SetTexCoord(coords[1], coords[2], coords[3], coords[4])

    if ( enabled == 1 ) then
      button.enable = true
      SetButtonDesaturated(button)
      button.name = name; button.tooltip = name
    else
      button.enable = false
      SetButtonDesaturated(button, 1)
      button.name = name; button.tooltip = name
    end
  end

  local _origEnumRaces = CharacterCreateEnumerateRaces
  function CharacterCreateEnumerateRaces(...)
    BTN_TO_RACE, RACE_TO_BTN, RACEIDX_TO_FILESTRING = {}, {}, {}

    local al, ho = {}, {}
    for i = 1, select('#', ...), 3 do
      local name, fs, en = select(i, ...), select(i+1, ...), select(i+2, ...)
      local realIndex = (i + 2) / 3
      local fsu = upper(fs)
      if fsu ~= "BLOODELF" and fsu ~= "DRAENEI" then
        local bucket = (ALLIANCE[fsu] and al) or (HORDE[fsu] and ho) or al
        table.insert(bucket, {name=name, fs=fs, en=en, idx=realIndex})
      end
      RACEIDX_TO_FILESTRING[realIndex] = fsu
    end

    CharacterCreate.numRaces = MAX_RACES or 10
    local gender = (GetSelectedSex() == SEX_MALE) and "MALE" or "FEMALE"

    -- Alliance 1..4
    local idx = 1
    for k=1, #al do
      if idx > 4 then break end
      local r = al[k]; applyRaceToButton(idx, r.name, r.fs, r.en, gender, r.idx); idx = idx + 1
    end
    for i=idx,4 do local b=_G["CharacterCreateRaceButton"..i]; if b then b:Hide(); b.raceIndex=nil end end

    -- Hide 5
    local b5=_G["CharacterCreateRaceButton5"]; if b5 then b5:Hide(); b5.raceIndex=nil end

    -- Horde 6..9
    idx = 6
    for k=1, #ho do
      if idx > 9 then break end
      local r = ho[k]; applyRaceToButton(idx, r.name, r.fs, r.en, gender, r.idx); idx = idx + 1
    end
    for i=idx,9 do local b=_G["CharacterCreateRaceButton"..i]; if b then b:Hide(); b.raceIndex=nil end end

    -- Hide 10
    local b10=_G["CharacterCreateRaceButton10"]; if b10 then b10:Hide(); b10.raceIndex=nil end

    -- One-time randomize away from BE/DRAENEI
    if not _randomizedAwayFromBElfOrDraenei then
      local current = GetSelectedRace()
      local fs = RACEIDX_TO_FILESTRING[current]
      if fs == "BLOODELF" or fs == "DRAENEI" then
        local cand, seen = {}, {}
        for _,real in pairs(BTN_TO_RACE) do if real and not seen[real] then table.insert(cand, real); seen[real]=true end end
        if #cand>0 then
          local pick = cand[math.random(1, #cand)]
          if pick and pick ~= current then SetSelectedRace(pick); SetCharacterRace(pick) end
        end
        _randomizedAwayFromBElfOrDraenei = true
      end
    end
  end

  local _origRaceClick = CharacterRace_OnClick
  function CharacterRace_OnClick(self, id)
    if type(self) == "number" and id == nil then id = self; self = nil end
    PlaySound("gsCharacterCreationClass")
    if self and self.GetChecked and not self:GetChecked() then self:SetChecked(1); return end
    local real = (self and self.raceIndex) or (BTN_TO_RACE and BTN_TO_RACE[id]) or id
    if GetSelectedRace() ~= real then
      SetSelectedRace(real); SetCharacterRace(real); SetSelectedSex(GetSelectedSex())
      SetCharacterCreateFacing(-15)
      CharacterCreateEnumerateClasses(GetAvailableClasses())
      local _,_,classIndex = GetSelectedClass()
      if PAID_SERVICE_TYPE then classIndex = PaidChange_GetCurrentClassIndex() end
      SetCharacterClass(classIndex)
      CharacterCreate_UpdateHairCustomization(); CharacterChangeFixup()
    end
  end

  local _origSetRace = SetCharacterRace
  function SetCharacterRace(realId)
    if _origSetRace then pcall(_origSetRace, realId) end
    local selectedBtnIndex = RACE_TO_BTN[realId] or realId
    for i=1, CharacterCreate.numRaces or 10 do
      local btn = _G["CharacterCreateRaceButton"..i]
      if btn then
        local isMatch = (i == selectedBtnIndex) or (btn.raceIndex == realId)
        local text = _G[btn:GetName().."Text"]
        if text then text:SetText(isMatch and (btn.name or text:GetText()) or "") end
        btn:SetChecked(isMatch and 1 or 0)
      end
    end
  end

  -- Wrap the original class enumeration to hide Death Knight after Blizzard sets states/desaturation
  local _origEnumClasses = CharacterCreateEnumerateClasses
  function CharacterCreateEnumerateClasses(...)
    _origEnumClasses(...)
    local dkIndex
    for i = 1, select('#', ...), 3 do
      local fs = select(i+1, ...)
      if string.upper(fs or "") == "DEATHKNIGHT" then
        dkIndex = (i + 2) / 3
        break
      end
    end
    if dkIndex then
      local btn = _G["CharacterCreateClassButton"..dkIndex]
      if btn then btn:Hide() end
    end
  end
end
-- END GPT PATCH v7

-- BEGIN GPT PATCH v8 - hard-block BE/Draenei as initial/selected races
-- This is intentionally placed AFTER the older race remap patches so it wins.
do
  local BLOCKED_RACE = { BLOODELF = true, DRAENEI = true }

  local function upper(s)
    return (type(s) == "string") and string.upper(s) or ""
  end

  local function getAvailableRaceRows()
    local ok, races = pcall(function() return { GetAvailableRaces() } end)
    if not ok or not races then
      return {}
    end

    local rows = {}
    for i = 1, #races, 3 do
      table.insert(rows, {
        idx = (i + 2) / 3,
        name = races[i],
        fs = upper(races[i + 1]),
        enabled = races[i + 2],
      })
    end
    return rows
  end

  local function getRaceFileString(raceIndex)
    for _, r in ipairs(getAvailableRaceRows()) do
      if r.idx == raceIndex then
        return r.fs
      end
    end
    return ""
  end

  local function getSelectedClassIndexSafe()
    local ok, _, _, classIndex = pcall(GetSelectedClass)
    if ok then
      return classIndex
    end
    return nil
  end

  local function findReplacementClassicRace()
    local selectedClass = getSelectedClassIndexSafe()
    local validForCurrentClass = {}
    local firstEnabledClassicRace = nil

    for _, r in ipairs(getAvailableRaceRows()) do
      if r.enabled == 1 and not BLOCKED_RACE[r.fs] then
        if not firstEnabledClassicRace then
          firstEnabledClassicRace = r.idx
        end
        if selectedClass and IsRaceClassValid(r.idx, selectedClass) then
          table.insert(validForCurrentClass, r.idx)
        end
      end
    end

    if #validForCurrentClass > 0 then
      return validForCurrentClass[math.random(1, #validForCurrentClass)]
    end

    return firstEnabledClassicRace
  end

  local function getSafeClassicRace(raceIndex)
    raceIndex = raceIndex or GetSelectedRace()
    local fs = getRaceFileString(raceIndex)
    if not BLOCKED_RACE[fs] then
      return raceIndex, false
    end

    local replacement = findReplacementClassicRace()
    if replacement and replacement ~= raceIndex then
      return replacement, true
    end

    return raceIndex, false
  end

  local function forceClassicRaceSelection()
    local safeRace, changed = getSafeClassicRace(GetSelectedRace())
    if not changed then
      return false
    end

    SetSelectedRace(safeRace)

    local classIndex = getSelectedClassIndexSafe()
    if classIndex and not IsRaceClassValid(safeRace, classIndex) then
      local classes = { GetAvailableClasses() }
      for i = 1, #classes, 3 do
        local candidateClass = (i + 2) / 3
        local enabled = classes[i + 2]
        if enabled == 1 and IsRaceClassValid(safeRace, candidateClass) then
          SetSelectedClass(candidateClass)
          break
        end
      end
    end

    CharacterCreateEnumerateRaces(GetAvailableRaces())
    CharacterCreateEnumerateClasses(GetAvailableClasses())
    SetCharacterRace(safeRace)

    local _, _, finalClassIndex = GetSelectedClass()
    if finalClassIndex then
      SetCharacterClass(finalClassIndex)
    end

    SetSelectedSex(GetSelectedSex())
    CharacterCreate_UpdateHairCustomization()
    CharacterChangeFixup()
    return true
  end

  local _origSetCharacterRace_ClassicOnly = SetCharacterRace
  function SetCharacterRace(raceIndex)
    if not PAID_SERVICE_TYPE then
      local safeRace, changed = getSafeClassicRace(raceIndex)
      if changed and safeRace then
        SetSelectedRace(safeRace)
        raceIndex = safeRace
      end
    end
    return _origSetCharacterRace_ClassicOnly(raceIndex)
  end

  local _origCharacterCreate_OnShow_ClassicOnly = CharacterCreate_OnShow
  function CharacterCreate_OnShow(...)
    _origCharacterCreate_OnShow_ClassicOnly(...)
    if not PAID_SERVICE_TYPE then
      forceClassicRaceSelection()
    end
  end

  local _origCharacterCreate_Randomize_ClassicOnly = CharacterCreate_Randomize
  function CharacterCreate_Randomize(...)
    _origCharacterCreate_Randomize_ClassicOnly(...)
    if not PAID_SERVICE_TYPE then
      forceClassicRaceSelection()
    end
  end

  local _origSetCharacterGender_ClassicOnly = SetCharacterGender
  function SetCharacterGender(sex)
    _origSetCharacterGender_ClassicOnly(sex)
    if not PAID_SERVICE_TYPE then
      forceClassicRaceSelection()
    end
  end
end
-- END GPT PATCH v8
