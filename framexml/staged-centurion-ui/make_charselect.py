#!/usr/bin/env python3
"""Build the Centurion character select screen (ui/ -> ui_centurion/).

Source: the stock CharacterSelect.{lua,xml} from patch-enUS-3 (no custom archive
carries them); shipped in patch-enUS-6 next to CharacterCreate.

On the Centurion realms:
  - up to 20 characters: 10 rows (9 while the create button shows) with a scroll
    bar, mouse wheel and arrow keys,
  - a World / Tournament badge per character (the server sends a tournament
    character's zone as Argent Tournament Grounds, see
    game/Miscellaneous/CharacterScreen.h),
  - move up / move down buttons on the selected character (and Shift+Up/Down),
    saved through the client-tweaks DLL's CenturionGlueRequest.
Other realms keep the stock screen.
"""
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
SRC = os.path.join(HERE, 'ui', 'Interface', 'GlueXML')
DST = os.path.join(HERE, 'ui_centurion', 'Interface', 'GlueXML')


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
        sys.exit('anchor found %d times: %r' % (n, old[:90]))
    return text.replace(old, new)


# ------------------------------------------------------------------------- XML
xml, xml_crlf = read(os.path.join(SRC, 'CharacterSelect.xml'))

# Room on the right of each row for the move buttons and the scroll bar.
xml = replace_once(xml, '<AbsDimension x="217" y="12"/>', '<AbsDimension x="180" y="12"/>')
xml = replace_once(xml, '<AbsDimension x="217" y="16"/>', '<AbsDimension x="180" y="16"/>')

# The Tournament marker, on the zone line of a tournament character in place of
# the zone (the zone there was only ever "Tournament grounds", the server's way of
# flagging the character - see CharacterScreen.h). A world character has no
# marker; its NAME is drawn blue instead (CharacterSelect_UpdateRows).
#
# The marker used to be right-aligned on the name line, where "Elgrom Fernbloom"
# ran into it once surnames arrived (Miscellaneous/Surnames.h), and the right edge
# of any line is where the selected row's move buttons sit. The start of the zone
# line collides with neither.
xml = replace_once(xml, '''						</FontString>
					</Layer>
				</Layers>
			</Frame>
		</Frames>
		<Scripts>
			<OnClick>
				CharacterSelectButton_OnClick(self);
			</OnClick>
			<OnDoubleClick>
				CharacterSelectButton_OnDoubleClick(self);
			</OnDoubleClick>
''', '''						</FontString>
						<FontString name="$parentMode" inherits="GlueFontNormalSmall" justifyH="LEFT" hidden="true">
							<Size>
								<AbsDimension x="90" y="12"/>
							</Size>
							<Anchors>
								<Anchor point="LEFT" relativeTo="$parentLocation" relativePoint="LEFT">
									<Offset>
										<AbsDimension x="0" y="0"/>
									</Offset>
								</Anchor>
							</Anchors>
						</FontString>
					</Layer>
				</Layers>
			</Frame>
		</Frames>
		<Scripts>
			<OnClick>
				CharacterSelectButton_OnClick(self);
			</OnClick>
			<OnDoubleClick>
				CharacterSelectButton_OnDoubleClick(self);
			</OnDoubleClick>
			<OnMouseWheel>
				CharacterSelect_OnMouseWheel(delta);
			</OnMouseWheel>
''')

# Scroll bar and move buttons inside the character list frame.
xml = replace_once(xml, '''							<Button name="CharSelectCharacterButton1" inherits="CharSelectCharacterButtonTemplate" id="1">
''', '''							<Slider name="CharSelectCharacterScrollBar" hidden="true">
								<Size>
									<AbsDimension x="16" y="0"/>
								</Size>
								<Anchors>
									<Anchor point="TOPRIGHT">
										<Offset>
											<AbsDimension x="-10" y="-84"/>
										</Offset>
									</Anchor>
									<Anchor point="BOTTOMRIGHT">
										<Offset>
											<AbsDimension x="-10" y="84"/>
										</Offset>
									</Anchor>
								</Anchors>
								<Frames>
									<Button name="$parentScrollUpButton" inherits="GlueScrollUpButtonTemplate">
										<Anchors>
											<Anchor point="BOTTOM" relativePoint="TOP"/>
										</Anchors>
										<Scripts>
											<OnClick>
												CharacterSelect_ScrollBy(-1);
												PlaySound("UChatScrollButton");
											</OnClick>
										</Scripts>
									</Button>
									<Button name="$parentScrollDownButton" inherits="GlueScrollDownButtonTemplate">
										<Anchors>
											<Anchor point="TOP" relativePoint="BOTTOM"/>
										</Anchors>
										<Scripts>
											<OnClick>
												CharacterSelect_ScrollBy(1);
												PlaySound("UChatScrollButton");
											</OnClick>
										</Scripts>
									</Button>
								</Frames>
								<Scripts>
									<OnValueChanged>
										CharacterSelect_OnScrollValueChanged(self, value);
									</OnValueChanged>
									<OnMouseWheel>
										CharacterSelect_OnMouseWheel(delta);
									</OnMouseWheel>
								</Scripts>
								<ThumbTexture inherits="GlueScrollBarButton" file="Interface\\Buttons\\UI-ScrollBar-Knob">
									<Size>
										<AbsDimension x="18" y="24"/>
									</Size>
									<TexCoords left="0.20" right="0.80" top="0.125" bottom="0.875"/>
								</ThumbTexture>
							</Slider>
							<Button name="CharSelectMoveUpButton" inherits="GlueScrollUpButtonTemplate" frameStrata="HIGH" hidden="true">
								<Scripts>
									<OnClick>
										CharacterSelect_MoveSelected(-1);
									</OnClick>
									<OnEnter>
										GlueTooltip_SetOwner(self);
										GlueTooltip_SetText(CENTURION_MOVE_UP);
									</OnEnter>
									<OnLeave>
										GlueTooltip:Hide();
									</OnLeave>
								</Scripts>
							</Button>
							<Button name="CharSelectMoveDownButton" inherits="GlueScrollDownButtonTemplate" frameStrata="HIGH" hidden="true">
								<Scripts>
									<OnClick>
										CharacterSelect_MoveSelected(1);
									</OnClick>
									<OnEnter>
										GlueTooltip_SetOwner(self);
										GlueTooltip_SetText(CENTURION_MOVE_DOWN);
									</OnEnter>
									<OnLeave>
										GlueTooltip:Hide();
									</OnLeave>
								</Scripts>
							</Button>
							<Button name="CharSelectCharacterButton1" inherits="CharSelectCharacterButtonTemplate" id="1">
''')

