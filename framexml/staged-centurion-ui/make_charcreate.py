#!/usr/bin/env python3
"""Build the Centurion character create screen (ui/ -> ui_centurion/).

Source: the owner's CharacterCreate.{lua,xml} from patch-enUS-6 (ui/Interface/GlueXML),
replaced in place in patch-enUS-6.

On the Centurion realms (not during paid services):
  - "Tournament character" checkbox next to the name box. Ticked, the name is sent
    first-letter-lowercase / rest-uppercase, which the server decodes as a tournament
    character (game/Miscellaneous/TournamentMode.h).
  - A Challenges panel above it, one checkbox per challenge mode. Ticking the
    tournament box unticks every challenge and ticking a challenge unticks the
    tournament box; ticking one of an exclusive pair locks the other. The choice goes
    to the server through the client-tweaks DLL's
    CenturionGlueRequest ("CREATE\\t<name>\\t<mask>", bit n = ChallengeModeSettings n)
    just before CreateCharacter; the panel stays hidden without the DLL.
  - Tooltips that wrap (the shared CharacterCreateTooltip never does).
  - A "LAST NAME" box beside the name box, sent to the server the same way
    ("SURNAME\\t<name>\\t<surname>"). The name box slides left so the pair is
    centred and the tournament checkbox moves to the right of the new box; a
    realm without surnames gets the stock layout. A paid customize/race change
    gets the pair too, filled from the listed "First Last".
  - The paid race change filter reads each button's own race (button.raceIndex)
    instead of assuming button i shows race i, which the 4+4 layout breaks.
"""
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
SRC = os.path.join(HERE, 'ui', 'Interface', 'GlueXML')
DST = os.path.join(HERE, 'ui_centurion', 'Interface', 'GlueXML')

# The last name Randomize button offers the same names the server's naming tool
# hands out, read from that tool rather than copied, so there is one list.
SURNAMES_TOOL = os.path.normpath(os.path.join(HERE, '..', '..', '..', 'servers', 'tc-lplus',
                                              'tools', 'surnames', 'surnames.py'))
# Race id -> the file name GetNameForRace() returns, upper-cased.
RACE_FILE = {1: 'HUMAN', 2: 'ORC', 3: 'DWARF', 4: 'NIGHTELF', 5: 'SCOURGE', 6: 'TAUREN',
             7: 'GNOME', 8: 'TROLL', 10: 'BLOODELF', 11: 'DRAENEI'}


def load_surname_pools():
    import importlib.util
    if not os.path.isfile(SURNAMES_TOOL):
        sys.exit('no surname pools at %s' % SURNAMES_TOOL)
    spec = importlib.util.spec_from_file_location('surnames_tool', SURNAMES_TOOL)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    module.check_pools()
    return module.POOLS


def read(path):
    with open(path, 'r', encoding='utf-8', newline='') as f:
        text = f.read()
    return text.replace('\r\n', '\n'), '\r\n' in text


def write(path, text, crlf):
    os.makedirs(os.path.dirname(path), exist_ok=True)
    if crlf:
        text = text.replace('\n', '\r\n')
    with open(path, 'w', encoding='utf-8', newline='') as f:
        f.write(text)


def replace_once(text, old, new):
    n = text.count(old)
    if n != 1:
        sys.exit('replace target found %d times: %r' % (n, old[:90]))
    return text.replace(old, new)


def insert_after(text, anchor, block):
    n = text.count(anchor)
    if n != 1:
        sys.exit('anchor found %d times: %r' % (n, anchor[:90]))
    i = text.index(anchor) + len(anchor)
    return text[:i] + block + text[i:]


# (bit = ChallengeModeSettings value, label, exclusive partner bit or None), in panel order.
CHALLENGES = [
    (0, 'Hardcore', 1),
    (1, 'Semi-Hardcore', 0),
    (2, 'Self Crafted', 7),
    (7, 'Iron Man', 2),
    (3, 'Poor/Normal Gear Only', None),
    (4, 'Slow XP', 5),
    (5, 'Very Slow XP', 4),
    (6, 'Quest XP Only', None),
]
ROW_HEIGHT = 22

