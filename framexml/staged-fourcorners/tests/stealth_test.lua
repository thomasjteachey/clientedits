-- Loads the real CenturionArenaStealth.lua (path in MODULE) and the stock
-- TextStatusBar_UpdateTextString logic (inlined below, from patch-enUS's
-- TextStatusBar.lua) and walks opponents through what the client fires:
-- stealth arrives as "destroyed" (SMSG_ARENA_UNIT_DESTROYED), not "unseen".
local fail, pass, log = 0, 0, {}
local function check(cond, what)
	if cond then pass = pass + 1 else fail = fail + 1; log[#log + 1] = "FAIL " .. what end
end

strmatch = string.match
DEAD = "Dead"
local cvars = { statusTextPercentage = "0" }
function GetCVarBool(n) return cvars[n] == "1" end
function GetCVar(n) return cvars[n] end

-- stock, verbatim in the parts this exercises
function TextStatusBar_UpdateTextString(bar)
	local textString = bar.TextString
	if textString then
		local value = bar:GetValue()
		local _, valueMax = bar:GetMinMaxValues()
		if (tonumber(valueMax) ~= valueMax or valueMax > 0) and not bar.pauseUpdates then
			if value == 0 and bar.zeroText then
				textString:SetText(bar.zeroText); bar.isZero = 1; return
			end
			bar.isZero = nil
			textString:SetText(value .. " / " .. valueMax)
		end
	end
end
function SetTextStatusBarTextZeroText(bar, zeroText) if bar and bar.TextString then bar.zeroText = zeroText end end

-- the units the client knows about
local units = {}
function UnitExists(u) return units[u] ~= nil end
function UnitIsDeadOrGhost(u) return units[u] and units[u].dead or false end

local locked = {}
function ArenaEnemyFrame_Lock(f) locked[f.unit] = (locked[f.unit] or 0) + 1 end

local hooks = {}
function hooksecurefunc(name, fn)
	local orig = _G[name]
	hooks[#hooks + 1] = name
	_G[name] = function(...) local r = orig(...); fn(...); return r end
end

local loaded = false
function IsAddOnLoaded(n) return loaded and n == "Blizzard_ArenaUI" end
local driver
function CreateFrame()
	local f = { events = {} }
	function f:RegisterEvent(e) self.events[e] = true end
	function f:UnregisterEvent(e) self.events[e] = nil end
	function f:SetScript(_, fn) self.onEvent = fn end
	driver = f
	return f
end
local function fire(event, arg) driver.onEvent(driver, event, arg) end

local function newFrame(unit)
	local text = {}
	function text:SetText(t) self.text = t end
	local bar = { TextString = text, value = 100, max = 100 }
	function bar:GetValue() return self.value end
	function bar:GetMinMaxValues() return 0, self.max end
	SetTextStatusBarTextZeroText(bar, DEAD)
	return { unit = unit, healthbar = bar, text = text }
end
local a1, a2 = newFrame("arena1"), newFrame("arena2")

-- The stock handler, as far as this matters: it redraws the bar from the unit,
-- which reads 0 once the unit is gone.
local stockCalls = 0
function ArenaEnemyFrame_OnEvent(self, event, unit, state)
	stockCalls = stockCalls + 1
	if event == "ARENA_OPPONENT_UPDATE" and unit == self.unit then
		self.healthbar.value = units[unit] and units[unit].hp or 0
		TextStatusBar_UpdateTextString(self.healthbar)
	end
end
local function opp(f, state) ArenaEnemyFrame_OnEvent(f, "ARENA_OPPONENT_UPDATE", f.unit, state) end

assert(loadfile(MODULE))()
check(#hooks == 0, "no hook before Blizzard_ArenaUI loads")
check(driver.events.UNIT_HEALTH and driver.events.PLAYER_ENTERING_WORLD, "watches health and zoning")
loaded = true
fire("ADDON_LOADED", "Blizzard_ArenaUI")
check(#hooks == 1 and hooks[1] == "ArenaEnemyFrame_OnEvent", "hooked on ADDON_LOADED")
fire("ADDON_LOADED", "Blizzard_ArenaUI")
check(#hooks == 1, "hooked once")

-- a druid at full health, then Prowl: the server destroys the unit for us
units.arena1 = { hp = 100, dead = false }
opp(a1, "seen")
check(stockCalls == 1, "stock handler still runs")
check(a1.text.text == "100 / 100", "seen: health shown: " .. tostring(a1.text.text))
units.arena1 = nil
opp(a1, "destroyed")
check(a1.text.text == "Stealthed", "stealth (destroyed) reads Stealthed: " .. tostring(a1.text.text))
check(locked.arena1 == 1, "and is greyed like the stock unseen")
TextStatusBar_UpdateTextString(a1.healthbar)
check(a1.text.text == "Stealthed", "stays Stealthed on redraw")

-- out of stealth
units.arena1 = { hp = 70, dead = false }
opp(a1, "seen")
check(a1.text.text == "70 / 100", "seen again: health: " .. tostring(a1.text.text))
check(a1.healthbar.zeroText == "Dead", "zero text back to Dead")

-- another slot's events leave this one alone
units.arena2 = { hp = 100, dead = false }
opp(a2, "seen")
units.arena2 = nil
opp(a2, "destroyed")
check(a2.text.text == "Stealthed" and a1.healthbar.zeroText == "Dead", "per slot")

-- killed, then gone from sight (kept dead, or a spectator): stays Dead
units.arena1 = { hp = 0, dead = true }
fire("UNIT_HEALTH", "arena1")
units.arena1 = nil
opp(a1, "destroyed")
check(a1.text.text == "Dead", "dead and destroyed stays Dead: " .. tostring(a1.text.text))
check(locked.arena1 == 1, "and is not greyed as stealthed")

-- a slot handed out to someone already out of sight
local a3 = newFrame("arena3")
opp(a3, "unseen")
check(a3.text.text == "Stealthed", "unseen reads Stealthed")

-- cleared, and a new zone, forget everything
opp(a1, "cleared")
check(a1.healthbar.zeroText == "Dead", "cleared restores Dead")
units.arena2 = { hp = 0, dead = true }
fire("UNIT_HEALTH", "arena2")
fire("PLAYER_ENTERING_WORLD")
units.arena2 = nil
opp(a2, "destroyed")
check(a2.text.text == "Stealthed", "a new zone forgets who was dead")

ArenaEnemyFrame_OnEvent(a1, "UNIT_NAME_UPDATE", "arena1")
check(a1.healthbar.zeroText == "Dead", "other events ignored")
fire("UNIT_HEALTH", "player")
check(true, "non-arena units ignored")

OUT = table.concat(log, "\n") .. (#log > 0 and "\n" or "") .. string.format("pass=%d fail=%d", pass, fail)
