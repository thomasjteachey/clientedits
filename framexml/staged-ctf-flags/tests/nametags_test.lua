-- Loads the real CenturionNameTags.lua (path in MODULE) against stubbed WoW
-- API and exercises the checkbox with the DLL's CVar present and absent.
local fail, pass, log = 0, 0, {}
local function check(cond, what)
	if cond then pass = pass + 1 else fail = fail + 1; log[#log + 1] = "FAIL " .. what end
end

local cvars = {}
local cvarExists = true
function GetCVar(name)
	if not cvarExists then error("Couldn't find CVar named '" .. name .. "'") end
	return cvars[name]
end
function SetCVar(name, value) cvars[name] = value end
function PlaySound() end
local categories = {}
function InterfaceOptions_AddCategory(p) categories[#categories + 1] = p end
UIParent = {}

local function newRegion()
	local r = { shown = true }
	function r:SetPoint() end
	function r:SetText(t) self.text = t end
	function r:SetJustifyH() end
	function r:SetFontObject(f) self.font = f end
	return r
end

function CreateFrame(kind, name, parent, template)
	local f = newRegion()
	f.scripts = {}
	function f:Hide() self.shown = false end
	function f:SetScript(ev, fn) self.scripts[ev] = fn end
	function f:CreateFontString() return newRegion() end
	if kind == "CheckButton" then
		f.checked = false; f.enabled = true
		function f:SetChecked(v) self.checked = v and true or false end
		function f:GetChecked() return self.checked and 1 or nil end
		function f:Enable() self.enabled = true end
		function f:Disable() self.enabled = false end
		_G[name .. "Text"] = newRegion()
	end
	if name then _G[name] = f end
	return f
end
function pcall_wrap(...) return pcall(...) end

local chunk = assert(loadfile(MODULE))
chunk()

local panel = CenturionOptionsPanel
local box = CenturionNameTagsCheck
check(#categories == 1 and categories[1] == panel, "panel registered under AddOns")
check(panel.name == "Centurion", "panel is named Centurion")

-- DLL present, marker on
cvars.centurionNameTags = "1"
panel.scripts.OnShow(panel)
check(box.enabled and box.checked, "on: enabled and ticked")

-- untick -> CVar 0
box.checked = false
box.scripts.OnClick(box)
check(cvars.centurionNameTags == "0", "untick writes 0")
panel.scripts.OnShow(panel)
check(box.enabled and not box.checked, "reopened: unticked")

-- tick -> CVar 1
box.checked = true
box.scripts.OnClick(box)
check(cvars.centurionNameTags == "1", "tick writes 1")

-- no DLL: CVar missing (both as an error and as nil)
cvarExists = false
panel.scripts.OnShow(panel)
check(not box.enabled and not box.checked, "no DLL (error): greyed out")
cvarExists = true; cvars.centurionNameTags = nil
panel.scripts.OnShow(panel)
check(not box.enabled, "no DLL (nil): greyed out")

OUT = string.format("pass=%d fail=%d %s", pass, fail, table.concat(log, "; "))
