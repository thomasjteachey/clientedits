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

		-- CENTURION: the race buttons are laid out 4+4 by the remap patch below,
		-- so button i is not race i; each button carries the race it shows as
		-- button.raceIndex. Judging button i as race i enabled the wrong icons
		-- (a Tauren shaman was offered Dwarf and not Troll).
		for i=1, MAX_RACES, 1 do
			local button = _G["CharacterCreateRaceButton"..i];
			local race = button.raceIndex or i;
			local allow = false;
			if ( PAID_SERVICE_TYPE == PAID_FACTION_CHANGE ) then
				local faction = GetFactionForRace(PaidChange_GetCurrentRaceIndex());
				if ( (race == PaidChange_GetCurrentRaceIndex()) or ((GetFactionForRace(race) ~= faction) and (IsRaceClassValid(race,CharacterCreate.selectedClass))) ) then
					allow = true;
				end
			elseif ( PAID_SERVICE_TYPE == PAID_RACE_CHANGE ) then
				local faction = GetFactionForRace(PaidChange_GetCurrentRaceIndex());
				if ( (race == PaidChange_GetCurrentRaceIndex()) or ((GetFactionForRace(race) == faction) and (IsRaceClassValid(race,CharacterCreate.selectedClass))) ) then
					allow = true
				end
			elseif ( PAID_SERVICE_TYPE == PAID_CHARACTER_CUSTOMIZATION ) then
				if ( race == CharacterCreate.selectedRace ) then
					allow = true
				end
			end
			if (not allow) then
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

-- CENTURION: world or tournament character, and challenge modes, on the Centurion
-- realms only.
--
-- The server reads a tournament character from the case of the name it is sent:
-- first letter lowercase, the rest uppercase ("eLGROM"). It decodes that before
-- names are normalized, so the character is still called "Elgrom"
-- (Miscellaneous/TournamentMode.h on the server).
--
-- Challenge modes cannot ride the create request, so they go first through the
-- client-tweaks DLL: CenturionGlueRequest("CREATE\t<name>\t<mask>"), where bit n
-- is the server's ChallengeModeSettings n. The server applies them when the
-- character is created, with the same checks as the challenge stones
-- (Miscellaneous/CharacterScreen.h).
CENTURION_TOURNAMENT_CHARACTER_TOOLTIP = "Tournament character\n\nStarts at level 60 on the tournament grounds with a full kit, and queues for battlegrounds and arenas with other tournament characters.\n\nTournament characters stay on the tournament grounds. They cannot trade or mail world characters, or use the auction house, dungeons or flight masters, and they cannot take challenges.\n\nLeave unchecked for a world character.";

-- Keyed by ChallengeModeSettings value (the checkbox ID).
CENTURION_CHALLENGE_TOOLTIPS = {
	[0] = "Hardcore\n\nOne life. Dying finishes the character: you are disconnected at once, and every later login kills and disconnects you again. There is no way back.\n\nCannot be combined with Semi-Hardcore.",
	[1] = "Semi-Hardcore\n\nDeath costs everything you are wearing. However you die, every equipped item except your shirt and tabard is deleted and your gold is set to zero. Nothing is left behind to loot. Your bags are left alone, and the character survives.\n\nCannot be combined with Hardcore.",
	[2] = "Self Crafted\n\nYou may only equip items you crafted yourself. Anything looted, bought or given to you cannot be worn.\n\nCannot be combined with Iron Man.",
	[3] = "Poor/Normal Gear Only\n\nYou may only equip grey and white items. Nothing green or better, however it was obtained.",
	[4] = "Slow XP\n\nYou earn half of the normal experience from everything.\n\nCannot be combined with Very Slow XP.",
	[5] = "Very Slow XP\n\nYou earn a quarter of the normal experience from everything.\n\nCannot be combined with Slow XP.",
	[6] = "Quest XP Only\n\nKills give you no experience at all; only quests do. A pet with you still earns its own share.",
	[7] = "Iron Man\n\nAll of these together: no talent points ever, no resurrection, grey and white gear only, no potions, elixirs, flasks or food that heals over time, no enchantments, and no professions (Runeforging, Poisons and Beast Training are allowed).\n\nCannot be combined with Self Crafted.",
};

