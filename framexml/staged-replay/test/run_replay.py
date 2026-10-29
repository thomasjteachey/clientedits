"""Run the staged CENTURION_Replay addon under Lua 5.1 with the bounty board's WoW mock,
play the server's ASSUN feed for a Four Corners replay, a Buffle Shuffle round change and
an All-Star life, then spectate; assert on frame positions, visibility and the bars."""
import json
import os
import sys

import lupa.lua51 as lupa

HERE = os.path.dirname(os.path.abspath(__file__))
HARNESS = r'C:\Projects\Gamedev\wow\clientedits\bountyboard\harness'
ADDON = os.path.join(os.path.dirname(HERE), 'CENTURION_Replay')

lua = lupa.LuaRuntime()
g = lua.globals()
for f in (os.path.join(HARNESS, 'wowmock.lua'), os.path.join(HARNESS, 'wowmock_ext.lua'), os.path.join(HERE, 'mock_replay.lua')):
    lua.execute(open(f, encoding='utf-8').read())
g.MOCK.itemIcons = lua.table()
spells = lua.table()
for sid, name, icon in [(133, 'Fireball', 'Spell_Fire_FlameBolt'), (118, 'Polymorph', 'Spell_Nature_Polymorph'),
                        (2139, 'Counterspell', 'Spell_Frost_IceShock'), (42955, 'Conjure Refreshment', 'Inv_Misc_Food_100'),
                        (47486, 'Mortal Strike', 'Ability_Warrior_SavageBlow'), (6603, 'Attack', 'Inv_Sword_04'),
                        (2457, 'Battle Stance', 'Ability_Warrior_OffensiveStance')]:
    spells[sid] = lua.table(name, 'Interface\\Icons\\' + icon)
g.MOCK.spells = spells
g.MOCK.itemIcons[42122] = 'Interface\\Icons\\INV_Jewelry_Necklace_37'

for f in ('SunwellAS.lua', 'CenturionReplaySpectate.lua', 'CenturionReplayScore.lua', 'combatfeedback.lua'):
    lua.execute(open(os.path.join(ADDON, f), encoding='latin1').read())

failures = []


def check(cond, what):
    print(('ok   ' if cond else 'FAIL ') + what)
    if not cond:
        failures.append(what)


def msg(text):
    g.MOCK.fire('CHAT_MSG_ADDON', 'ASSUN', text, 'WHISPER', '')


def cmd(guid, prefix, value):
    msg('%s;%s=%s;' % (guid, prefix, value))


def actor(guid, name, cls, team, squad=255):
    cmd(guid, 'NME', name)
    cmd(guid, 'SQD', squad)
    cmd(guid, 'TEM', team)
    cmd(guid, 'CLA', cls)
    cmd(guid, 'MHP', 100)
    cmd(guid, 'CHP', 100)
    cmd(guid, 'STA', 1)
    cmd(guid, 'PWT', 0)


def frame_point(guid, which='fsmall'):
    p = g.ATPlayers[guid]
    f = p[which].main
    pts = [(pt.point, pt.relPoint, pt.x, pt.y) for pt in f._points.values()]
    return pts, f._shown, f._scale


g.MOCK.fire('PLAYER_LOGIN')
g.MOCK.fire('PLAYER_ENTERING_WORLD')

# ---------------------------------------------------------------- Four Corners, 2v2v2v2
msg('ENABLE')
msg('REQUESTRESET')
guids = ['0x00000000000000%02X' % (0x10 + i) for i in range(8)]
names = ['Gold A', 'Gold B', 'Green A', 'Green B', 'Blue A', 'Blue B', 'Purple A', 'Purple B']
for i, gd in enumerate(guids):
    squad = i // 2
    actor(gd, names[i], 8, 469 if squad % 2 == 0 else 67, squad)

corners = {}
for i, gd in enumerate(guids):
    pts, shown, scale = frame_point(gd)
    point, rel, x, y = pts[0]
    corners[names[i]] = (point, x, y, scale)
    print('   ', names[i], point, round(x, 1), round(y, 1), 'scale', scale)
check(corners['Gold A'][0] == 'LEFT' and corners['Gold A'][2] > 0, 'Gold (NW) frames are top left')
check(corners['Blue A'][0] == 'RIGHT' and corners['Blue A'][2] > 0, 'Blue (NE) frames are top right')
check(corners['Purple A'][0] == 'LEFT' and corners['Purple A'][2] < 0, 'Purple (SW) frames are bottom left')
check(corners['Green A'][0] == 'RIGHT' and corners['Green A'][2] < 0, 'Green (SE) frames are bottom right')
check(corners['Gold A'][2] > corners['Gold B'][2], 'a squad stacks downwards')
border = g.ATPlayers[guids[4]].fsmall.main._border
check(abs(border[1] - 0.18) < 0.01 and abs(border[3] - 1.0) < 0.01, 'Blue squad frames have a blue edge')

