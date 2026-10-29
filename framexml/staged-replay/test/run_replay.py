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

for f in ('SunwellAS.lua', 'CenturionReplaySpectate.lua', 'combatfeedback.lua'):
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
g.MOCK.update(0.1)
check(all(not g.ATPlayers[gd].fsmall.main._shown for gd in sh), 'arena frames are hidden while spectating')
check(g.CenturionReplaySpectateStatus._shown and 'move to stop' in (g.CenturionReplaySpectateStatus.text._text or ''),
      'a line says who is spectated and how to stop')

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
check(any('SPECTATE OFF' in s for s in sent_since(mark)), 'the server is told to give the bars back')
check(not uiparent_shown(), 'the UI goes back the way the replay had it')
check(all(g.ATPlayers[gd].fsmall.main._shown for gd in sh), 'arena frames come back')
check(not g.CenturionReplaySpectateStatus._shown, 'the status line goes')
check(f(None, 'CHAT_MSG_SYSTEM', 'You have unlearned Polymorph.') is True, '"unlearned" lines just after are hidden too')

# the arena frame right-click
frame = g.ATPlayers[sh[4]].fsmall.main
frame._scripts.OnClick(frame, 'RightButton')
check(g.MOCK.following == sh[4], "right-clicking a player's arena frame spectates them")
g.CenturionReplaySpectate_Stop()
check(g.MOCK.following is None, 'the Spectate button / X stops it')

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

print()
print('FAILURES: %d' % len(failures))
sys.exit(1 if failures else 0)