# A LAST NAME box on the rename prompt (Miscellaneous/Surnames.h on the server).
# A rename takes the whole name - first and last - and a last name is required,
# so the prompt needs a second box. It is a copy of CharacterRenameEditBox,
# hidden here and placed from Lua (CharacterRename_UpdateLayout), so a realm
# without surnames keeps the stock prompt. Its layer also carries the two
# labels, anchored one to each box, so they come and go with it.
xml = replace_once(xml, '''				<FontString inherits="GlueFontHighlight"/>
			</EditBox>
		</Frames>
		<Scripts>
			<OnShow>
				self:Raise();
			</OnShow>
			<OnHide>
				CharacterRenameEditBox:SetText("");
			</OnHide>''', '''				<FontString inherits="GlueFontHighlight"/>
			</EditBox>
			<EditBox name="CharacterRenameSurnameEditBox" letters="12" historyLines="1" hidden="true">
				<Size>
					<AbsDimension x="130" y="32"/>
				</Size>
				<Anchors>
					<Anchor point="LEFT" relativeTo="CharacterRenameEditBox" relativePoint="RIGHT">
						<Offset>
							<AbsDimension x="28" y="0"/>
						</Offset>
					</Anchor>
				</Anchors>
				<Layers>
					<Layer level="ARTWORK">
						<Texture file="Interface\\ChatFrame\\UI-ChatInputBorder-Left">
							<Size>
								<AbsDimension x="75" y="32"/>
							</Size>
							<Anchors>
								<Anchor point="LEFT">
									<Offset>
										<AbsDimension x="-10" y="0"/>
									</Offset>
								</Anchor>
							</Anchors>
							<TexCoords left="0" right="0.29296875" top="0" bottom="1.0"/>
						</Texture>
						<Texture file="Interface\\ChatFrame\\UI-ChatInputBorder-Right">
							<Size>
								<AbsDimension x="75" y="32"/>
							</Size>
							<Anchors>
								<Anchor point="RIGHT">
									<Offset>
										<AbsDimension x="10" y="0"/>
									</Offset>
								</Anchor>
							</Anchors>
							<TexCoords left="0.70703125" right="1.0" top="0" bottom="1.0"/>
						</Texture>
						<FontString name="CharacterRenameSurnameLabel" inherits="GlueFontNormalSmall" text="Last Name">
							<Anchors>
								<Anchor point="BOTTOM" relativePoint="TOP">
									<Offset>
										<AbsDimension x="0" y="1"/>
									</Offset>
								</Anchor>
							</Anchors>
						</FontString>
						<FontString name="CharacterRenameFirstNameLabel" inherits="GlueFontNormalSmall" text="First Name">
							<Anchors>
								<Anchor point="BOTTOM" relativeTo="CharacterRenameEditBox" relativePoint="TOP">
									<Offset>
										<AbsDimension x="0" y="1"/>
									</Offset>
								</Anchor>
							</Anchors>
						</FontString>
					</Layer>
				</Layers>
				<Scripts>
					<OnEnterPressed>
						CharacterRenameButton1:Click();
					</OnEnterPressed>
					<OnEscapePressed>
						CharacterRenameDialog:Hide();
					</OnEscapePressed>
					<OnTabPressed>
						CharacterRenameEditBox:SetFocus();
					</OnTabPressed>
				</Scripts>
				<FontString inherits="GlueFontHighlight"/>
			</EditBox>
		</Frames>
		<Scripts>
			<OnShow>
				self:Raise();
			</OnShow>
			<OnHide>
				CharacterRenameEditBox:SetText("");
				if ( CharacterRenameSurnameEditBox ) then
					CharacterRenameSurnameEditBox:SetText("");
				end
			</OnHide>''')

write(os.path.join(DST, 'CharacterSelect.xml'), xml, xml_crlf)

# ------------------------------------------------------------------------- Lua
lua, lua_crlf = read(os.path.join(SRC, 'CharacterSelect.lua'))
lua = lua.rstrip('\n') + '\n' + '''
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
CENTURION_MOVE_UP = "Move up\\n(Shift+Up)";
CENTURION_MOVE_DOWN = "Move down\\n(Shift+Down)";

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

	CenturionGlueRequest("ORDER\\t"..table.concat(names, ","));
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
-- screen's does - CenturionGlueRequest("SURNAME\\t<new name>\\t<surname>") just
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
		CenturionGlueRequest("SURNAME\\t"..(name or "").."\\t"..surname);
	end
	return _origRenameCharacter_Surname(index, name, ...);
end
-- END CENTURION character list
'''
write(os.path.join(DST, 'CharacterSelect.lua'), lua, lua_crlf)
print('wrote', DST)