# ------------------------------------------------------------------------- XML
xml, xml_crlf = read(os.path.join(SRC, 'CharacterCreate.xml'))

tooltip_anchor = '''	<Frame name="CharacterCreateTooltip" frameStrata="TOOLTIP" hidden="true" parent="GlueParent" inherits="GlueTooltipTemplate">
		<Scripts>
			<OnLoad>
				GlueTooltip_SetFont(self, CharacterCreateTooltipFont);
				self:SetBackdropBorderColor(1.0, 1.0, 1.0);
				self:SetBackdropColor(0.09, 0.09, 0.19 );
			</OnLoad>
		</Scripts>
	</Frame>
'''
tooltip_frame = '''	<!-- CENTURION: tooltip for the tournament and challenge checkboxes. Same look as
	     CharacterCreateTooltip, which sizes itself to one unwrapped line; this one
	     wraps at a fixed width, left-justified. -->
	<Frame name="CharacterCreateCenturionTooltip" frameStrata="TOOLTIP" hidden="true" parent="GlueParent" inherits="GlueTooltipTemplate">
		<Scripts>
			<OnLoad>
				GlueTooltip_SetFont(self, CharacterCreateTooltipFont);
				self:SetBackdropBorderColor(1.0, 1.0, 1.0);
				self:SetBackdropColor(0.09, 0.09, 0.19 );
				local text = _G[self:GetName().."TextLeft1"];
				text:SetWidth(300);
				text:SetJustifyH("LEFT");
			</OnLoad>
		</Scripts>
	</Frame>
'''
xml = insert_after(xml, tooltip_anchor, tooltip_frame)

# The name box's label is anonymous, so Lua cannot reach it; naming it is the
# whole change. Its text stays the NAME global ("Name") and only becomes
# "First Name" while the last name box is up (CharacterCreate_UpdateSurnameLayout),
# so a screen without surnames still reads exactly as it did.
xml = replace_once(xml,
    '<FontString inherits="GlueFontNormalLarge" text="NAME">',
    '<FontString name="CharacterCreateNameEditLabel" inherits="GlueFontNormalLarge" text="NAME">')

randomize_anchor = '''								CharacterCreateNameEdit:SetText(GetRandomName());
								PlaySound("gsCharacterCreationLook");
							</OnClick>
							<OnUpdate>
								CharacterCreate_DeathKnightSwap(self);
							</OnUpdate>
						</Scripts>
					</Button>
'''

checkbox_textures = '''						<NormalTexture file="Interface\\Buttons\\UI-CheckBox-Up"/>
						<PushedTexture file="Interface\\Buttons\\UI-CheckBox-Down"/>
						<HighlightTexture file="Interface\\Buttons\\UI-CheckBox-Highlight" alphaMode="ADD"/>
						<CheckedTexture file="Interface\\Buttons\\UI-CheckBox-Check"/>
						<DisabledCheckedTexture file="Interface\\Buttons\\UI-CheckBox-Check-Disabled"/>
'''

