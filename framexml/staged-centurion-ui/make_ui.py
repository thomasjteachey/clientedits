#!/usr/bin/env python3
"""Build the Centurion UI changes from the live patch copies (ui/ -> ui_centurion/).

The client ranks patch archives by their LAST character only (patch-enUS-A = A,
patch-Y = Y; the folder and the locale part do not count), so each file has to be
changed in the highest archive that carries it:

  CharacterCreate.{lua,xml}       patch-enUS-6 (ui/Interface/GlueXML) - replaced in
                                  place; the tournament checkbox only shows on the
                                  Centurion realms.
  PVPBattlegroundFrame.{lua,xml}  patch-Y (ui/Y/Interface/FrameXML) - Y outranks the
                                  copies in patch-enUS-A and -8, which never load.
"""
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
SRC = os.path.join(HERE, 'ui')
DST = os.path.join(HERE, 'ui_centurion')


def read(path):
    with open(path, 'r', encoding='utf-8', newline='') as f:
        return f.read()


def write(path, text):
    os.makedirs(os.path.dirname(path), exist_ok=True)
    with open(path, 'w', encoding='utf-8', newline='') as f:
        f.write(text)


def insert_after(text, anchor, block, count=1):
    n = text.count(anchor)
    if n != count:
        sys.exit('anchor found %d times (want %d): %r' % (n, count, anchor[:80]))
    i = text.index(anchor) + len(anchor)
    return text[:i] + block + text[i:]


def replace_once(text, old, new):
    if text.count(old) != 1:
        sys.exit('replace anchor found %d times: %r' % (text.count(old), old[:80]))
    return text.replace(old, new)


# CharacterCreate.{lua,xml}: make_charcreate.py

# ---------------------------------------------------------------- PVPBattlegroundFrame
bg_xml = read(os.path.join(SRC, 'Y/Interface/FrameXML/PVPBattlegroundFrame.xml'))
anchor = '''					<OnUpdate function="WintergraspTimer_OnUpdate"/>
				</Scripts>
			</Frame>
'''


def checkbutton(name, y, label, scripts):
    return '''			<CheckButton name="%s" hidden="true">
				<Size>
					<AbsDimension x="22" y="22"/>
				</Size>
				<Anchors>
					<Anchor point="RIGHT" relativePoint="TOPRIGHT">
						<Offset>
							<AbsDimension x="-40" y="%d"/>
						</Offset>
					</Anchor>
				</Anchors>
				<HitRectInsets>
					<AbsInset left="-110" right="0" top="0" bottom="0"/>
				</HitRectInsets>
				<Layers>
					<Layer level="ARTWORK">
						<FontString name="$parentText" inherits="GameFontNormalSmall" justifyH="RIGHT" text="%s">
							<Anchors>
								<Anchor point="RIGHT" relativePoint="LEFT">
									<Offset>
										<AbsDimension x="-1" y="1"/>
									</Offset>
								</Anchor>
							</Anchors>
						</FontString>
					</Layer>
				</Layers>
				<Scripts>
%s				</Scripts>
				<NormalTexture file="Interface\\Buttons\\UI-CheckBox-Up"/>
				<PushedTexture file="Interface\\Buttons\\UI-CheckBox-Down"/>
				<HighlightTexture file="Interface\\Buttons\\UI-CheckBox-Highlight" alphaMode="ADD"/>
				<CheckedTexture file="Interface\\Buttons\\UI-CheckBox-Check"/>
				<DisabledCheckedTexture file="Interface\\Buttons\\UI-CheckBox-Check-Disabled"/>
			</CheckButton>
''' % (name, y, label, scripts)


# The chest box explains itself from one string; the tournament box builds its
# tooltip in Lua from what the server last said, and keeps its mouse scripts
# alive while it is disabled (CenturionTournamentQueue_OnLoad).
GURUBASHI_SCRIPTS = '''					<OnClick function="CenturionGurubashiChest_OnClick"/>
					<OnEnter>
						GameTooltip:SetOwner(self, "ANCHOR_BOTTOMRIGHT", 5, 40);
						GameTooltip:SetText(CENTURION_GURUBASHI_CHEST_TOOLTIP, nil, nil, nil, nil, 1);
					</OnEnter>
					<OnLeave function="GameTooltip_Hide"/>
'''

TOURNAMENT_SCRIPTS = '''					<OnLoad function="CenturionTournamentQueue_OnLoad"/>
					<OnClick function="CenturionTournamentQueue_OnClick"/>
					<OnEnter function="CenturionTournamentQueue_OnEnter"/>
					<OnLeave function="GameTooltip_Hide"/>
'''

bg_xml = insert_after(bg_xml, anchor,
                      checkbutton('PVPBattlegroundFrameGurubashiChest', -47, 'Gurubashi chest', GURUBASHI_SCRIPTS) +
                      checkbutton('PVPBattlegroundFrameTournamentQueue', -65, 'Tournament queue', TOURNAMENT_SCRIPTS))
write(os.path.join(DST, 'Interface/FrameXML/PVPBattlegroundFrame.xml'), bg_xml)

bg_lua = read(os.path.join(SRC, 'Y/Interface/FrameXML/PVPBattlegroundFrame.lua'))
bg_lua = replace_once(bg_lua, '''function PVPBattlegroundFrame_OnShow(self)
	if ( IsInInstance() ) then
		WintergraspTimer:Hide();
	else
		WintergraspTimer:Show();
	end
''', '''function PVPBattlegroundFrame_OnShow(self)
	-- CENTURION: the Gurubashi chest toggle takes Wintergrasp's place once the
	-- server has answered for it.
	if ( IsInInstance() or CenturionPVPToggles_GurubashiChestAnswered() ) then
		WintergraspTimer:Hide();
	else
		WintergraspTimer:Show();
	end
	CenturionPVPToggles_Query();
''')
bg_lua = bg_lua.rstrip('\n') + '\n' + '''
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
CENTURION_GURUBASHI_CHEST_TOOLTIP = "Gurubashi chest\\n\\nWhen the hourly Gurubashi Arena chest appears, everyone in Stranglethorn Vale is pulled into the Battle Ring to fight for it.\\n\\nUncheck to stay where you are and not count towards the chest.";

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
'''
write(os.path.join(DST, 'Interface/FrameXML/PVPBattlegroundFrame.lua'), bg_lua)
print('wrote', DST)
