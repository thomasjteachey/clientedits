-- Executes BPlusPVPPortraitFix.lua against mocked PvP frames (stock 3.3.5 layout:
-- PVPParentFrame > PVPFrame with PVPFramePortrait; no PVPParentFramePortrait).
-- Global set by the runner: FILE.
local lines = {}
local function log(fmt, ...) lines[#lines + 1] = string.format(fmt, ...) end
local pass, fail = 0, 0
local function check(label, ok, detail)
	if ok then pass = pass + 1 else fail = fail + 1 end
	log("%-4s %s%s", ok and "ok" or "FAIL", label, ok and "" or ("  (" .. tostring(detail) .. ")"))
end

local ICON = "Interface\\BattlefieldFrame\\UI-Battlefield-Icon"

local function texture()
	local t = { tex = nil, shown = false }
	function t:SetTexture(p) self.tex = p end
	function t:SetTexCoord() end
	function t:SetAlpha() end
	function t:Show() self.shown = true end
	function t:SetWidth() end
	function t:SetHeight() end
	function t:SetPoint() end
	return t
end

local function frame(name)
	local f = { hooks = {} }
	function f:HookScript(script, fn) self.hooks[script] = self.hooks[script] or {}; table.insert(self.hooks[script], fn) end
	function f:CreateTexture(texName) local t = texture(); _G[texName] = t; return t end
	function f:Fire(script) for _, fn in ipairs(self.hooks[script] or {}) do fn(self) end end
	_G[name] = f
	return f
end

local eventFrames = {}
function CreateFrame()
	local f = { events = {} }
	function f:RegisterEvent(e) self.events[e] = true end
	function f:SetScript(s, fn) self[s] = fn end
	table.insert(eventFrames, f)
	return f
end

frame("PVPParentFrame")
frame("PVPFrame")
frame("PVPParentFrameTab1")
frame("PVPParentFrameTab2")
PVPFramePortrait = texture()

dofile(FILE)

check("one event frame created", #eventFrames == 1, #eventFrames)
local ef = eventFrames[1]
check("listens to PLAYER_LOGIN and PLAYER_ENTERING_WORLD", ef.events.PLAYER_LOGIN and ef.events.PLAYER_ENTERING_WORLD)

ef.OnEvent(ef, "PLAYER_LOGIN")
check("portrait set at login", PVPFramePortrait.tex == ICON and PVPFramePortrait.shown, PVPFramePortrait.tex)
check("no parent portrait created when PVPFramePortrait exists", PVPParentFramePortrait == nil)
check("OnShow hooked once on PVPParentFrame and PVPFrame",
	#(PVPParentFrame.hooks.OnShow or {}) == 1 and #(PVPFrame.hooks.OnShow or {}) == 1)
check("tab clicks hooked", #(PVPParentFrameTab1.hooks.OnClick or {}) == 1 and #(PVPParentFrameTab2.hooks.OnClick or {}) == 1)

ef.OnEvent(ef, "PLAYER_ENTERING_WORLD")
check("second event does not hook twice", #(PVPFrame.hooks.OnShow or {}) == 1)

-- stock PVPFrame_OnShow runs SetPortraitTexture(PVPFramePortrait, "player") first
PVPFramePortrait.tex = "PORTRAIT(player)"
PVPFrame:Fire("OnShow")
check("PVPFrame OnShow restores the icon after the stock portrait", PVPFramePortrait.tex == ICON, PVPFramePortrait.tex)
PVPFramePortrait.tex = "PORTRAIT(player)"
PVPParentFrameTab2:Fire("OnClick")
check("tab click restores the icon", PVPFramePortrait.tex == ICON, PVPFramePortrait.tex)

log("RESULT pass=%d fail=%d", pass, fail)
OUT = table.concat(lines, "\n")
