-- lua_run.py timer_test.lua ADDON=<CENTURION_GurubashiTimer.lua>
-- Mocks just the client API the addon touches and walks it through a chest hour.
local results, fails = {}, 0
local function check(name, cond)
	if cond then
		results[#results + 1] = "ok   " .. name
	else
		fails = fails + 1
		results[#results + 1] = "FAIL " .. name
	end
end

local now = 100
function GetTime() return now end
function UnitName() return "Tester" end
local sent = {}
function SendAddonMessage(prefix, msg, channel, target) sent[#sent + 1] = { prefix, msg, channel, target } end

local function newFontString()
	local fs = { shown = false }
	function fs:SetPoint(...) self.point = { ... } end
	function fs:Hide() self.shown = false end
	function fs:Show() self.shown = true end
	function fs:SetText(t) self.text = t end
	function fs:SetTextColor(r, g, b) self.color = { r, g, b } end
	return fs
end

local created
local function newFrame()
	local f = { scripts = {}, events = {}, hooks = {} }
	function f:SetScript(name, fn) self.scripts[name] = fn end
	function f:HookScript(name, fn) self.hooks[name] = fn end
	function f:RegisterEvent(e) self.events[e] = true end
	function f:CreateFontString(name, layer, template) self.fs = newFontString(); self.fs.template = template; return self.fs end
	return f
end
function CreateFrame(kind, name, parent)
	created = newFrame()
	created.parent = parent
	return created
end

PVPBattlegroundFrame = newFrame()
PVPBattlegroundFrameGurubashiChest = newFrame()
PVPBattlegroundFrameGurubashiChestText = newFontString()
CENTURION_GURUBASHI_CHEST_TOOLTIP = "Gurubashi chest"

local chunk = assert(loadfile(ADDON))
chunk()
local timer = PVPBattlegroundFrameGurubashiChest.fs
local driver = created
local function event(prefix, message) driver.scripts.OnEvent(driver, "CHAT_MSG_ADDON", prefix, message, "WHISPER", "Tester") end
local function tick(t, elapsed) now = t; driver.scripts.OnUpdate(driver, elapsed or 0.3) end

check("timer created on the checkbox, hidden", timer ~= nil and not timer.shown and timer.point[2] == PVPBattlegroundFrameGurubashiChestText)
check("driver is a child of the tab", driver ~= nil and driver.parent == PVPBattlegroundFrame and driver.events.CHAT_MSG_ADDON)
check("tooltip extended", CENTURION_GURUBASHI_CHEST_TOOLTIP:find("timer counts down", 1, true) ~= nil)

PVPBattlegroundFrame.hooks.OnShow()
check("asks when the tab opens", #sent == 1 and sent[1][1] == "CCGAMEREQ" and sent[1][2] == "GURUTIMER" and sent[1][3] == "WHISPER" and sent[1][4] == "Tester")

tick(100.5)
check("nothing shown before an answer", not timer.shown)

event("CCGAME", "GURUCHEST:1")
check("ignores the toggle's own answer", not timer.shown)
event("OTHER", "GURUTIMER:0:5")
check("ignores other prefixes", not timer.shown)

event("CCGAME", "GURUTIMER:0:125")
check("next chest shown", timer.shown and timer.text == "Next chest 2:05" and timer.color[1] == 1.0)

tick(110)
-- the answer came at 100.5, so 125s run out at 225.5
check("counts down", timer.text == "Next chest 1:56")
check("no extra request inside 30s", #sent == 1)

tick(110.1, 0.1)
check("refresh is throttled", timer.text == "Next chest 1:56")

tick(131)
check("asks again every 30s", #sent == 2)

tick(226)
check("stops at zero", timer.text == "Next chest 0:00")
check("asks when the clock runs out", #sent == 3)
tick(227)
check("no request storm while the server catches up", #sent == 3)
tick(229.5)
check("retries after 3s", #sent == 4)

event("CCGAME", "GURUTIMER:1:300")
check("chest up shown in green", timer.text == "Chest up 5:00" and timer.color[2] == 1.0 and timer.color[1] < 0.5)
tick(289.5)
check("chest countdown", timer.text == "Chest up 4:00")

event("CCGAME", "GURUTIMER:0:3500")
check("back to the hourly clock", timer.text == "Next chest 58:20" and timer.color[1] == 1.0)

-- A client whose Battlegrounds tab has no chest checkbox loads it silently.
PVPBattlegroundFrameGurubashiChest = nil
local before = #sent
local ok = pcall(chunk)
check("no checkbox: no error, nothing sent", ok and #sent == before)

OUT = table.concat(results, "\n") .. "\nfail=" .. fails