# ---------------------------------------------------------------- 5v5v5v5 fits
msg('DISABLE')
msg('ENABLE')
guids20 = ['0x0000000000000%03X' % (0x100 + i) for i in range(20)]
for i, gd in enumerate(guids20):
    squad = i // 5
    actor(gd, 'P%d' % i, 1, 469 if squad % 2 == 0 else 67, squad)
ys = {}
for i, gd in enumerate(guids20):
    pts, shown, scale = frame_point(gd)
    point, rel, x, y = pts[0]
    screen_y = y * scale
    ys.setdefault(i // 5, []).append(screen_y)
half = 768 / 2
for s, vals in ys.items():
    print('    squad', s, [round(v) for v in vals])
check(all(-half < v < half for vals in ys.values() for v in vals), '5v5v5v5: every frame is on screen')
check(min(ys[0]) > max(ys[3]), '5v5v5v5: the top-left squad is above the bottom-left one')

# ---------------------------------------------------------------- Buffle Shuffle round change
msg('DISABLE')
msg('ENABLE')
sh = ['0x00000000000000%02X' % (0x40 + i) for i in range(6)]
for i, gd in enumerate(sh):
    actor(gd, 'Shuffler%d' % i, 4, 469 if i < 3 else 67)
before = frame_point(sh[0])[0][0][0]
cmd(sh[0], 'TEM', 67)       # round 2: moved to the other side
after = frame_point(sh[0])[0][0][0]
check(before == 'RIGHT' and after == 'LEFT', 'a TEM change moves the frame to the other side (%s -> %s)' % (before, after))

# ---------------------------------------------------------------- All-Star life: class + name change
cmd(sh[1], 'NME', 'Nextlife')
cmd(sh[1], 'CLA', 1)
p = g.ATPlayers[sh[1]]
check(p.name == 'Nextlife' and p['class'] == 1, 'NME/CLA after a swap rename and reclass the frame')

# ---------------------------------------------------------------- spectate
def sent_since(n):
    return [v for k, v in sorted(g.MOCK.sent.items())][n:]


def uiparent_shown():
    return bool(g.UIParent._shown)


check(not uiparent_shown(), 'a replay hides the normal UI (as before)')
g.MOCK.update(0.1)
check(g.MOCK.armed, 'in a replay a right-click on a player is armed for spectating')

# right-click a replay player in the world
mark = len(sent_since(0))
g.MOCK.click = sh[2]
g.MOCK.update(0.1)
check(g.MOCK.following == sh[2], 'a right-click on a player starts the follow camera on them')
check(g.watch == sh[2], 'spectating makes them the POV')
told = sent_since(mark)
check(any(('SPECTATE ' + sh[2]) in s for s in told), 'the server is asked for their bars (%s)' % told)
check(uiparent_shown(), 'the normal UI shows while spectating (their buttons are on it)')
check(g.MOCK.cvars['Sound_ListenerAtCharacter'] == '0', 'while spectating the sound is heard from the camera')
g.MOCK.update(0.1)
check(all(not g.ATPlayers[gd].fsmall.main._shown for gd in sh), 'arena frames are hidden while spectating')
check(g.CenturionReplaySpectateStatus._shown and 'move to stop' in (g.CenturionReplaySpectateStatus.text._text or ''),
      'a line says who is spectated and how to stop')

# their player frame, their enemy frames
check(g.PlayerFrame.unit == 'focus' and g.MOCK.focus == sh[2] and g.PlayerLevelText._text in ('80', 80),
      'your player frame shows them (through the focus)')
check(g.FocusFrame._alpha == 0, 'the focus frame does not show them twice')
g.Recount.ShowReset(g.Recount)
check(not g.MOCK.recountAsked, 'Recount does not ask to reset for their party')
f = g.MOCK.chatFilters['CHAT_MSG_SYSTEM']
check(f(None, 'CHAT_MSG_SYSTEM', 'Koffee Jones joins the party.') is True and f(None, 'CHAT_MSG_SYSTEM', 'Looting changed to Free for All.') is True,
      'their party joining is not announced in chat')
cmd(sh[3], 'PWT', 3)
g.MOCK.update(0.3)
hl = [l for l in g.MOCK.squadLines.values() if l.startswith('SQH:1:')]
check(hl and '.3' in hl[-1], 'an enemy on energy shows as energy (power type kept): %s' % hl[-1:])
lines = list(g.MOCK.squadLines.values())
mine = [l for l in lines if l.startswith('SQ:')]
check(mine and mine[-1] == 'SQ:0:2', 'the enemy frames are set up from their side (gold, two teams): %s' % mine[-1:])
greens = [l for l in lines if l.startswith('SQR:1:')]
check(greens and greens[-1].count(';') == 3 and 'Shuffler0' in greens[-1], 'their enemies are the green side, this round: %s' % greens[-1:])
check(any(l.startswith('SQH:1:') for l in lines), 'with health and power')
cmd(sh[3], 'CHP', 40)
g.MOCK.update(0.3)
check(list(g.MOCK.squadLines.values())[-1].startswith('SQH:1:') and '40.' in list(g.MOCK.squadLines.values())[-1],
      'an enemy losing health shows on the enemy frame')
g.MOCK.focus = sh[4]
g.MOCK.update(0.3)
check(g.MOCK.focus == sh[2], 'focusing someone else from a frame puts them back')
b = g.CenturionSquadFrame1Member2
b._scripts.PostClick(b, 'RightButton')
check(g.MOCK.following == sh[3] and g.MOCK.focus == sh[3], 'a right-click on an enemy frame spectates that player: %s' % g.MOCK.following)
g.CenturionReplaySpectate_Start(sh[2])
check(g.MOCK.following == sh[2], 'and back')

# the echo of our own whisper does not break the feed parser
msg('SPECTATE ' + sh[2])
check(True, 'our own SPECTATE whisper coming back is ignored without an error')

# a cast lights the default button holding that spell
g.MOCK.actions[1] = 118
g.MOCK.actions[2] = 133
cmd(sh[2], 'SPE', '118,1500')
check(g.ActionButton1._lockedHighlight and not g.ActionButton2._lockedHighlight,
      'their Polymorph lights the Polymorph button on the default bar')
g.MOCK.now = g.MOCK.now + 1
g.MOCK.update(0.1)
check(not g.ActionButton1._lockedHighlight, 'the light goes out')

# their casts on YOUR cast bar
g.MOCK.spells[5143] = lua.table('Arcane Missiles', 'Interface\Icons\Spell_Nature_StarFall')
bar = g.CastingBarFrame
cmd(sh[2], 'SPE', '118,1500')
check(bar._shown and bar.casting == 1 and abs(bar.maxValue - 1.5) < 1e-6 and g.CastingBarFrameText._text == 'Polymorph',
      'a cast of theirs starts your cast bar (Polymorph, 1.5 s)')
bar.value = 0.6
cmd(sh[2], 'CSD', '500')
check(abs(bar.value - 0.1) < 1e-6, 'pushback sets it back')
cmd(sh[2], 'CSG', '118')
check(not bar.casting and bar.fadeOut == 1 and bar._barColour[2] == 1.0, 'the cast going off finishes it green')
cmd(sh[2], 'SPE', '118,1500')
cmd(sh[2], 'CSF', '118,40')
check(not bar.casting and g.CastingBarFrameText._text == 'Interrupted' and bar._barColour[1] == 1.0 and bar._barColour[2] == 0.0,
      'an interrupt shows Interrupted in red')
cmd(sh[2], 'CHS', '5143,5000')
check(bar.channeling == 1 and abs(bar.value - 5.0) < 1e-6 and g.CastingBarFrameText._text == 'Arcane Missiles', 'a channel runs the bar down')
cmd(sh[2], 'CHU', '2500')
check(abs(bar.value - 2.5) < 1e-6, 'a channel update moves it')
cmd(sh[2], 'CHU', '0')
check(not bar.channeling and bar.fadeOut == 1, 'the channel ending finishes it')
cmd(sh[3], 'SPE', '47486,2000')
check(not bar.casting, "someone else's cast leaves your bar alone")

# learn/unlearn spam is filtered while spectating
f = g.MOCK.chatFilters['CHAT_MSG_SYSTEM']
check(f(None, 'CHAT_MSG_SYSTEM', 'You have learned a new spell: Polymorph.') is True, '"You have learned" lines are hidden')
check(f(None, 'CHAT_MSG_SYSTEM', 'Welcome to Centurion') is False, 'other system lines are not')

# right-click someone else switches
mark = len(sent_since(0))
g.MOCK.click = sh[3]
g.MOCK.update(0.1)
check(g.MOCK.following == sh[3] and any(('SPECTATE ' + sh[3]) in s for s in sent_since(mark)), 'a right-click on another player switches to them')

# the mouse wheel is the normal camera's: the addon does not touch it
log_before = len(list(g.MOCK.spectateLog.values()))
g.CameraZoomIn(1)
check(len(list(g.MOCK.spectateLog.values())) == log_before and g.MOCK.following == sh[3], 'the mouse wheel leaves spectating alone')

# W: the DLL ends the follow, the addon notices
mark = len(sent_since(0))
g.MOCK.following = None
g.MOCK.moved = True
g.MOCK.update(0.1)
check(not g.CenturionReplaySpectate_IsActive(), 'moving ends spectating')
check(any('SPECTATE OFF LAND' in s for s in sent_since(mark)), 'the server is told to give the bars back and land you on them')
check(not uiparent_shown(), 'the UI goes back the way the replay had it')
check(all(g.ATPlayers[gd].fsmall.main._shown for gd in sh), 'arena frames come back')
check(not g.CenturionReplaySpectateStatus._shown, 'the status line goes')
check(g.PlayerFrame.unit == 'player' and g.MOCK.focus is None and g.FocusFrame._alpha == 1, 'your own player frame and focus come back')
check(list(g.MOCK.squadLines.values())[-1] == 'SQ:-', 'the enemy frames are put away')
check(g.MOCK.cvars['Sound_ListenerAtCharacter'] == '1', 'the sound goes back to your own setting')
check(f(None, 'CHAT_MSG_SYSTEM', 'You have unlearned Polymorph.') is True, '"unlearned" lines just after are hidden too')

# the arena frame right-click
frame = g.ATPlayers[sh[4]].fsmall.main
frame._scripts.OnClick(frame, 'RightButton')
check(g.MOCK.following == sh[4], "right-clicking a player's arena frame spectates them")
mark = len(sent_since(0))
g.CenturionReplaySpectate_Stop()
check(g.MOCK.following is None, 'the Spectate button / X stops it')
check(any('SPECTATE OFF' in v for v in sent_since(mark)) and not any('LAND' in v for v in sent_since(mark)), 'the Spectate button does not move you')

# logging out while spectating leaves the setting as it was
g.CenturionReplaySpectate_Start(sh[2])
g.MOCK.fire('PLAYER_LOGOUT')
check(g.MOCK.cvars['Sound_ListenerAtCharacter'] == '1', 'logging out spectating saves your own sound setting, not ours')
g.CenturionReplaySpectate_Stop()

# the scoreboard any time in a replay
check(not uiparent_shown(), 'the replay starts with the UI hidden')
asked = g.MOCK.scoreRequests
g.ToggleWorldStateScoreFrame()
check(g.WorldStateScoreFrame._shown and g.MOCK.scoreRequests == asked + 1, 'the scoreboard key opens the board mid-replay and asks the server')
check(uiparent_shown(), 'and shows the UI it lives in')
g.MOCK.update(1.0)
g.MOCK.update(1.1)
check(g.MOCK.scoreRequests == asked + 2, 'the open board keeps asking')
g.ToggleWorldStateScoreFrame()
check(not g.WorldStateScoreFrame._shown and not uiparent_shown(), 'closing it hides the UI again')
g.CenturionReplayScore_Toggle()
check(g.WorldStateScoreFrame._shown, 'the Scoreboard button opens it')
g.CenturionReplayScore_Toggle()

# restart resumes
g.CenturionReplaySpectate_Start(sh[2])
msg('DISABLE')
check(g.MOCK.following is None, 'a restart (DISABLE) stops the follow')
msg('ENABLE')
for i, gd in enumerate(sh):
    actor(gd, 'Shuffler%d' % i, 4, 469 if i < 3 else 67)
g.MOCK.update(0.6)
check(g.MOCK.following == sh[2], 'after the restart the same player is followed again')
g.MOCK.fire('PLAYER_ENTERING_WORLD')
g.MOCK.update(0.6)
check(g.MOCK.following is None and not g.CenturionReplaySpectate_IsActive(), 'leaving the replay ends spectating')
check(g.MOCK.armed is False, 'and right-click is a normal right-click again')
stock = g.MOCK.stockToggle or 0
g.ToggleWorldStateScoreFrame()
check((g.MOCK.stockToggle or 0) == stock + 1 and not g.WorldStateScoreFrame._shown, 'outside a replay the scoreboard key is the stock one')

print()
print('FAILURES: %d' % len(failures))
sys.exit(1 if failures else 0)
