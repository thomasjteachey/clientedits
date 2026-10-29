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
cmd(sh[2], 'BRS', 5)
cmd(sh[2], 'BAR', '0:0:133,1:0:118,2:0:2139,3:128:42122,4:64:7')
cmd(sh[2], 'BMC', '4^INV_Misc_QuestionMark^118^Poly')
cmd(sh[2], 'BAR', '60:0:42955')
cmd(sh[2], 'BRS', 1)  # a newer set replaces the old
cmd(sh[2], 'BAR', '0:0:133,1:0:118,2:0:2139,3:128:42122,4:64:7,60:0:42955')
cmd(sh[2], 'BMC', '4^INV_Misc_QuestionMark^118^Poly')
cmd(sh[3], 'BRS', 2)
cmd(sh[3], 'BAR', '0:0:47486,72:0:2457')

g.CenturionReplaySpectate_Start(sh[2])
g.MOCK.update(0.2)
check(g.MOCK.following == sh[2], 'the camera follows the picked player')
check(g.watch == sh[2], 'spectating makes them the POV')
check(all(not g.ATPlayers[gd].fsmall.main._shown for gd in sh), 'arena frames are hidden while spectating')
panel = g.CenturionReplaySpectateFrame
check(panel._shown, 'the spectate view shows')


def row_buttons(obj_filter):
    out = []
    for o in g.MOCK.objects.values():
        pass
    return out


# find the main row: a frame with 12 children whose parent is the panel and anchored BOTTOM
rows = []
for o in g.MOCK.objects.values():
    if o._parent is not None and o._parent._id == panel._id and o.buttons is not None and o.row is not None:
        rows.append(o)
main = [r for r in rows if r.row.key == 'main'][0]
bl = [r for r in rows if r.row.key == 'bottomleft'][0]
icons = [b.icon._file if b.icon._shown else None for b in main.buttons.values()]
print('    main row icons', icons[:6])
check(icons[0] and icons[0].endswith('Spell_Fire_FlameBolt'), 'button 1 shows Fireball')
check(icons[3] and icons[3].endswith('INV_Jewelry_Necklace_37'), 'an item button shows the item icon')
check(icons[4] and icons[4].endswith('Spell_Nature_Polymorph'), 'a ?-icon macro shows the spell it casts')
check(main.buttons[5].label._text == 'Poly', 'a macro button carries its name')
check(bl._shown and bl.buttons[1].icon._file.endswith('Inv_Misc_Food_100'), 'Bottom Left (61-72) shows when it has buttons')
check(not [r for r in rows if r.row.key == 'bottomright'][0]._shown, 'an empty Bottom Right row stays hidden')

g.MOCK.now = 200
cmd(sh[2], 'BCD', '0:2139:24000')
g.MOCK.update(0.2)
cd = main.buttons[3].cooldown._cd
check(cd is not None and cd[1] == 200 and cd[2] == 24.0, 'Counterspell shows a 24 s cooldown (%s)' % (list(cd.values()) if cd else None))
cmd(sh[2], 'BCD', '0:2139:0')
g.MOCK.update(0.2)
cd = main.buttons[3].cooldown._cd
check(cd is not None and cd[2] == 0, 'a cleared cooldown clears the button')

cmd(sh[2], 'SPE', '118,1500')
check(main.buttons[2].flash._alpha == 1 and main.buttons[5].flash._alpha == 1,
      'casting Polymorph lights its button and the macro that casts it')

# switch to the warrior, in Battle Stance
cmd(sh[3], 'FRM', 17)
g.CenturionReplaySpectate_Start(sh[3])
g.MOCK.update(0.2)
icons = [b.icon._file if b.icon._shown else None for b in main.buttons.values()]
check(g.MOCK.following == sh[3], 'switching follows the new player')
check(icons[0] and icons[0].endswith('Ability_Warrior_OffensiveStance'), 'Battle Stance shows the stance page (slots 73-84)')

# mouse wheel
g.CameraZoomIn(1)
check(g.MOCK.zoom == -2, 'the mouse wheel zooms the follow camera (%s)' % g.MOCK.zoom)

# frames come back
g.CenturionReplaySpectate_Stop()
g.MOCK.update(0.2)
check(g.MOCK.following is None, 'stop gives the camera back')
check(all(g.ATPlayers[gd].fsmall.main._shown for gd in sh), 'arena frames come back after spectating')
check(not panel._shown, 'the spectate view hides')

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
check(g.MOCK.following is None and not panel._shown, 'leaving the replay ends spectating')

print()
print('FAILURES: %d' % len(failures))
sys.exit(1 if failures else 0)