CENTURION_CHALLENGES = {
	{ bit = 0, partner = 1 },
	{ bit = 1, partner = 0 },
	{ bit = 2, partner = 7 },
	{ bit = 7, partner = 2 },
	{ bit = 3, partner = nil },
	{ bit = 4, partner = 5 },
	{ bit = 5, partner = 4 },
	{ bit = 6, partner = nil },
};

local CENTURION_TOURNAMENT_REALMS = { ["centurion"] = true, ["centuriondev"] = true };

function CharacterCreate_IsTournamentRealm()
	if ( not GetServerName ) then
		return false;
	end
	local serverName = GetServerName();
	return type(serverName) == "string" and CENTURION_TOURNAMENT_REALMS[string.lower(serverName)] == true;
end

function CharacterCreate_ShowCenturionTooltip(owner, text)
	if ( not text ) then
		return;
	end
	GlueTooltip_SetOwner(owner, CharacterCreateCenturionTooltip, -3, -5);
	GlueTooltip_SetText(text, CharacterCreateCenturionTooltip);
end

local function CharacterCreate_IsTournamentChecked()
	return CharacterCreateTournamentMode and CharacterCreateTournamentMode:IsShown() and CharacterCreateTournamentMode:GetChecked();
end

-- A challenge is locked while its exclusive partner is ticked. The tournament box
-- and the challenges never lock each other: ticking one side unticks the other
-- (CharacterCreateTournamentMode_OnClick, CharacterCreateChallenge_OnClick).
function CharacterCreate_UpdateCenturionToggles()
	for _, info in ipairs(CENTURION_CHALLENGES) do
		local button = _G["CharacterCreateChallenge"..info.bit];
		local text = _G["CharacterCreateChallenge"..info.bit.."Text"];
		local unlocked = true;
		if ( info.partner ) then
			local partner = _G["CharacterCreateChallenge"..info.partner];
			if ( partner:GetChecked() ) then
				unlocked = false;
			end
		end
		if ( unlocked ) then
			button:Enable();
			text:SetTextColor(1.0, 0.82, 0.0);
		else
			button:Disable();
			text:SetTextColor(0.5, 0.5, 0.5);
		end
	end
end

function CharacterCreate_ChallengeMask()
	if ( CharacterCreate_IsTournamentChecked() or not CharacterCreateChallengesFrame:IsShown() ) then
		return 0;
	end
	local mask = 0;
	for _, info in ipairs(CENTURION_CHALLENGES) do
		local button = _G["CharacterCreateChallenge"..info.bit];
		if ( button:GetChecked() and button:IsEnabled() ) then
			mask = mask + 2 ^ info.bit;
		end
	end
	return mask;
end

-- A tournament character takes no challenges: ticking it clears them all.
function CharacterCreateTournamentMode_OnClick(self)
	if ( self:GetChecked() ) then
		PlaySound("igMainMenuOptionCheckBoxOn");
		for _, info in ipairs(CENTURION_CHALLENGES) do
			_G["CharacterCreateChallenge"..info.bit]:SetChecked(nil);
		end
	else
		PlaySound("igMainMenuOptionCheckBoxOff");
	end
	CharacterCreate_UpdateCenturionToggles();
end

-- ...and ticking a challenge makes it a world character again.
function CharacterCreateChallenge_OnClick(self)
	if ( self:GetChecked() ) then
		PlaySound("igMainMenuOptionCheckBoxOn");
		if ( CharacterCreateTournamentMode ) then
			CharacterCreateTournamentMode:SetChecked(nil);
		end
	else
		PlaySound("igMainMenuOptionCheckBoxOff");
	end
	CharacterCreate_UpdateCenturionToggles();
end

local function CharacterCreate_TournamentName(name)
	if ( type(name) ~= "string" or string.len(name) < 2 ) then
		return name;
	end
	return string.lower(string.sub(name, 1, 1))..string.upper(string.sub(name, 2));
end