block = '''					<CheckButton name="CharacterCreateTournamentMode" hidden="true">
						<Size x="26" y="26"/>
						<Anchors>
							<Anchor point="LEFT" relativeTo="CharacterCreateNameEdit" relativePoint="RIGHT" x="-2" y="-2"/>
						</Anchors>
						<HitRectInsets>
							<AbsInset left="0" right="-120" top="0" bottom="0"/>
						</HitRectInsets>
						<Layers>
							<Layer level="ARTWORK">
								<FontString name="$parentText" inherits="GlueFontNormalSmall" text="Tournament character">
									<Anchors>
										<Anchor point="LEFT" relativePoint="RIGHT" x="0" y="1"/>
									</Anchors>
								</FontString>
							</Layer>
						</Layers>
						<Scripts>
							<OnClick>
								CharacterCreateTournamentMode_OnClick(self);
							</OnClick>
							<OnEnter>
								CharacterCreate_ShowCenturionTooltip(self, CENTURION_TOURNAMENT_CHARACTER_TOOLTIP);
							</OnEnter>
							<OnLeave>
								CharacterCreateCenturionTooltip:Hide();
							</OnLeave>
						</Scripts>
''' + checkbox_textures + '''					</CheckButton>
					<Frame name="CharacterCreateChallengesFrame" hidden="true">
						<Size x="190" y="%d"/>
						<Anchors>
							<Anchor point="BOTTOMLEFT" relativeTo="CharacterCreateTournamentMode" relativePoint="TOPLEFT" x="64" y="0"/>
						</Anchors>
						<Backdrop bgFile="Interface\\Tooltips\\UI-Tooltip-Background" edgeFile="Interface\\Tooltips\\UI-Tooltip-Border" tile="true">
							<EdgeSize>
								<AbsValue val="16"/>
							</EdgeSize>
							<TileSize>
								<AbsValue val="16"/>
							</TileSize>
							<BackgroundInsets>
								<AbsInset left="4" right="4" top="4" bottom="4"/>
							</BackgroundInsets>
						</Backdrop>
						<Layers>
							<Layer level="ARTWORK">
								<FontString name="$parentTitle" inherits="GlueFontNormal" text="Challenges">
									<Anchors>
										<Anchor point="TOPLEFT" x="10" y="-8"/>
									</Anchors>
								</FontString>
							</Layer>
						</Layers>
						<Frames>
''' % (30 + ROW_HEIGHT * len(CHALLENGES) + 6)

previous = None
for bit, label, _partner in CHALLENGES:
    if previous is None:
        anchor = '<Anchor point="TOPLEFT" x="6" y="-26"/>'
    else:
        anchor = '<Anchor point="TOPLEFT" relativeTo="CharacterCreateChallenge%d" relativePoint="BOTTOMLEFT" x="0" y="%d"/>' % (previous, 24 - ROW_HEIGHT)
    block += '''							<CheckButton name="CharacterCreateChallenge%d" id="%d" motionScriptsWhileDisabled="true">
								<Size x="24" y="24"/>
								<Anchors>
									%s
								</Anchors>
								<HitRectInsets>
									<AbsInset left="0" right="-140" top="0" bottom="0"/>
								</HitRectInsets>
								<Layers>
									<Layer level="ARTWORK">
										<FontString name="$parentText" inherits="GlueFontNormalSmall" justifyH="LEFT" text="%s">
											<Anchors>
												<Anchor point="LEFT" relativePoint="RIGHT" x="0" y="1"/>
											</Anchors>
										</FontString>
									</Layer>
								</Layers>
								<Scripts>
									<OnClick>
										CharacterCreateChallenge_OnClick(self);
									</OnClick>
									<OnEnter>
										CharacterCreate_ShowCenturionTooltip(self, CENTURION_CHALLENGE_TOOLTIPS[self:GetID()]);
									</OnEnter>
									<OnLeave>
										CharacterCreateCenturionTooltip:Hide();
									</OnLeave>
								</Scripts>
''' % (bit, bit, anchor, label) + checkbox_textures.replace('\t\t\t\t\t\t', '\t\t\t\t\t\t\t\t') + '''							</CheckButton>
'''
    previous = bit

block += '''						</Frames>
						<Scripts>
							<OnLoad>
								self:SetBackdropBorderColor(0.6, 0.6, 0.6);
								self:SetBackdropColor(0.09, 0.09, 0.19, 0.85);
							</OnLoad>
						</Scripts>
					</Frame>
'''

