-- CENTURION: the switch for the World / Tournament / Bot marker over names.
--
-- The marker itself is drawn by the client tweaks (dinput8.dll, src/nametag.cpp)
-- from a hidden aura the realm gives every player. The DLL registers one CVar,
-- centurionNameTags, and remembers its value in AnimSpeedFix.ini, so this panel
-- only has to flip it. It lives under Interface > AddOns > Centurion.
--
-- A client without the DLL has no such CVar; the box is then shown greyed out
-- rather than pretending to do something.

local CVAR = "centurionNameTags";

local function CenturionNameTags_Get()
	local ok, value = pcall(GetCVar, CVAR);
	if ( ok ) then
		return value;
	end
	return nil;
end

local panel = CreateFrame("Frame", "CenturionOptionsPanel", UIParent);
panel.name = "Centurion";
panel:Hide();

local title = panel:CreateFontString(nil, "ARTWORK", "GameFontNormalLarge");
title:SetPoint("TOPLEFT", 16, -16);
title:SetText("Centurion");

local subtitle = panel:CreateFontString(nil, "ARTWORK", "GameFontHighlightSmall");
subtitle:SetPoint("TOPLEFT", title, "BOTTOMLEFT", 0, -8);
subtitle:SetPoint("RIGHT", panel, "RIGHT", -32, 0);
subtitle:SetJustifyH("LEFT");
subtitle:SetText("Options for the Centurion client.");

local check = CreateFrame("CheckButton", "CenturionNameTagsCheck", panel, "InterfaceOptionsCheckButtonTemplate");
check:SetPoint("TOPLEFT", subtitle, "BOTTOMLEFT", -2, -16);
CenturionNameTagsCheckText:SetText("Show |cff73bfffWorld|r, |cffff9933Tournament|r and |cffb266ffBot|r above player names");

local note = panel:CreateFontString(nil, "ARTWORK", "GameFontDisableSmall");
note:SetPoint("TOPLEFT", check, "BOTTOMLEFT", 26, -2);
note:SetPoint("RIGHT", panel, "RIGHT", -32, 0);
note:SetJustifyH("LEFT");

check:SetScript("OnClick", function(self)
	local on = self:GetChecked() and "1" or "0";
	SetCVar(CVAR, on);
	if ( on == "1" ) then
		PlaySound("igMainMenuOptionCheckBoxOn");
	else
		PlaySound("igMainMenuOptionCheckBoxOff");
	end
end);

panel:SetScript("OnShow", function()
	local value = CenturionNameTags_Get();
	if ( value == nil ) then
		check:SetChecked(false);
		check:Disable();
		CenturionNameTagsCheckText:SetFontObject("GameFontDisable");
		note:SetText("Needs the Centurion client tweaks (dinput8.dll), which this client is not running.");
	else
		check:Enable();
		CenturionNameTagsCheckText:SetFontObject("GameFontHighlight");
		check:SetChecked(value ~= "0");
		note:SetText("Which kind of character each player is. Takes effect at once and is remembered.");
	end
end);

InterfaceOptions_AddCategory(panel);