local _origCharacterCreate_Okay_Tournament = CharacterCreate_Okay;
function CharacterCreate_Okay(...)
	if ( not PAID_SERVICE_TYPE and CharacterCreateTournamentMode and CharacterCreateTournamentMode:IsShown() ) then
		local name = CharacterCreateNameEdit:GetText();
		-- Always sent when the panel is up, so an earlier attempt's choice is replaced
		-- (a tournament character or no ticks sends 0, which clears it).
		if ( CenturionGlueRequest and CharacterCreateChallengesFrame:IsShown() ) then
			CenturionGlueRequest("CREATE\t"..name.."\t"..CharacterCreate_ChallengeMask());
		end
		if ( CharacterCreateTournamentMode:GetChecked() ) then
			CreateCharacter(CharacterCreate_TournamentName(name));
			PlaySound("gsCharacterCreationCreateChar");
			return;
		end
	end
	return _origCharacterCreate_Okay_Tournament(...);
end

local _origCharacterCreate_OnShow_Tournament = CharacterCreate_OnShow;
function CharacterCreate_OnShow(...)
	_origCharacterCreate_OnShow_Tournament(...);
	if ( not CharacterCreateTournamentMode ) then
		return;
	end

	CharacterCreateTournamentMode:SetChecked(nil);
	for _, info in ipairs(CENTURION_CHALLENGES) do
		_G["CharacterCreateChallenge"..info.bit]:SetChecked(nil);
	end

	if ( not PAID_SERVICE_TYPE and CharacterCreate_IsTournamentRealm() ) then
		CharacterCreateTournamentMode:Show();
		-- Without the client-tweaks DLL there is no way to deliver the choice.
		if ( CenturionGlueRequest ) then
			CharacterCreateChallengesFrame:Show();
		else
			CharacterCreateChallengesFrame:Hide();
		end
	else
		CharacterCreateTournamentMode:Hide();
		CharacterCreateChallengesFrame:Hide();
	end
	CharacterCreate_UpdateCenturionToggles();
end
-- END CENTURION character create

-- CENTURION: family names (Miscellaneous/Surnames.h on the server).
--
-- The create request has no field for a second name either, so it travels the
-- way the challenge modes do - through the client-tweaks DLL, as
-- CenturionGlueRequest("SURNAME\t<name>\t<surname>") sent just before
-- CreateCharacter. The server holds it against that name and writes it to the
-- character once it exists. Without the DLL there is nothing to send it with,
-- so the box stays hidden.
--
-- The name box is the client's own and is centred on the screen; with a second
-- box beside it the PAIR is centred instead, and the tournament checkbox moves
-- along to the right of the new box (the challenges panel hangs off the
-- checkbox and follows on its own). A realm without surnames - or a paid
-- customize/rename, which creates nothing - gets the stock layout back, so the
-- screen is untouched anywhere this is not wanted.
CENTURION_SURNAME_MIN = 2;
CENTURION_SURNAME_MAX = 12;
CENTURION_SURNAME_INVALID = "That is not a valid last name.\n\nLetters only, between %d and %d of them, and no more than two of the same letter in a row.";
CENTURION_SURNAME_REQUIRED = "Your character needs a last name.";

-- CharacterCreateNameEdit's own anchor is BOTTOM (0, 55) of CharacterCreateFrame.
local CENTURION_NAME_EDIT_Y = 55;
local CENTURION_NAME_EDIT_X_ALONE = 0;
-- Half of (box width 156 + gap 8), so the two boxes straddle the centre.
local CENTURION_NAME_EDIT_X_PAIRED = -82;

-- The paid-change screens (customize, race change) get the pair too: they send
-- the first name in the packet and the family name ahead of it, and the server
-- keeps the one on file if none arrives.
function CharacterCreate_SurnamesEnabled()
	return CenturionGlueRequest ~= nil and CharacterCreate_IsTournamentRealm();
end