# The last name box, a copy of CharacterCreateNameEdit sat beside it. Hidden
# here and placed from Lua, so a realm without surnames keeps the stock screen
# exactly (CharacterCreate_UpdateSurnameLayout).
block += '''					<EditBox name="CharacterCreateSurnameEdit" letters="12" hidden="true">
						<Size x="156" y="40"/>
						<Anchors>
							<Anchor point="LEFT" relativeTo="CharacterCreateNameEdit" relativePoint="RIGHT" x="8" y="0"/>
						</Anchors>
						<Layers>
							<Layer level="BACKGROUND">
								<FontString inherits="GlueFontNormalLarge" text="Last Name">
									<Size x="256" y="64"/>
									<Anchors>
										<Anchor point="BOTTOM" relativePoint="TOP" x="0" y="-23"/>
									</Anchors>
								</FontString>
							</Layer>
						</Layers>
						<Backdrop bgFile="Interface\\Tooltips\\UI-Tooltip-Background" edgeFile="Interface\\Glues\\Common\\Glue-Tooltip-Border" tile="true">
							<BackgroundInsets>
								<AbsInset left="10" right="5" top="4" bottom="9"/>
							</BackgroundInsets>
							<TileSize>
								<AbsValue val="16"/>
							</TileSize>
							<EdgeSize>
								<AbsValue val="16"/>
							</EdgeSize>
						</Backdrop>
						<Frames>
							<!-- Same button as CharacterCreateRandomName under the name box,
							     and a child of this box so it comes and goes with it. -->
							<Button name="CharacterCreateRandomSurname" inherits="GlueButtonSmallTemplate" text="RANDOMIZE">
								<Size x="146" y="30"/>
								<Anchors>
									<Anchor point="TOP" relativePoint="BOTTOM" x="0" y="7"/>
								</Anchors>
								<Scripts>
									<OnLoad>
										self:SetWidth(self:GetTextWidth() + 50);
									</OnLoad>
									<OnClick>
										CharacterCreate_RandomSurname();
										PlaySound("gsCharacterCreationLook");
									</OnClick>
									<OnUpdate>
										CharacterCreate_DeathKnightSwap(self);
									</OnUpdate>
								</Scripts>
							</Button>
						</Frames>
						<Scripts>
							<OnEscapePressed>
								CharacterCreate_Back();
							</OnEscapePressed>
							<OnEnterPressed>
								CharacterCreate_Okay();
							</OnEnterPressed>
							<OnTabPressed>
								CharacterCreateNameEdit:SetFocus();
							</OnTabPressed>
						</Scripts>
						<FontString inherits="GlueEditBoxFont"/>
						<TextInsets>
							<AbsInset left="15"/>
						</TextInsets>
					</EditBox>
'''
xml = insert_after(xml, randomize_anchor, block)
write(os.path.join(DST, 'CharacterCreate.xml'), xml, xml_crlf)

# ------------------------------------------------------------------------- Lua
lua, lua_crlf = read(os.path.join(SRC, 'CharacterCreate.lua'))

# The paid-change race filter judged button i as race i, but the 4+4 remap patch
# in the owner's file puts a different race on most buttons (each carries the one
# it shows as button.raceIndex), so a Tauren shaman was offered Dwarf, not Troll.
lua = replace_once(lua, '''		for i=1, MAX_RACES, 1 do
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
		end''', '''		-- CENTURION: the race buttons are laid out 4+4 by the remap patch below,
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
		end''')

challenge_rows = '\n'.join('\t{ bit = %d, partner = %s },' % (bit, 'nil' if partner is None else partner)
                           for bit, _label, partner in CHALLENGES)

