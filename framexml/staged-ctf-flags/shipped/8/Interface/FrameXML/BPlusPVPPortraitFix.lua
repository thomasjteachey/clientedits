-- BarracksPlus: keep the main PvP window portrait from showing as a hollow frame.
-- Stock PVPFrame_OnShow fills PVPFramePortrait with SetPortraitTexture(..., "player"),
-- which comes out empty here, so artwork that is known to exist in the 3.3.5 client
-- is forced whenever the PvP window or one of its tabs is shown, and the parent
-- portrait is created if it is missing.
--
-- This used to sit at the top of patch-enUS-A/-8's PVPBattlegroundFrame.lua, where
-- patch-Y's copy of that file shadowed it. As its own FrameXML.toc entry it loads from
-- the winning toc: patch-enUS-A for Barracks+/Centurion, patch-enUS-8 for Legionnaire+.
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