function CharacterCreate_UpdateSurnameLayout()
	if ( not CharacterCreateSurnameEdit ) then
		return;
	end

	local paired = CharacterCreate_SurnamesEnabled();

	-- The stock label is the NAME global, which is "Name" on its own and wants
	-- to say which name it means once there are two boxes.
	if ( CharacterCreateNameEditLabel ) then
		CharacterCreateNameEditLabel:SetText(paired and "First Name" or (NAME or "Name"));
	end

	CharacterCreateNameEdit:ClearAllPoints();
	if ( paired ) then
		CharacterCreateNameEdit:SetPoint("BOTTOM", CharacterCreateFrame, "BOTTOM", CENTURION_NAME_EDIT_X_PAIRED, CENTURION_NAME_EDIT_Y);
		CharacterCreateNameEdit:SetScript("OnTabPressed", function() CharacterCreateSurnameEdit:SetFocus(); end);
		-- The border colour is set once, for the name box, in CharacterCreate_OnLoad;
		-- only the backdrop follows the faction (FRAMES_TO_BACKDROP_COLOR).
		local backdropColor = FACTION_BACKDROP_COLOR_TABLE["Alliance"];
		CharacterCreateSurnameEdit:SetBackdropBorderColor(backdropColor[1], backdropColor[2], backdropColor[3]);
		CharacterCreateSurnameEdit:Show();
	else
		CharacterCreateNameEdit:SetPoint("BOTTOM", CharacterCreateFrame, "BOTTOM", CENTURION_NAME_EDIT_X_ALONE, CENTURION_NAME_EDIT_Y);
		CharacterCreateNameEdit:SetScript("OnTabPressed", nil);
		CharacterCreateSurnameEdit:Hide();
	end

	if ( CharacterCreateTournamentMode ) then
		CharacterCreateTournamentMode:ClearAllPoints();
		if ( paired ) then
			CharacterCreateTournamentMode:SetPoint("LEFT", CharacterCreateSurnameEdit, "RIGHT", -2, -2);
		else
			CharacterCreateTournamentMode:SetPoint("LEFT", CharacterCreateNameEdit, "RIGHT", -2, -2);
		end
	end
end

-- The server's own name rules, as far as they can be checked here: it takes
-- letters in one alphabet, 2 to 12 of them, and refuses three of the same
-- letter in a row. Bytes above 127 are left to the server to judge.
function CharacterCreate_SurnameIsValid(surname)
	local length = string.len(surname);
	if ( length < CENTURION_SURNAME_MIN or length > CENTURION_SURNAME_MAX ) then
		return false;
	end
	if ( string.find(surname, "[%s%p%d%c]") ) then
		return false;
	end

	local lower = string.lower(surname);
	for i = 3, length do
		local c = string.sub(lower, i, i);
		if ( c == string.sub(lower, i - 1, i - 1) and c == string.sub(lower, i - 2, i - 2) ) then
			return false;
		end
	end
	return true;
end

local _origCharacterCreate_Okay_Surname = CharacterCreate_Okay;
function CharacterCreate_Okay(...)
	if ( CharacterCreate_SurnamesEnabled() and CharacterCreateSurnameEdit ) then
		-- Required: the server refuses to create a character without one, so say
		-- so here, where the player can still do something about it.
		local surname = string.gsub(CharacterCreateSurnameEdit:GetText() or "", "^%s*(.-)%s*$", "%1");
		local problem;
		if ( surname == "" ) then
			problem = CENTURION_SURNAME_REQUIRED;
		elseif ( not CharacterCreate_SurnameIsValid(surname) ) then
			problem = format(CENTURION_SURNAME_INVALID, CENTURION_SURNAME_MIN, CENTURION_SURNAME_MAX);
		end
		if ( problem ) then
			if ( GlueDialog_Show ) then
				GlueDialog_Show("OKAY", problem);
			end
			CharacterCreateSurnameEdit:SetFocus();
			return;
		end
		-- Always sent while the box is up, so an earlier attempt's surname is
		-- replaced rather than left waiting.
		CenturionGlueRequest("SURNAME\t"..CharacterCreateNameEdit:GetText().."\t"..surname);
	end
	return _origCharacterCreate_Okay_Surname(...);
end

local _origCharacterCreate_OnShow_Surname = CharacterCreate_OnShow;
function CharacterCreate_OnShow(...)
	_origCharacterCreate_OnShow_Surname(...);
	if ( CharacterCreateSurnameEdit ) then
		CharacterCreateSurnameEdit:SetText("");
		-- A paid change starts from the name the list shows, "First Last":
		-- one half in each box.
		if ( PAID_SERVICE_TYPE and CharacterCreate_SurnamesEnabled() ) then
			local full = PaidChange_GetName() or "";
			local first, last = string.match(full, "^(%S+)%s+(%S+)");
			if ( first ) then
				CharacterCreateNameEdit:SetText(first);
				CharacterCreateSurnameEdit:SetText(last);
			end
		end
		CharacterCreate_UpdateSurnameLayout();
	end
end