lua = lua.rstrip('\n') + '\n' + '''
-- CENTURION: world or tournament character, and challenge modes, on the Centurion
-- realms only.
--
-- The server reads a tournament character from the case of the name it is sent:
-- first letter lowercase, the rest uppercase ("eLGROM"). It decodes that before
-- names are normalized, so the character is still called "Elgrom"
-- (Miscellaneous/TournamentMode.h on the server).
--
-- Challenge modes cannot ride the create request, so they go first through the
-- client-tweaks DLL: CenturionGlueRequest("CREATE\\t<name>\\t<mask>"), where bit n
-- is the server's ChallengeModeSettings n. The server applies them when the
-- character is created, with the same checks as the challenge stones
-- (Miscellaneous/CharacterScreen.h).
CENTURION_TOURNAMENT_CHARACTER_TOOLTIP = "Tournament character\\n\\nStarts at level 60 on the tournament grounds with a full kit, and queues for battlegrounds and arenas with other tournament characters.\\n\\nTournament characters stay on the tournament grounds. They cannot trade or mail world characters, or use the auction house, dungeons or flight masters, and they cannot take challenges.\\n\\nLeave unchecked for a world character.";

-- Keyed by ChallengeModeSettings value (the checkbox ID).
CENTURION_CHALLENGE_TOOLTIPS = {
	[0] = "Hardcore\\n\\nOne life. Dying finishes the character: you are disconnected at once, and every later login kills and disconnects you again. There is no way back.\\n\\nCannot be combined with Semi-Hardcore.",
	[1] = "Semi-Hardcore\\n\\nDeath costs everything you are wearing. However you die, every equipped item except your shirt and tabard is deleted and your gold is set to zero. Nothing is left behind to loot. Your bags are left alone, and the character survives.\\n\\nCannot be combined with Hardcore.",
	[2] = "Self Crafted\\n\\nYou may only equip items you crafted yourself. Anything looted, bought or given to you cannot be worn.\\n\\nCannot be combined with Iron Man.",
	[3] = "Poor/Normal Gear Only\\n\\nYou may only equip grey and white items. Nothing green or better, however it was obtained.",
	[4] = "Slow XP\\n\\nYou earn half of the normal experience from everything.\\n\\nCannot be combined with Very Slow XP.",
	[5] = "Very Slow XP\\n\\nYou earn a quarter of the normal experience from everything.\\n\\nCannot be combined with Slow XP.",
	[6] = "Quest XP Only\\n\\nKills give you no experience at all; only quests do. A pet with you still earns its own share.",
	[7] = "Iron Man\\n\\nAll of these together: no talent points ever, no resurrection, grey and white gear only, no potions, elixirs, flasks or food that heals over time, no enchantments, and no professions (Runeforging, Poisons and Beast Training are allowed).\\n\\nCannot be combined with Self Crafted.",
};

CENTURION_CHALLENGES = {
''' + challenge_rows + '''
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
			CenturionGlueRequest("CREATE\\t"..name.."\\t"..CharacterCreate_ChallengeMask());
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
'''

lua += '''
-- CENTURION: family names (Miscellaneous/Surnames.h on the server).
--
-- The create request has no field for a second name either, so it travels the
-- way the challenge modes do - through the client-tweaks DLL, as
-- CenturionGlueRequest("SURNAME\\t<name>\\t<surname>") sent just before
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
CENTURION_SURNAME_INVALID = "That is not a valid last name.\\n\\nLetters only, between %d and %d of them, and no more than two of the same letter in a row.";
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

-- Without the DLL there is no LAST NAME box, and the server refuses a new
-- character that arrives without a family name (CHAR_CREATE_FAILED, "Character
-- creation failed"). Say what is actually wrong before sending it, rather than
-- leave the player retrying names.
CENTURION_NO_CLIENT_TWEAKS = "Your game is not loading the Centurion client tweaks (dinput8.dll in your World of Warcraft folder), so it cannot send your character's last name, and this realm needs one.\\n\\nStart the game from the Centurion launcher, and check that your antivirus has not removed dinput8.dll.";

local _origCharacterCreate_Okay_Surname = CharacterCreate_Okay;
function CharacterCreate_Okay(...)
	if ( not PAID_SERVICE_TYPE and CenturionGlueRequest == nil and CharacterCreate_IsTournamentRealm() ) then
		if ( GlueDialog_Show ) then
			GlueDialog_Show("OKAY", CENTURION_NO_CLIENT_TWEAKS);
		end
		return;
	end
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
		CenturionGlueRequest("SURNAME\\t"..CharacterCreateNameEdit:GetText().."\\t"..surname);
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
''' + ''.join('\t["%s"] = { %s },\n' % (RACE_FILE[race], ', '.join('"%s"' % n for n in names))
              for race, names in sorted(load_surname_pools().items())) + '''};

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
'''
write(os.path.join(DST, 'CharacterCreate.lua'), lua, lua_crlf)
print('wrote', DST)
