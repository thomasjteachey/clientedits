"""Run CenturionComboPoints.lua under Lua 5.1 against a mocked FrameXML and check
what is shown in each situation. Usage: python test_combo.py  (exit 1 on failure)"""
import os
import sys

try:
    import lupa.lua51 as lupa_mod
except ImportError:
    import lupa as lupa_mod

HERE = os.path.dirname(os.path.abspath(__file__))
SRC = open(os.path.join(HERE, 'CenturionComboPoints.lua'), encoding='utf-8').read()

MOCK = r'''
MAX_COMBO_POINTS = 5
COMBOFRAME_FADE_IN = 0.3
COMBOFRAME_HIGHLIGHT_FADE_IN = 0.4
COMBO_FRAME_LAST_NUM_POINTS = 0
ENABLE_COLORBLIND_MODE = "0"
frames = {}
local function newFrame(name)
  local f = { shown = true, alpha = 1, events = {}, scripts = {}, name = name }
  function f:Show() self.shown = true end
  function f:Hide() self.shown = false end
  function f:IsShown() return self.shown end
  function f:SetAlpha(a) self.alpha = a end
  function f:GetAlpha() return self.alpha end
  function f:SetScale() end
  function f:SetWidth() end
  function f:SetHeight() end
  function f:SetPoint(...) self.point = {...} end
  function f:GetName() return self.name end
  function f:RegisterEvent(e) self.events[e] = true end
  function f:SetScript(k, fn) self.scripts[k] = fn end
  if name then _G[name] = f end
  frames[#frames + 1] = f
  return f
end
function CreateFrame(kind, name, parent, template)
  local f = newFrame(name)
  if template == "ComboPointTemplate" then
    newFrame(name .. "Highlight"); newFrame(name .. "Shine")
  end
  return f
end
PlayerFrame = newFrame("PlayerFrame"); PlayerFrame.unit = "player"
ComboFrame = newFrame("ComboFrame"); ComboFrame.shown = false
for i = 1, 5 do newFrame("ComboPoint" .. i); newFrame("ComboPoint" .. i .. "Highlight"); newFrame("ComboPoint" .. i .. "Shine") end
function UIFrameFadeIn(f) f:Show() end
function UIFrameFade(f, info) f:SetAlpha(1) end
function ComboPointShineFadeIn() end
-- world state the tests drive
state = { talentRank = 0, comboGuid = nil, points = 0, target = nil, player = "PLAYER" }
function GetSpellInfo(id) if id == 81500 then return "Unfinished Business" end end
function GetNumTalentTabs() return 3 end
function GetNumTalents(tab) return tab == 1 and 2 or 1 end
function GetTalentInfo(tab, i)
  if tab == 1 and i == 2 then return "Unfinished Business", "", 5, 0, state.talentRank end
  return "Something", "", 0, 0, 1
end
local function guidOf(tok) if tok == "player" then return state.player elseif tok == "target" then return state.target end end
function GetComboPoints(unit, tok)
  local g = guidOf(tok)
  if g and g == state.comboGuid then return state.points end
  return 0
end
-- the stock update, as ComboFrame.lua has it (shape only)
function ComboFrame_Update()
  local n = GetComboPoints(PlayerFrame.unit, "target")
  if n > 0 then ComboFrame:Show() else ComboFrame:Hide() end
  COMBO_FRAME_LAST_NUM_POINTS = n
end
function fire(event, arg) local h = CenturionPlayerComboFrame.scripts.OnEvent; h(CenturionPlayerComboFrame, event, arg) end
function lit()
  local n = 0
  for i = 1, 5 do if _G["CenturionPlayerComboPoint" .. i .. "Highlight"].alpha > 0 then n = n + 1 end end
  return n
end
'''

fails = 0


def check(cond, msg):
    global fails
    print(('ok    ' if cond else 'FAIL  ') + msg)
    if not cond:
        fails += 1


def fresh():
    L = lupa_mod.LuaRuntime(unpack_returned_tuples=True)
    L.execute(MOCK)
    L.execute(SRC)
    return L, L.globals()


# 1. a rogue without the talent: stock frame works, the row never shows
L, G = fresh()
G.state.points, G.state.target, G.state.comboGuid = 3, 'MOB1', 'MOB1'
L.execute('fire("PLAYER_ENTERING_WORLD")')
check(G.ComboFrame.shown and not G.CenturionPlayerComboFrame.shown, 'no talent: stock target pips, no player row')

# 2. talent learned, points on the selected enemy
L.execute('state.talentRank = 1; fire("PLAYER_TALENT_UPDATE")')
check(not G.ComboFrame.shown, 'talent: stock target pips hidden')
check(G.CenturionPlayerComboFrame.shown and L.eval('lit()') == 3, 'talent: row shows 3 lit')

# 3. target cleared: server parks the points on the player
L.execute('state.target = nil; state.comboGuid = state.player; fire("PLAYER_TARGET_CHANGED")')
check(G.CenturionPlayerComboFrame.shown and L.eval('lit()') == 3, 'no target: still 3 (read from player)')
check(not G.ComboFrame.shown, 'no target: stock pips stay hidden')

# 4. new enemy selected: server moves them there, a point is added
L.execute('state.target = "MOB2"; state.comboGuid = "MOB2"; fire("PLAYER_TARGET_CHANGED")')
L.execute('state.points = 4; fire("UNIT_COMBO_POINTS", "player")')
check(L.eval('lit()') == 4, 'switched target and built: 4 lit')

# 5. finisher spends them
L.execute('state.points = 0; fire("UNIT_COMBO_POINTS", "player")')
check(not G.CenturionPlayerComboFrame.shown and L.eval('lit()') == 0, 'spent: row hidden, nothing lit')

# 6. a combo event for another unit is ignored
L.execute('state.points = 2; fire("UNIT_COMBO_POINTS", "vehicle")')
check(not G.CenturionPlayerComboFrame.shown, 'other unit event ignored')
L.execute('fire("UNIT_COMBO_POINTS", "player")')
check(L.eval('lit()') == 2, 'player event: 2 lit')

# 7. in a vehicle the stock frame takes over
L.execute('PlayerFrame.unit = "vehicle"; fire("UNIT_ENTERED_VEHICLE", "player")')
check(not G.CenturionPlayerComboFrame.shown, 'vehicle: row hidden')
L.execute('PlayerFrame.unit = "player"; fire("UNIT_EXITED_VEHICLE", "player")')
check(G.CenturionPlayerComboFrame.shown, 'out of vehicle: row back')

# 8. talent removed (respec): back to stock
L.execute('state.talentRank = 0; state.comboGuid = "MOB2"; fire("PLAYER_TALENT_UPDATE")')
check(not G.CenturionPlayerComboFrame.shown and G.ComboFrame.shown, 'talent removed: stock pips back, row gone')

# 9. anchored to the player frame
check(L.eval('CenturionPlayerComboFrame.point[2] == PlayerFrame'), 'row anchored to PlayerFrame')

print('fail=%d' % fails)
sys.exit(1 if fails else 0)