-- Faction colouring, the same as the name box gets (SetCharacterRace). Only the
-- name is added here; the frame itself is looked up when that runs, by which
-- time the XML has been read.
table.insert(FRAMES_TO_BACKDROP_COLOR, "CharacterCreateSurnameEdit");

-- The Randomize button under the last name box: a family name that fits the
-- race on screen, from the same lists the server's naming tool uses
-- (tools/surnames/surnames.py). Keyed by GetNameForRace()'s file name.
CENTURION_SURNAME_POOLS = {
	["HUMAN"] = { "Ashcroft", "Ashford", "Barlow", "Bellamy", "Blackwood", "Brightmore", "Carrow", "Caulfield", "Cranwell", "Darrow", "Deveraux", "Dunwich", "Eastvale", "Fairbanks", "Fenwick", "Galloway", "Garrick", "Greyson", "Halstead", "Hartwell", "Havelock", "Holloway", "Kingsley", "Lockhart", "Marsden", "Merrick", "Norwood", "Oakhurst", "Pemberton", "Quinley", "Radcliffe", "Ravenholt", "Redpath", "Rothwell", "Sandover", "Shawcross", "Stanwick", "Stoneleigh", "Thornbury", "Vanbrook", "Wexford", "Whitlock", "Winslow", "Wycliffe" },
	["ORC"] = { "Axebreaker", "Blackfang", "Bladefury", "Bloodaxe", "Bloodfist", "Bloodhowl", "Bonecrusher", "Darkmaul", "Deathfury", "Doomaxe", "Dreadfist", "Emberfist", "Feltusk", "Fellblade", "Gorefang", "Grimaxe", "Grimjaw", "Grimtusk", "Ironfist", "Ironjaw", "Ironmaul", "Rageblade", "Ragefist", "Redtusk", "Ruinhowl", "Savagehide", "Scarfang", "Screamblade", "Skullcleave", "Steelfang", "Stonefury", "Stormfang", "Thunderaxe", "Warfang", "Warhowl", "Wolfrider", "Wrathblade" },
	["DWARF"] = { "Anvilfist", "Anvilmar", "Barrelbelt", "Battlebrow", "Brewmantle", "Cinderforge", "Coalbraid", "Coldanvil", "Copperkeg", "Deepdelve", "Deepforge", "Direhammer", "Emberforge", "Flintbrow", "Forgewright", "Frostbeard", "Gemcutter", "Goldbraid", "Granitefist", "Grimbolt", "Hammerfall", "Hammerhold", "Ironbrow", "Ironkeg", "Ironshale", "Kegsmasher", "Longbeard", "Orebender", "Quarrystone", "Sootbeard", "Steelgrip", "Stonefist", "Stonehelm", "Stormbrew", "Thunderbrew", "Wildhammer", "Winterforge" },
	["NIGHTELF"] = { "Ashenbough", "Bladeleaf", "Briarwind", "Dawnstrider", "Dewbright", "Duskwhisper", "Elunesong", "Feathermoon", "Fernbloom", "Frostglade", "Gladewalker", "Glaivewind", "Larkwing", "Leafwhisper", "Mistwood", "Moonbreeze", "Mooncaller", "Moonfall", "Moonshadow", "Nightbloom", "Nightbreeze", "Nightglade", "Nightsong", "Oakenmoon", "Owlsong", "Ravenwing", "Sablewind", "Shadecrest", "Silverbough", "Silverleaf", "Starbreeze", "Starcaller", "Starweaver", "Swiftwind", "Thistlebrook", "Thornbloom", "Wildbloom", "Willowbark", "Wolfsong" },
	["SCOURGE"] = { "Ashbone", "Ashenmourn", "Bittergrave", "Blackmire", "Bonecrypt", "Cinderbone", "Coldgrave", "Coldheart", "Corpsewood", "Darkmourn", "Deadmarsh", "Deathbough", "Dirgewood", "Dreadmourn", "Duskbane", "Gallowsworn", "Gloomvale", "Gravemire", "Graveswood", "Grimward", "Hollowbone", "Hollowgrave", "Marrowbane", "Mortlake", "Mournfall", "Nightgrave", "Pallidmoor", "Plaguewood", "Quietgrave", "Ravenmourn", "Rotheart", "Rotwood", "Sablecrypt", "Shadegrave", "Shroudveil", "Silentgrave", "Sorrowmoor", "Stitchbone", "Tombwood", "Wormwood" },
	["TAUREN"] = { "Bloodhoof", "Cloudmane", "Dawnhorn", "Deeproot", "Earthhoof", "Elderhoof", "Greatmane", "Grimtotem", "Heavyhoof", "Highmountain", "Longhorn", "Mistrunner", "Mossrunner", "Oakenhoof", "Plainstrider", "Proudhorn", "Rainchaser", "Ragetotem", "Redhoof", "Riverhorn", "Runetotem", "Sagehorn", "Silverhorn", "Skychaser", "Skyhorn", "Skyseer", "Snowhoof", "Spiritmane", "Stargrazer", "Stonehoof", "Stormhoof", "Stouthoof", "Sunwalker", "Swiftmane", "Tallgrass", "Thunderhoof", "Thunderhorn", "Whitehorn", "Wildhorn", "Wildmane", "Windmane", "Windtotem", "Wisemane" },
	["GNOME"] = { "Bellowspark", "Boltwhistle", "Brasscog", "Coilspring", "Cogspark", "Copperbolt", "Cranktooth", "Dialspin", "Emberfuse", "Fizzlebang", "Fizzlespark", "Fusebox", "Gadgetspring", "Gearloose", "Gearspanner", "Gizmospark", "Gyrowrench", "Hexnut", "Ironspanner", "Knobtwist", "Nimblefinger", "Nutbolt", "Overcrank", "Pipewrench", "Quickfuse", "Ratchetcog", "Rivetspark", "Sparkfizzle", "Sprocketgear", "Steamvalve", "Tinkerpop", "Tinkerspan", "Twistbolt", "Voltcap", "Whirlygig", "Zapwhistle" },
	["TROLL"] = { "Amani", "Bloodscalp", "Bogstalker", "Bonecharm", "Bonerattle", "Darkspear", "Deathmask", "Drakkari", "Dreadmask", "Fetishclaw", "Frostmane", "Grimfeather", "Gurubashi", "Hakkari", "Headhunter", "Hexcaller", "Hexfang", "Jujuhand", "Lashtail", "Marshwalker", "Mojoheart", "Mossflayer", "Ragefang", "Sandfury", "Serpentcoil", "Shadowpine", "Spiritmask", "Swampfang", "Vilebranch", "Voodoofang", "Wildmask", "Witherbark" },
	["BLOODELF"] = { "Bloodwrath", "Crimsonveil", "Dawnblade", "Dawnfire", "Dawnstar", "Duskblade", "Duskwither", "Emberfall", "Emberlight", "Eversong", "Felbane", "Firesong", "Goldenbough", "Manaveil", "Morningsong", "Phoenixfall", "Runeveil", "Sablewing", "Solarblade", "Sunbinder", "Sunfury", "Sunmender", "Sunsorrow", "Sunward", "Thornblade", "Truefire" },
	["DRAENEI"] = { "Aetherwing", "Beaconlight", "Brightsoul", "Crystalsong", "Crystalvein", "Dawnbinder", "Dawnhammer", "Dawnspire", "Dawnward", "Everlight", "Faithbound", "Farseeker", "Glimmerward", "Holyward", "Hopebringer", "Keeplight", "Lightbearer", "Lightbinder", "Lightsworn", "Lightward", "Lucidsong", "Lumenveil", "Mercyhand", "Naarusworn", "Nobleheart", "Oathlight", "Prismwing", "Purelight", "Quietmind", "Radiantforge", "Runewatch", "Sablevault", "Sanctumward", "Shieldlight", "Soulmender", "Starforge", "Sunmantle", "Templeward", "Truelight", "Voidbane", "Warpstrider", "Wayfinder", "Wisdomlight" },
};

function CharacterCreate_RandomSurname()
	if ( not CharacterCreateSurnameEdit ) then
		return;
	end
	local _, fileString = GetNameForRace();
	local pool = CENTURION_SURNAME_POOLS[strupper(fileString or "")];
	if ( not pool or #pool == 0 ) then
		return;
	end
	local pick = pool[math.random(#pool)];
	-- A second roll rather than showing the same name twice in a row.
	if ( pick == CharacterCreateSurnameEdit:GetText() and #pool > 1 ) then
		pick = pool[math.random(#pool)];
	end
	CharacterCreateSurnameEdit:SetText(pick);
end
-- END CENTURION family names
