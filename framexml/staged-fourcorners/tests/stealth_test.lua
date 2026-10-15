-- Loads the real CenturionArenaStealth.lua (path in MODULE) and the stock
-- TextStatusBar_UpdateTextString logic (inlined below, from patch-enUS's
-- TextStatusBar.lua) and walks one opponent through seen -> unseen -> seen.
local fail, pass, log = 0, 0, {}
local function check(cond, what)
	if cond then pass = pass + 1 else fail = fail + 1; log[#log + 1] = "FAIL " .. what end
end

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

local hooks = {}
function hooksecurefunc(name, fn)
	local orig = _G[name]
	hooks[#hooks + 1] = name
	_G[name] = function(...) local r = orig(...); fn(...); return r end
end

local loaded = false
function IsAddOnLoaded(n) return loaded and n == "Blizzard_ArenaUI" end
local loaderFrame
function CreateFrame()
	local f = { events = {} }
	function f:RegisterEvent(e) self.events[e] = true end
	function f:UnregisterEvent(e) self.events[e] = nil end
	function f:SetScript(_, fn) self.onEvent = fn end
	loaderFrame = f
	return f
end

-- the stock unit-frame bits the hook touches
local text = { text = nil }
function text:SetText(t) self.text = t end
local bar = { TextString = text, value = 100, max = 100 }
function bar:GetValue() return self.value end
function bar:GetMinMaxValues() return 0, self.max end
SetTextStatusBarTextZeroText(bar, DEAD)
local frame = { unit = "arena1", healthbar = bar }

-- the stock handler (stand-in: it only has to exist and be called first)
local stockCalls = 0
function ArenaEnemyFrame_OnEvent(self, event, a1, a2) stockCalls = stockCalls + 1 end

assert(loadfile(MODULE))()
check(#hooks == 0, "no hook before Blizzard_ArenaUI loads")
loaded = true
loaderFrame.onEvent(loaderFrame, "ADDON_LOADED", "Blizzard_ArenaUI")
check(#hooks == 1 and hooks[1] == "ArenaEnemyFrame_OnEvent", "hooked on ADDON_LOADED")
loaderFrame.onEvent(loaderFrame, "ADDON_LOADED", "Blizzard_ArenaUI")
check(#hooks == 1, "hooked once")

ArenaEnemyFrame_OnEvent(frame, "ARENA_OPPONENT_UPDATE", "arena1", "seen")
check(stockCalls == 1, "stock handler still runs")
check(text.text == "100 / 100", "seen at full health shows health: " .. tostring(text.text))

bar.value = 0
ArenaEnemyFrame_OnEvent(frame, "ARENA_OPPONENT_UPDATE", "arena1", "unseen")
check(text.text == "Stealthed", "unseen reads Stealthed: " .. tostring(text.text))
TextStatusBar_UpdateTextString(bar)      -- a later UNIT_HEALTH redraw keeps it
check(text.text == "Stealthed", "stays Stealthed on redraw: " .. tostring(text.text))

ArenaEnemyFrame_OnEvent(frame, "ARENA_OPPONENT_UPDATE", "arena2", "seen")
check(text.text == "Stealthed", "another slot's event leaves this one alone")

ArenaEnemyFrame_OnEvent(frame, "ARENA_OPPONENT_UPDATE", "arena1", "seen")
check(text.text == "Dead", "seen again at 0 health is Dead: " .. tostring(text.text))
bar.value = 80
TextStatusBar_UpdateTextString(bar)
check(text.text == "80 / 100", "and health once it has some: " .. tostring(text.text))

ArenaEnemyFrame_OnEvent(frame, "ARENA_OPPONENT_UPDATE", "arena1", "unseen")
ArenaEnemyFrame_OnEvent(frame, "ARENA_OPPONENT_UPDATE", "arena1", "cleared")
check(bar.zeroText == "Dead", "cleared restores Dead")

ArenaEnemyFrame_OnEvent(frame, "UNIT_NAME_UPDATE", "arena1")
check(bar.zeroText == "Dead", "other events ignored")

OUT = table.concat(log, "\n") .. (#log > 0 and "\n" or "") .. string.format("pass=%d fail=%d", pass, fail)
