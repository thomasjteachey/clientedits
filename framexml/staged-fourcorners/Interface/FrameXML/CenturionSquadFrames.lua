-- CENTURION: enemy frames and scoreboard colours for a multi-team arena
-- (Baradin Hold: Four Corners, up to four squads).
--
-- The stock enemy frames (Blizzard_ArenaUI) are arena1-arena5, and the client
-- fills those with everyone who is not on the viewer's side - in a four-squad
-- match up to fifteen players from three squads, five of them at most, mixed.
-- The scoreboard knows only two sides (the PvP log's team byte), so every
-- enemy row wore the same colour. So the server feeds each squad's roster and
-- state instead (Arena::UpdateSquadFeed), as CCGAME addon lines:
--
--   SQ:<mine>:<squads>        you are in squad <mine> (0-3, "-" none) of <squads>
--   SQ:-                      not a squad match, or you left it: clear
--   SQR:<s>:<team>:<members>  squad s's roster, "name,class;..." in seat order
--   SQH:<s>:<states>          the same members: <hp%>.<power%>.<power type>,
--                             d (dead), s (stealthed), o (not here)
--   SQW:<s> / SQW:-           the winning squad, or a draw
--
-- and asked for all of it with CCGAMEREQ SQUADS once the UI is up in an arena.
--
-- The frames are the stock ones, member for member: ArenaEnemyFrameTemplate's
-- size, border, class circle, name and the two bars, laid out the way
-- Blizzard_ArenaUI.xml lays them out (patch-enUS-2). What is new is how they
-- are grouped: one column per enemy squad under its name in its colour, since
-- fifteen of them stacked would not fit the screen, and the block shrinks for
-- squads of four (90%) and five (80%). With only two teams (Buffle Shuffle feeds
-- these too, since arena1-5 cannot follow its re-dealt rounds) there is one
-- enemy column and no header, exactly as the stock frames look. Health and mana text obey
-- the party status-text option, as the stock bars do, and show on mouseover;
-- "Dead", "Stealthed" and "Away" always show. Left click targets, right click
-- focuses. The stock frames are put away for the match. On the scoreboard
-- every row takes its squad's colour, matched by name, and the winner banner
-- the winning squad's.
--
-- The members are secure buttons (/targetexact, /focus), so which ones exist
-- and where they sit can only change out of combat; a roster that changes
-- mid-fight is laid out when combat ends. Bars, text and colours update any
-- time.

CENTURION_SQUAD_NAMES = { [0] = "Gold Team", [1] = "Green Team", [2] = "Blue Team", [3] = "Purple Team" };
-- The bright colour (headers - the client DLL's squad colours) and the
-- scoreboard's darker row band. Gold and green bands are the stock arena ones.
CENTURION_SQUAD_COLORS = {
	[0] = { r = 1.00, g = 0.78, b = 0.00, band = { 0.85, 0.71, 0.26 } },
	[1] = { r = 0.20, g = 0.88, b = 0.30, band = { 0.19, 0.57, 0.11 } },
	[2] = { r = 0.18, g = 0.66, b = 1.00, band = { 0.11, 0.26, 0.51 } },
	[3] = { r = 0.72, g = 0.30, b = 1.00, band = { 0.40, 0.16, 0.60 } },
};
CENTURION_SQUAD_DEAD = DEAD or "Dead";
CENTURION_SQUAD_STEALTHED = "Stealthed";
CENTURION_SQUAD_AWAY = "Away";

local MAX_SQUADS, MAX_MEMBERS = 4, 5;
-- ArenaEnemyFrameTemplate is 102x32. The stock frames hang 20 apart to make
-- room for a pet frame under each; these have none, so they sit closer.
local FRAME_W, FRAME_H, FRAME_GAP = 102, 32, 8;
local HEADER_H, COLUMN_GAP = 16, 18;
-- Three columns of five (5v5v5v5) would take a quarter of the screen at full
-- size, so the whole block shrinks with the largest squad.
local SCALE_BY_SQUAD_SIZE = { [4] = 0.9, [5] = 0.8 };
local ANCHOR_X, ANCHOR_Y = -102, -25;

local CLASS_TOKENS = { "WARRIOR", "PALADIN", "HUNTER", "ROGUE", "PRIEST", "DEATHKNIGHT", "SHAMAN", "MAGE", "WARLOCK", nil, "DRUID" };

local state = {
	active = false,
	mine = nil,         -- your squad, nil for none (a spectator)
	count = 0,
	roster = {},        -- [squad] = { { name = , class = }, ... }
	team = {},          -- [squad] = arena team name, "" in a skirmish
	states = {},        -- [squad] = { token, ... } in roster order
	byName = {},        -- name -> squad, for the scoreboard
	winner = nil,       -- squad number, "-" for a draw
	pendingLayout = false,
};
CenturionSquadFrames_State = state;

-- ------------------------------------------------------------------ frames

-- Where the stock frames sit: ArenaEnemyFrames hangs TOPRIGHT off the minimap
-- cluster's BOTTOMRIGHT at (-100, -25) and its first frame 2 further in.
local container = CreateFrame("Frame", "CenturionSquadFrames", UIParent);
container:SetWidth(FRAME_W);
container:SetHeight(1);
container:SetPoint("TOPRIGHT", MinimapCluster or UIParent, "BOTTOMRIGHT", ANCHOR_X, ANCHOR_Y);
container:Hide();

local groups = {};

local function StatusBar(parent, x, y)
	local bar = CreateFrame("StatusBar", nil, parent);
	bar:SetStatusBarTexture("Interface\\TargetingFrame\\UI-StatusBar");
	bar:SetMinMaxValues(0, 100);
	bar:SetWidth(70);
	bar:SetHeight(8);
	bar:SetPoint("TOPLEFT", parent, "TOPLEFT", x, y);
	return bar;
end

-- One member: ArenaEnemyFrameTemplate rebuilt without the unit behind it.
local function NewMember(group, squad, index)
	local frame = CreateFrame("Button", "CenturionSquadFrame"..squad.."Member"..index, group, "SecureActionButtonTemplate");
	frame:SetWidth(FRAME_W);
	frame:SetHeight(FRAME_H);
	frame:RegisterForClicks("AnyUp");
	frame:SetAttribute("type1", "macro");
	frame:SetAttribute("type2", "macro");

	frame.portrait = frame:CreateTexture(nil, "BACKGROUND");
	frame.portrait:SetTexture("Interface\\TargetingFrame\\UI-Classes-Circles");
	frame.portrait:SetWidth(30);
	frame.portrait:SetHeight(30);
	frame.portrait:SetPoint("TOPRIGHT", frame, "TOPRIGHT", -1, -4);

	local background = frame:CreateTexture(nil, "BACKGROUND");
	background:SetTexture(0, 0, 0, 0.5);
	background:SetWidth(72);
	background:SetHeight(17);
	background:SetPoint("TOPLEFT", frame, "TOPLEFT", 2, -10);

	frame.health = StatusBar(frame, 2, -12);
	frame.mana = StatusBar(frame, 2, -20);

	-- The stock template nests its border two frames deep, which draws it -
	-- and the name and bar texts on it - over the bars.
	local art = CreateFrame("Frame", nil, frame);
	art:SetAllPoints(frame);
	art:SetFrameLevel(frame:GetFrameLevel() + 2);
	local border = art:CreateTexture(nil, "BACKGROUND");
	border:SetTexture("Interface\\ArenaEnemyFrame\\UI-ArenaTargetingFrame");
	border:SetWidth(FRAME_W);
	border:SetHeight(FRAME_H);
	border:SetPoint("TOPLEFT", frame, "TOPLEFT", 0, -2);
	border:SetTexCoord(0, 0.796, 0, 0.5);

	frame.name = art:CreateFontString(nil, "BACKGROUND", "GameFontNormalSmall");
	frame.name:SetJustifyH("LEFT");
	frame.name:SetPoint("BOTTOMLEFT", frame, "BOTTOMLEFT", 3, 24);

	frame.status = art:CreateFontString(nil, "BACKGROUND", "TextStatusBarText");
	frame.status:SetPoint("CENTER", frame, "CENTER", -20, 3);
	frame.manaText = art:CreateFontString(nil, "BACKGROUND", "TextStatusBarText");
	frame.manaText:SetPoint("CENTER", frame, "CENTER", -20, -6);

	frame:SetScript("OnEnter", function(self)
		self.hovered = true;
		CenturionSquadFrames_PaintSquad(squad);
	end);
	frame:SetScript("OnLeave", function(self)
		self.hovered = nil;
		CenturionSquadFrames_PaintSquad(squad);
	end);

	frame:Hide();
	return frame;
end

for squad = 0, MAX_SQUADS - 1 do
	local group = CreateFrame("Frame", "CenturionSquadFrame"..squad, container);
	group:SetWidth(FRAME_W);
	group:SetHeight(HEADER_H);
	local c = CENTURION_SQUAD_COLORS[squad];
	group.header = group:CreateFontString(nil, "OVERLAY", "GameFontNormalSmall");
	group.header:SetPoint("TOPLEFT", group, "TOPLEFT", 3, 0);
	group.header:SetJustifyH("LEFT");
	group.header:SetText(CENTURION_SQUAD_NAMES[squad]);
	group.header:SetTextColor(c.r, c.g, c.b);
	group.count = group:CreateFontString(nil, "OVERLAY", "GameFontNormalSmall");
	group.count:SetPoint("TOPRIGHT", group, "TOPRIGHT", -3, 0);
	group.count:SetJustifyH("RIGHT");
	group.count:SetTextColor(c.r, c.g, c.b);
	group.rows = {};
	for index = 1, MAX_MEMBERS do
		local frame = NewMember(group, squad, index);
		frame:SetPoint("TOPLEFT", group, "TOPLEFT", 0, -(HEADER_H + (index - 1) * (FRAME_H + FRAME_GAP)));
		group.rows[index] = frame;
	end
	group:Hide();
	groups[squad] = group;
end

-- The stock frames, put away for a squad match by parking them under a hidden
-- frame (they are secure, so only out of combat) and handed back after.
local stash = CreateFrame("Frame");
stash:Hide();

local function StashStock()
	if ( ArenaEnemyFrames and not InCombatLockdown() and ArenaEnemyFrames:GetParent() ~= stash ) then
		state.stockParent = ArenaEnemyFrames:GetParent();
		ArenaEnemyFrames:SetParent(stash);
	end
end

local function RestoreStock()
	if ( ArenaEnemyFrames and state.stockParent and not InCombatLockdown() ) then
		ArenaEnemyFrames:SetParent(state.stockParent);
		state.stockParent = nil;
	end
end

-- ------------------------------------------------------------------ painting

local function ParseState(token)
	if ( not token or token == "o" ) then
		return "away";
	elseif ( token == "d" ) then
		return "dead";
	elseif ( token == "s" ) then
		return "stealthed";
	end
	local health, power, powerType = strmatch(token, "^(%d+)%.(%d+)%.(%d+)$");
	if ( not health ) then
		return "away";
	end
	return "alive", tonumber(health), tonumber(power), tonumber(powerType);
end

local function PowerColor(powerType)
	local color = PowerBarColor and (PowerBarColor[powerType] or PowerBarColor["MANA"]);
	if ( color ) then
		return color.r, color.g, color.b;
	end
	return 0, 0, 1;
end

-- HealthBar_OnValueChanged's colour: green, through yellow, to red.
local function HealthColor(percent)
	local value = percent / 100;
	if ( value > 0.5 ) then
		return (1.0 - value) * 2, 1.0, 0.0;
	end
	return 1.0, value * 2, 0.0;
end

-- What the stock bars would print: their text only with the party status-text
-- option on (or the mouse over the frame), as a percentage.
local function ShowsNumbers(frame)
	return frame.hovered == true or (GetCVarBool ~= nil and GetCVarBool("partyStatusText") and true or false);
end

function CenturionSquadFrames_PaintSquad(squad)
	local group = groups[squad];
	if ( not group ) then
		return;
	end
	local roster = state.roster[squad] or {};
	local states = state.states[squad] or {};
	local standing = 0;

	for index = 1, MAX_MEMBERS do
		local frame = group.rows[index];
		local member = roster[index];
		if ( member ) then
			local token = CLASS_TOKENS[member.class];
			local coords = token and CLASS_ICON_TCOORDS and CLASS_ICON_TCOORDS[token];
			if ( coords ) then
				frame.portrait:SetTexture("Interface\\TargetingFrame\\UI-Classes-Circles");
				frame.portrait:SetTexCoord(coords[1], coords[2], coords[3], coords[4]);
			else
				frame.portrait:SetTexture("Interface\\CharacterFrame\\TempPortrait");
				frame.portrait:SetTexCoord(0, 1, 0, 1);
			end
			frame.name:SetText(member.name);

			local what, health, power, powerType = ParseState(states[index]);
			if ( what == "alive" ) then
				standing = standing + 1;
				frame.health:SetStatusBarColor(HealthColor(health));
				frame.health:SetValue(health);
				frame.mana:SetStatusBarColor(PowerColor(powerType));
				frame.mana:SetValue(power);
				if ( ShowsNumbers(frame) ) then
					frame.status:SetText(health.."%");
					frame.manaText:SetText(power.."%");
				else
					frame.status:SetText("");
					frame.manaText:SetText("");
				end
				frame.portrait:SetDesaturated(nil);
			else
				-- ArenaEnemyFrame_Lock: the bars go grey and keep what they last
				-- showed - all but a dead member's, which are empty.
				frame.health:SetStatusBarColor(0.5, 0.5, 0.5);
				frame.mana:SetStatusBarColor(0.5, 0.5, 0.5);
				frame.manaText:SetText("");
				if ( what == "dead" ) then
					frame.health:SetValue(0);
					frame.mana:SetValue(0);
					frame.status:SetText(CENTURION_SQUAD_DEAD);
					frame.portrait:SetDesaturated(1);
				elseif ( what == "stealthed" ) then
					standing = standing + 1;
					frame.status:SetText(CENTURION_SQUAD_STEALTHED);
					frame.portrait:SetDesaturated(nil);
				else
					frame.status:SetText(CENTURION_SQUAD_AWAY);
					frame.portrait:SetDesaturated(1);
				end
			end
		end
	end

	group.count:SetText(standing.."/"..#roster);
end

-- Which members exist and where they sit: secure, so out of combat only.
-- Enemy squads are columns, right to left from where the stock frames start.
function CenturionSquadFrames_Layout()
	if ( InCombatLockdown() ) then
		state.pendingLayout = true;
		return;
	end
	state.pendingLayout = false;

	if ( not state.active ) then
		for squad = 0, MAX_SQUADS - 1 do
			for index = 1, MAX_MEMBERS do
				groups[squad].rows[index]:SetAttribute("macrotext1", nil);
				groups[squad].rows[index]:SetAttribute("macrotext2", nil);
			end
		end
		container:Hide();
		RestoreStock();
		return;
	end

	-- Two teams (a Buffle Shuffle round) leave one enemy column, which is drawn
	-- the way the stock frames are: no team header over it.
	local headerH = state.count > 2 and HEADER_H or 0;

	local column, tallest, largest = 0, 0, 0;
	for squad = 0, MAX_SQUADS - 1 do
		local group = groups[squad];
		local roster = state.roster[squad];
		if ( squad ~= state.mine and squad < state.count and roster and #roster > 0 ) then
			local height = headerH + #roster * (FRAME_H + FRAME_GAP) - FRAME_GAP;
			group:ClearAllPoints();
			group:SetPoint("TOPRIGHT", container, "TOPRIGHT", -column * (FRAME_W + COLUMN_GAP), 0);
			group:SetHeight(height);
			if ( headerH > 0 ) then
				group.header:Show();
				group.count:Show();
			else
				group.header:Hide();
				group.count:Hide();
			end
			for index = 1, MAX_MEMBERS do
				local frame = group.rows[index];
				local member = roster[index];
				frame:ClearAllPoints();
				frame:SetPoint("TOPLEFT", group, "TOPLEFT", 0, -(headerH + (index - 1) * (FRAME_H + FRAME_GAP)));
				if ( member ) then
					frame:SetAttribute("macrotext1", "/targetexact "..member.name);
					frame:SetAttribute("macrotext2", "/focus "..member.name);
					frame:Show();
				else
					frame:SetAttribute("macrotext1", nil);
					frame:SetAttribute("macrotext2", nil);
					frame:Hide();
				end
			end
			group:Show();
			CenturionSquadFrames_PaintSquad(squad);
			column = column + 1;
			tallest = math.max(tallest, height);
			largest = math.max(largest, #roster);
		else
			group:Hide();
		end
	end

	-- Offsets are in the frame's own scaled units, so the anchor is divided by
	-- the scale to keep the corner where the stock frames start.
	local scale = SCALE_BY_SQUAD_SIZE[largest] or 1;
	container:SetScale(scale);
	container:ClearAllPoints();
	container:SetPoint("TOPRIGHT", MinimapCluster or UIParent, "BOTTOMRIGHT", ANCHOR_X / scale, ANCHOR_Y / scale);
	container:SetWidth(math.max(column * (FRAME_W + COLUMN_GAP) - COLUMN_GAP, 1));
	container:SetHeight(math.max(tallest, 1));
	container:Show();
	StashStock();
end

-- ------------------------------------------------------------------ scoreboard

-- After the stock update has coloured every row by side: recolour by squad.
function CenturionSquadFrames_ColourScoreboard()
	if ( not state.active or not WorldStateScoreFrame or not WorldStateScoreFrame:IsShown() ) then
		return;
	end

	local numScores = GetNumBattlefieldScores();
	local offset = FauxScrollFrame_GetOffset(WorldStateScoreScrollFrame);
	for i = 1, MAX_WORLDSTATE_SCORE_BUTTONS do
		if ( offset + i <= numScores ) then
			local nameButton = _G["WorldStateScoreButton"..i.."Name"];
			local squad = nameButton and nameButton.name and state.byName[nameButton.name];
			local c = squad and CENTURION_SQUAD_COLORS[squad];
			if ( c ) then
				_G["WorldStateScoreButton"..i.."FactionLeft"]:SetVertexColor(c.band[1], c.band[2], c.band[3]);
				_G["WorldStateScoreButton"..i.."FactionRight"]:SetVertexColor(c.band[1], c.band[2], c.band[3]);
				_G["WorldStateScoreButton"..i.."NameText"]:SetVertexColor(c.r, c.g, c.b);
				local teamText = _G["WorldStateScoreButton"..i.."Team"];
				if ( teamText and state.team[squad] and state.team[squad] ~= "" ) then
					teamText:SetText(state.team[squad]);
				end
			end
		end
	end

	if ( state.winner ~= nil and GetBattlefieldWinner() and WorldStateScoreWinnerFrame:IsShown() ) then
		local c = state.winner ~= "-" and CENTURION_SQUAD_COLORS[state.winner];
		if ( c ) then
			local team = state.team[state.winner];
			WorldStateScoreWinnerFrameText:SetFormattedText(VICTORY_TEXT_ARENA_WINS,
				(team and team ~= "") and team or CENTURION_SQUAD_NAMES[state.winner]);
			WorldStateScoreWinnerFrameLeft:SetVertexColor(c.band[1], c.band[2], c.band[3]);
			WorldStateScoreWinnerFrameRight:SetVertexColor(c.band[1], c.band[2], c.band[3]);
			WorldStateScoreWinnerFrameText:SetVertexColor(c.r, c.g, c.b);
		else
			WorldStateScoreWinnerFrameText:SetText(VICTORY_TEXT_ARENA_DRAW);
			WorldStateScoreWinnerFrameLeft:SetVertexColor(0.5, 0.5, 0.5);
			WorldStateScoreWinnerFrameRight:SetVertexColor(0.5, 0.5, 0.5);
			WorldStateScoreWinnerFrameText:SetVertexColor(1, 1, 1);
		end
	end
end

if ( type(WorldStateScoreFrame_Update) == "function" ) then
	hooksecurefunc("WorldStateScoreFrame_Update", CenturionSquadFrames_ColourScoreboard);
end

-- ------------------------------------------------------------------ the feed

local function Clear()
	state.active = false;
	state.mine = nil;
	state.count = 0;
	state.roster = {};
	state.team = {};
	state.states = {};
	state.byName = {};
	state.winner = nil;
	CenturionSquadFrames_Layout();
end

local function RebuildNames()
	local byName = {};
	for squad, roster in pairs(state.roster) do
		for _, member in ipairs(roster) do
			byName[member.name] = squad;
		end
	end
	state.byName = byName;
end

function CenturionSquadFrames_OnLine(line)
	if ( line == "SQ:-" ) then
		Clear();
		return;
	end

	local mine, count = strmatch(line, "^SQ:([%d%-]):(%d+)$");
	if ( mine ) then
		state.active = true;
		state.mine = tonumber(mine);
		state.count = math.min(tonumber(count) or 0, MAX_SQUADS);
		state.winner = nil;
		CenturionSquadFrames_Layout();
		return;
	end

	if ( not state.active ) then
		return;
	end

	local squad, team, members = strmatch(line, "^SQR:(%d):([^:]*):(.*)$");
	if ( squad ) then
		squad = tonumber(squad);
		local roster = {};
		for item in string.gmatch(members, "[^;]+") do
			local name, class = strmatch(item, "^(.+),(%d+)$");
			if ( name and #roster < MAX_MEMBERS ) then
				roster[#roster + 1] = { name = name, class = tonumber(class) };
			end
		end
		state.roster[squad] = roster;
		state.team[squad] = team;
		state.states[squad] = {};
		RebuildNames();
		CenturionSquadFrames_Layout();
		CenturionSquadFrames_PaintSquad(squad);
		CenturionSquadFrames_ColourScoreboard();
		return;
	end

	local statesSquad, states = strmatch(line, "^SQH:(%d):(.*)$");
	if ( statesSquad ) then
		statesSquad = tonumber(statesSquad);
		local list = {};
		for token in string.gmatch(states, "[^,]+") do
			list[#list + 1] = token;
		end
		state.states[statesSquad] = list;
		CenturionSquadFrames_PaintSquad(statesSquad);
		return;
	end

	local winner = strmatch(line, "^SQW:([%d%-])$");
	if ( winner ) then
		state.winner = (winner == "-") and "-" or tonumber(winner);
		CenturionSquadFrames_ColourScoreboard();
	end
end

local function Ask()
	local playerName = UnitName("player");
	if ( SendAddonMessage and playerName ) then
		SendAddonMessage("CCGAMEREQ", "SQUADS", "WHISPER", playerName);
	end
end

local driver = CreateFrame("Frame");
driver:RegisterEvent("PLAYER_ENTERING_WORLD");
driver:RegisterEvent("PLAYER_REGEN_ENABLED");
driver:RegisterEvent("ADDON_LOADED");
driver:RegisterEvent("CVAR_UPDATE");
driver:RegisterEvent("CHAT_MSG_ADDON");
driver:SetScript("OnEvent", function(self, event, arg1, arg2)
	if ( event == "CHAT_MSG_ADDON" ) then
		if ( arg1 == "CCGAME" and type(arg2) == "string" ) then
			CenturionSquadFrames_OnLine(arg2);
		end
	elseif ( event == "PLAYER_ENTERING_WORLD" ) then
		local _, instanceType = IsInInstance();
		if ( instanceType == "arena" ) then
			Ask();
		elseif ( state.active ) then
			Clear();
		end
	elseif ( event == "PLAYER_REGEN_ENABLED" ) then
		if ( state.pendingLayout ) then
			CenturionSquadFrames_Layout();
		end
	elseif ( event == "CVAR_UPDATE" ) then
		-- The party status-text option, flipped in Interface Options.
		if ( state.active and arg1 == "STATUS_TEXT_PARTY" ) then
			for squad = 0, MAX_SQUADS - 1 do
				CenturionSquadFrames_PaintSquad(squad);
			end
		end
	elseif ( event == "ADDON_LOADED" ) then
		-- The stock frames load on demand, after the match may have started.
		if ( arg1 == "Blizzard_ArenaUI" and state.active ) then
			StashStock();
		end
	end
end);
