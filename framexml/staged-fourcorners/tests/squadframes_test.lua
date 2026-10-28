-- Loads the real CenturionSquadFrames.lua (path in MODULE) against a mocked
-- frame API and drives it with the server's feed lines.
local fail, pass, log = 0, 0, {}
local function check(cond, what)
	if cond then pass = pass + 1 else fail = fail + 1; log[#log + 1] = "FAIL " .. what end
end

strmatch = string.match
DEAD = "Dead"
VICTORY_TEXT_ARENA_WINS = "%s Wins!"
VICTORY_TEXT_ARENA_DRAW = "Draw"
MAX_WORLDSTATE_SCORE_BUTTONS = 3
RAID_CLASS_COLORS = { WARRIOR = { r = 0.78, g = 0.61, b = 0.43 }, MAGE = { r = 0.41, g = 0.8, b = 0.94 }, ROGUE = { r = 1, g = 0.96, b = 0.41 } }
CLASS_ICON_TCOORDS = { WARRIOR = { 0, 0.25, 0, 0.25 }, MAGE = { 0.25, 0.5, 0, 0.25 }, ROGUE = { 0.5, 0.75, 0, 0.25 } }
PowerBarColor = { MANA = { r = 0, g = 0, b = 1 }, [0] = { r = 0, g = 0, b = 1 }, [1] = { r = 1, g = 0, b = 0 }, [3] = { r = 1, g = 1, b = 0 } }

local cvars = { partyStatusText = "0" }
function GetCVarBool(n) return cvars[n] == "1" end
local combat = false
function InCombatLockdown() return combat end
local instanceType = "arena"
function IsInInstance() return true, instanceType end
function UnitName() return "Me" end
local sent = {}
function SendAddonMessage(prefix, msg, kind, to) sent[#sent + 1] = prefix .. "|" .. msg .. "|" .. kind .. "|" .. to end
local hooks = {}
function hooksecurefunc(name, fn)
	local orig = _G[name]
	hooks[name] = true
	_G[name] = function(...) local r = orig(...); fn(...); return r end
end

local MOCK = {}
local function obj(kind, name)
	local o = { kind = kind, name = name, shown = true, attrs = {}, events = {} }
	return setmetatable(o, { __index = function(_, k) return MOCK[k] or function() end end })
end
function MOCK.Hide(s) s.shown = false end
function MOCK.Show(s) s.shown = true end
function MOCK.IsShown(s) return s.shown end
function MOCK.SetAttribute(s, k, v) s.attrs[k] = v end
function MOCK.GetAttribute(s, k) return s.attrs[k] end
function MOCK.SetText(s, t) s.text = t end
function MOCK.GetText(s) return s.text end
function MOCK.SetFormattedText(s, f, ...) s.text = string.format(f, ...) end
function MOCK.SetValue(s, v) s.value = v end
function MOCK.SetStatusBarColor(s, r, g, b) s.color = { r, g, b } end
function MOCK.SetTextColor(s, r, g, b) s.tcolor = { r, g, b } end
function MOCK.SetVertexColor(s, r, g, b) s.vcolor = { r, g, b } end
function MOCK.SetParent(s, p) s.parent = p end
function MOCK.GetParent(s) return s.parent end
function MOCK.SetHeight(s, h) s.height = h end
function MOCK.SetScript(s, k, f) local t = rawget(s, "scripts") or {}; t[k] = f; rawset(s, "scripts", t) end
function MOCK.RegisterEvent(s, e) s.events[e] = true end
function MOCK.CreateTexture() return obj("Texture") end
function MOCK.GetFrameLevel() return 1 end
function MOCK.SetPoint(s, ...) s.point = { ... } end
function MOCK.SetScale(s, v) s.scale = v end
function MOCK.CreateFontString() return obj("FontString") end

local frames = {}
function CreateFrame(kind, name, parent, template)
	local f = obj(kind, name)
	f.parent = parent
	f.template = template
	if name then _G[name] = f end
	frames[#frames + 1] = f
	return f
end
UIParent = obj("Frame", "UIParent")
MinimapCluster = obj("Frame", "MinimapCluster")

-- the scoreboard bits the hook reads
WorldStateScoreFrame = obj("Frame", "WorldStateScoreFrame")
WorldStateScoreScrollFrame = obj("Frame")
WorldStateScoreWinnerFrame = obj("Frame")
WorldStateScoreWinnerFrameText = obj("FontString")
WorldStateScoreWinnerFrameLeft = obj("Texture")
WorldStateScoreWinnerFrameRight = obj("Texture")
local scoreNames = { "Alpha", "Delta", "Me" }
for i = 1, 3 do
	_G["WorldStateScoreButton" .. i .. "Name"] = { name = scoreNames[i] }
	_G["WorldStateScoreButton" .. i .. "NameText"] = obj("FontString")
	_G["WorldStateScoreButton" .. i .. "FactionLeft"] = obj("Texture")
	_G["WorldStateScoreButton" .. i .. "FactionRight"] = obj("Texture")
	_G["WorldStateScoreButton" .. i .. "Team"] = obj("FontString")
end
function GetNumBattlefieldScores() return 3 end
function FauxScrollFrame_GetOffset() return 0 end
local winner = nil
function GetBattlefieldWinner() return winner end
local stockUpdates = 0
function WorldStateScoreFrame_Update() stockUpdates = stockUpdates + 1 end
-- the Wins column (Buffle Shuffle) borrows the rating-change column
MOCK.SetWidth = function(s, w) s.width = w end
MOCK.GetWidth = function(s) return s.width or 0 end
MOCK.SetTexCoord = function() end
WORLDSTATESCOREFRAME_COLUMN_SPACING = 76
WorldStateScoreFrameHonorGained = obj("Frame")
WorldStateScoreFrameHonorGainedText = obj("FontString")
WorldStateScoreFrameKB = obj("Frame")
-- the order the two headers are re-anchored in (Killing Blows first would loop)
local anchorLog = {}
for _, f in ipairs({ { WorldStateScoreFrameHonorGained, "RoundsWon" }, { WorldStateScoreFrameKB, "KB" } }) do
	rawset(f[1], "SetPoint", function(s, ...) s.point = { ... }; anchorLog[#anchorLog + 1] = f[2] end)
end
WorldStateScoreFrameTopBackground = obj("Texture")
for i = 1, 3 do
	_G["WorldStateScoreButton" .. i] = obj("Button")
	_G["WorldStateScoreButton" .. i .. "HonorGained"] = obj("FontString")
	_G["WorldStateScoreButton" .. i .. "HonorGained"].shown = false
end
local arenaRegistered = false
function IsActiveBattlefieldArena() return true, arenaRegistered end
local resizes = 0
function WorldStateScoreFrame_Resize(width) resizes = resizes + 1; WorldStateScoreFrame:SetWidth(width or 400); return width or 400 end

assert(loadfile(MODULE))()
check(hooks.WorldStateScoreFrame_Update, "scoreboard hooked")

local driver
for _, f in ipairs(frames) do
	local scripts = rawget(f, "scripts")
	if f.events.CHAT_MSG_ADDON and scripts and scripts.OnEvent then driver = f end
end
check(driver ~= nil, "driver frame found")
local function fire(event, a1, a2) driver.scripts.OnEvent(driver, event, a1, a2) end
local function line(text) fire("CHAT_MSG_ADDON", "CCGAME", text, "WHISPER", "Me") end

local container = CenturionSquadFrames
local function row(s, i) return _G["CenturionSquadFrame" .. s .. "Member" .. i] end
local function group(s) return _G["CenturionSquadFrame" .. s] end
check(container and not container.shown, "container starts hidden")
check(row(0, 1).template == "SecureActionButtonTemplate" and row(0, 1).attrs.type1 == "macro", "rows are secure macro buttons")

-- entering an arena asks for the picture
fire("PLAYER_ENTERING_WORLD")
check(sent[1] == "CCGAMEREQ|SQUADS|WHISPER|Me", "asks on entering an arena: " .. tostring(sent[1]))

-- the stock frames exist before the match
ArenaEnemyFrames = obj("Frame", "ArenaEnemyFrames")
ArenaEnemyFrames.parent = UIParent

line("SQ:1:4")
check(container.shown, "SQ shows the container")
check(ArenaEnemyFrames.parent ~= UIParent, "stock frames put away")

line("SQR:0::Alpha,1;Bravo,8")
line("SQR:1::Me,4;Charlie,4")
line("SQR:2:Team Two:Delta,4")
line("SQR:3::")
check(group(0).shown and not group(1).shown, "enemy squad shown, own squad not")
check(group(2).shown and not group(3).shown, "squad 2 shown, empty squad 3 not")
check(row(0, 1).shown and row(0, 2).shown and not row(0, 3).shown, "rows for the roster only")
check(row(0, 1).attrs.macrotext1 == "/targetexact Alpha", "row targets by name: " .. tostring(row(0, 1).attrs.macrotext1))
check(row(2, 1).attrs.macrotext1 == "/targetexact Delta", "squad 2 row targets Delta")
check(group(0).height == 16 + 2 * 40 - 8, "group height fits two frames: " .. tostring(group(0).height))
check(container.scale == 1, "full size for small squads: " .. tostring(container.scale))
check(row(0, 1).attrs.macrotext2 == "/focus Alpha", "right click focuses")
check(group(0).point[4] == 0 and group(2).point[4] == -120, "one column per enemy squad, right to left: " .. tostring(group(2).point[4]))

line("SQH:0:87.45.1,d")
check(row(0, 1).status.text == "" and row(0, 1).health.value == 87, "no numbers with party status text off: " .. tostring(row(0, 1).status.text))
check(row(0, 1).mana.value == 45 and row(0, 1).mana.color[1] == 1, "rage bar")
check(row(0, 1).health.color[2] == 1 and math.abs(row(0, 1).health.color[1] - 0.26) < 0.001, "stock green-to-red health colour")
row(0, 1).scripts.OnEnter(row(0, 1))
check(row(0, 1).status.text == "87%" and row(0, 1).manaText.text == "45%", "numbers on mouseover: " .. tostring(row(0, 1).status.text))
row(0, 1).scripts.OnLeave(row(0, 1))
check(row(0, 1).status.text == "", "and gone again")
cvars.partyStatusText = "1"
fire("CVAR_UPDATE", "STATUS_TEXT_PARTY", "1")
check(row(0, 1).status.text == "87%", "numbers with party status text on")
check(row(0, 2).status.text == "Dead" and row(0, 2).health.value == 0, "dead member: " .. tostring(row(0, 2).status.text))
check(group(0).count.text == "1/2", "squad count: " .. tostring(group(0).count.text))

line("SQH:2:s")
check(row(2, 1).status.text == "Stealthed", "stealthed member: " .. tostring(row(2, 1).status.text))
check(group(2).count.text == "1/1", "stealthed still counts as standing")
line("SQH:2:o")
check(row(2, 1).status.text == "Away", "member not here")

-- a roster change mid-fight waits for combat to end
combat = true
line("SQR:2:Team Two:Delta,4;Echo,8")
check(not row(2, 2).shown and CenturionSquadFrames_State.pendingLayout, "layout deferred in combat")
line("SQH:2:50.100.0,60.20.0")
check(row(2, 1).status.text == "50%" and row(2, 1).health.value == 50, "painting goes on in combat")
combat = false
fire("PLAYER_REGEN_ENABLED")
check(row(2, 2).shown and row(2, 2).attrs.macrotext1 == "/targetexact Echo", "laid out after combat")

-- a squad of five shrinks the whole block, anchor kept where the stock frames start
line("SQR:3::Foxtrot,1;Golf,2;Hotel,3;India,5;Juliet,9")
check(math.abs(container.scale - 0.8) < 1e-9, "80% with a squad of five: " .. tostring(container.scale))
check(math.abs(container.point[4] * container.scale + 102) < 1e-9 and math.abs(container.point[5] * container.scale + 25) < 1e-9, "anchor compensated for the scale")
check(group(3).shown and row(3, 5).shown and row(3, 5).attrs.macrotext1 == "/targetexact Juliet", "fifth member laid out")
line("SQR:3::")
check(container.scale == 1, "back to full size")

-- scoreboard: the stock update runs, then each row takes its squad's colour
WorldStateScoreFrame.shown = true
WorldStateScoreFrame_Update()
check(stockUpdates == 1, "stock scoreboard update ran")
local c0 = CENTURION_SQUAD_COLORS[0]
check(WorldStateScoreButton1FactionLeft.vcolor[1] == c0.band[1], "Alpha's row in gold")
check(WorldStateScoreButton1NameText.vcolor[3] == c0.b, "Alpha's name in gold")
check(WorldStateScoreButton2FactionLeft.vcolor[3] == CENTURION_SQUAD_COLORS[2].band[3], "Delta's row in blue")
check(WorldStateScoreButton2Team.text == "Team Two", "rated squad's team name in the team column")
check(WorldStateScoreButton3NameText.vcolor[2] == CENTURION_SQUAD_COLORS[1].g, "own row in its squad's colour")

-- winner banner
winner = 0
WorldStateScoreWinnerFrame.shown = true
line("SQW:2")
check(WorldStateScoreWinnerFrameText.text == "Team Two Wins!", "winner banner names the squad's team: " .. tostring(WorldStateScoreWinnerFrameText.text))
check(WorldStateScoreWinnerFrameLeft.vcolor[3] == CENTURION_SQUAD_COLORS[2].band[3], "winner banner in its colour")
line("SQW:-")
check(WorldStateScoreWinnerFrameText.text == "Draw", "draw")

-- leaving: everything goes, and the stock frames come back
line("SQ:-")
check(not container.shown, "cleared")
check(ArenaEnemyFrames.parent == UIParent, "stock frames handed back")
check(row(0, 1).attrs.macrotext1 == nil, "targets cleared")
line("SQH:0:10.10.0")
check(row(0, 1).status.text ~= "10%", "lines ignored while inactive")

-- the stock UI loading late is put away too
ArenaEnemyFrames.parent = UIParent
line("SQ:0:2")
ArenaEnemyFrames = obj("Frame", "ArenaEnemyFrames"); ArenaEnemyFrames.parent = UIParent
fire("ADDON_LOADED", "Blizzard_ArenaUI")
check(ArenaEnemyFrames.parent ~= UIParent, "late-loaded stock frames put away")

-- two teams (Solo Shuffle): one enemy column, no header, frames from the top
line("SQ:-")
line("SQ:0:2")
line("SQR:0::Me,4;Kilo,5;Lima,8")
line("SQR:1::Mike,1;November,2;Oscar,3")
check(group(1).shown and not group(0).shown, "two teams: the enemy column only")
check(group(1).header.shown == false and group(1).count.shown == false, "two teams: no team header")
check(row(1, 1).point[5] == 0 and row(1, 2).point[5] == -40, "two teams: frames start at the top: " .. tostring(row(1, 1).point[5]))
check(group(1).height == 3 * 40 - 8, "two teams: column height without a header: " .. tostring(group(1).height))
-- and back to four: headers return
line("SQ:-")
line("SQ:0:4")
line("SQR:2::Papa,1")
check(group(2).header.shown == true and row(2, 1).point[5] == -16, "four teams: header back, frames under it")

-- Buffle Shuffle: SQV feeds each member's rounds won into the scoreboard's Rounds Won column
line("SQ:-")
line("SQ:0:2")
WorldStateScoreFrame.shown = true
WorldStateScoreFrameHonorGained.shown = false
WorldStateScoreFrame_Update()
check(not WorldStateScoreFrameHonorGained.shown, "no Rounds Won column before any wins arrive")
check(#anchorLog == 0, "columns left where they are before any wins arrive")
line("SQR:0::Alpha,1;Me,4")
line("SQR:1::Delta,4")
resizes = 0
line("SQV:0:3,2")
check(resizes == 1 and WorldStateScoreFrame.width == 400 + 76, "first wins line widens the frame by a column: " .. tostring(WorldStateScoreFrame.width))
check(WorldStateScoreButton1.width == 400 + 76 - 165, "rows widened with it: " .. tostring(WorldStateScoreButton1.width))
line("SQV:1:1")
check(resizes == 1, "later wins lines do not resize again")
anchorLog = {}
WorldStateScoreFrameHonorGained.sortType = "cp"
WorldStateScoreFrame_Update()
check(WorldStateScoreFrameHonorGained.shown and WorldStateScoreFrameHonorGainedText.text == "Rounds\nWon", "Rounds Won header shown: " .. tostring(WorldStateScoreFrameHonorGainedText.text))
local rw, kb = rawget(WorldStateScoreFrameHonorGained, "point"), rawget(WorldStateScoreFrameKB, "point")
check(rw and rw[1] == "LEFT" and rw[2] == "WorldStateScoreFrameName" and rw[3] == "RIGHT" and rw[4] == 4 and rw[5] == 0, "Rounds Won right after the names")
check(kb and kb[1] == "LEFT" and kb[2] == "WorldStateScoreFrameHonorGained" and kb[3] == "RIGHT" and kb[4] == -5 and kb[5] == 0, "Killing Blows right after Rounds Won")
check(anchorLog[1] == "RoundsWon" and anchorLog[2] == "KB", "Rounds Won moved before Killing Blows hangs from it: " .. table.concat(anchorLog, ","))
check(rawget(WorldStateScoreFrameHonorGained, "sortType") == nil, "Rounds Won header does not sort by bonus honor")
check(WorldStateScoreButton1HonorGained.text == "3" and WorldStateScoreButton1HonorGained.shown, "Alpha 3 wins: " .. tostring(WorldStateScoreButton1HonorGained.text))
check(WorldStateScoreButton2HonorGained.text == "1", "Delta 1 win: " .. tostring(WorldStateScoreButton2HonorGained.text))
check(WorldStateScoreButton3HonorGained.text == "2", "Me 2 wins: " .. tostring(WorldStateScoreButton3HonorGained.text))
-- a new round re-deals the rosters: wins follow the new order once SQV resends
line("SQR:0::Me,4;Delta,4")
line("SQR:1::Alpha,1")
check(CenturionSquadFrames_State.winsByName == nil, "roster change drops wins until resent")
line("SQV:0:3,2")
line("SQV:1:4")
WorldStateScoreFrame_Update()
check(WorldStateScoreButton1HonorGained.text == "4" and WorldStateScoreButton3HonorGained.text == "3" and WorldStateScoreButton2HonorGained.text == "2", "wins re-paired by name after a re-deal")
-- a rated arena keeps its rating column
arenaRegistered = true
WorldStateScoreFrameHonorGainedText.text = "Rating"
anchorLog = {}
WorldStateScoreFrame_Update()
check(WorldStateScoreFrameHonorGainedText.text == "Rating", "rated arena untouched")
check(#anchorLog == 0, "rated arena columns not moved")
arenaRegistered = false
WorldStateScoreFrame_Resize()
check(WorldStateScoreFrame.width == 476, "resize keeps the extra column")
line("SQ:-")
WorldStateScoreFrame_Resize()
check(WorldStateScoreFrame.width == 400, "no extra column once the shuffle is over")
WorldStateScoreFrame.shown = false

-- outside an arena the frames clear themselves
instanceType = "none"
fire("PLAYER_ENTERING_WORLD")
check(not container.shown, "cleared outside an arena")

OUT = table.concat(log, "\n") .. (#log > 0 and "\n" or "") .. string.format("pass=%d fail=%d", pass, fail)
