"""Run CENTURION_ArenaMaster under Lua 5.1 against the bounty board's wowmock (plus amock.lua)
and play the server's side of PROTOCOL.md: the Arena Master opening, every queue page, the
mode menu, joins, the All-Star lineup and the replays. Dumps each page for render_dump.py.

usage: python run_am.py
"""
import json
import os
import sys

import lupa.lua51 as lupa

HERE = os.path.dirname(os.path.abspath(__file__))
AM = os.path.dirname(HERE)
BB = r'C:\Projects\Gamedev\wow\clientedits\bountyboard\harness'
ADDON = os.path.join(AM, 'addon', 'CENTURION_ArenaMaster')

lua = lupa.LuaRuntime(unpack_returned_tuples=True)
g = lua.globals()
for f in (os.path.join(BB, 'wowmock.lua'), os.path.join(BB, 'wowmock_ext.lua'), os.path.join(HERE, 'amock.lua'),
          os.path.join(ADDON, 'ArenaMaps.lua'), os.path.join(ADDON, 'CENTURION_ArenaMaster.lua')):
    lua.execute(open(f, encoding='utf-8').read())
mock = g.MOCK
am = g.CenturionArenaMaster

failures = []


def check(cond, what):
    print(('ok    ' if cond else 'FAIL  ') + what)
    if not cond:
        failures.append(what)


def sent():
    return [mock.sent[i] for i in range(1, len(mock.sent) + 1)]


def last_sent():
    return sent()[-1].split('\t')[1] if sent() else ''


def sent_bodies():
    return [s.split('\t')[1] for s in sent()]


def deliver(lines):
    for line in lines:
        assert len('CCGAME\t' + line) <= 255, (len(line), line[:60])
        mock.fire('CHAT_MSG_ADDON', 'CCGAME', line, 'WHISPER', 'Baku')


def visible_texts():
    return [o._text for o in mock.objects.values() if o._kind == 'FontString' and o._text and o.IsVisible(o)]


def button_labelled(text):
    found = None
    for o in mock.objects.values():
        if o._kind == 'Button' and o._label is not None and o._label._text == text and o.IsVisible(o):
            found = o
    return found


def visible(o):
    return o is not None and o.IsVisible(o)


def enabled(o):
    return o is not None and not o._disabled


dumps = {}


def dump(name, hover=None):
    objs = {}
    for k, o in mock.objects.items():
        rec = {}
        for key in ('_kind', '_id', '_layer', '_w', '_h', '_shown', '_alpha', '_file', '_blend', '_desat', '_text',
                    '_justifyH', '_template', '_level', '_name', '_buttonState', '_disabled', '_mouse',
                    '_panelButton'):
            v = o[key]
            if v is not None:
                rec[key] = v
        p = o._parent
        rec['_parent'] = p._id if p is not None else None
        for key in ('_tc', '_vc', '_colour', '_textColour', '_gradient'):
            v = o[key]
            if v is not None:
                rec[key] = [v[i] for i in range(1, len(v) + 1)]
        pts = []
        for i in range(1, len(o._points) + 1):
            pt = o._points[i]
            pts.append({'point': pt.point, 'rel': pt.rel, 'relPoint': pt.relPoint, 'x': pt.x, 'y': pt.y})
        rec['points'] = pts
        rec['children'] = [o._children[i] for i in range(1, len(o._children) + 1)]
        rec['regions'] = [o._regions[i] for i in range(1, len(o._regions) + 1)]
        f = o._font
        if f is not None:
            rec['font'] = {'file': f.file, 'size': f.size,
                           'colour': [f.colour[i] for i in range(1, 4)] if f.colour else [1, 1, 1],
                           'shadow': [f.shadow[i] for i in range(1, 4)] if f.shadow else None}
        bd = o._backdrop
        if bd is not None:
            rec['backdrop'] = {'bgFile': bd.bgFile, 'edgeFile': bd.edgeFile, 'tileSize': bd.tileSize,
                               'edgeSize': bd.edgeSize,
                               'insets': [bd.insets.left, bd.insets.right, bd.insets.top, bd.insets.bottom]}
        objs[k] = rec
    extra = [g.DropDownList1._id] if g.DropDownList1._shown else []
    dumps[name] = {'root': am._id, 'uiparent': g.UIParent._id, 'objects': objs, 'extra': extra, 'hover': hover}


def open_master(modes='1:1:1:1:1:1:4', extra=()):
    deliver(['ARENAQUEUE:0', 'ARENAQUEUE:MASTER', 'AM:MODES:' + modes] + list(extra))
    g.ShowUIPanel(g.ArenaFrame)
    mock.update(0.02)


def pick(mode):
    menu_button = g.CenturionArenaMasterTypeButton
    menu_button.Click(menu_button)
    ok = mock.pickMenu(mode)
    return ok


def category(label):
    for o in mock.objects.values():
        if o._kind == 'Button' and o.label is not None and o.label._text == label and o.IsVisible(o):
            return o
    return None


# 1. the Arena Master opens instead of the stock window --------------------------------
deliver(['ARENAQUEUE:0'])
g.ShowUIPanel(g.ArenaFrame)
check(not am._shown and g.ArenaFrame._shown, 'an ordinary arena window stays the stock one')
g.HideUIPanel(g.ArenaFrame)

open_master(extra=['AM:POOL:559,562,572,617,618'])
check(am._shown, 'ARENAQUEUE:MASTER opens the Arena Master')
check(not g.ArenaFrame._shown, 'and the stock window is put away')
check('ARENABOTS' in sent_bodies(), 'it asks for the Arena bot matches box')
deliver(['ARENABOTS:1'])
check(g.CenturionArenaMasterBots._checked, 'which shows checked when the server says so')
texts = visible_texts()
check('3v3 Skirmish' in texts, 'Skirmish 3v3 by default, on the plate and the sheet')
check('You are not in a group.' in texts, 'alone: says so')
gb = button_labelled('Join as Group')
check(visible(gb) and not enabled(gb), 'Join as Group greyed alone')
check('Arenas in rotation' in texts and "Blade's Edge Arena" in texts or 'Blade\'s Edge' in ' '.join(texts),
      'the rotation from AM:POOL')
dump('1_skirmish')

bots = g.CenturionArenaMasterBots
bots.Click(bots)
check(last_sent() == 'ARENABOTS:0', 'unticking the box sends ARENABOTS:0')

jb = button_labelled('Join Battle')
jb.Click(jb)
check(last_sent() == 'ARENAMASTER:JOIN:S:3:0', 'Join Battle queues the 3v3 skirmish alone')
check(not am._shown, 'and the window closes')
g.ShowUIPanel(g.ArenaFrame)
check(not am._shown, 'the next arena window is the stock one again without a new MASTER')
g.HideUIPanel(g.ArenaFrame)

# 2. Rated, with a team and a group, and the menu open ----------------------------------
mock.teams[1] = lua.table_from({'name': 'Two Dots', 'size': 2, 'rating': 1820, 'played': 9, 'wins': 6,
                                'roster': lua.table_from([lua.table_from(['Tijuana', 'Rogue', 1840]),
                                                          lua.table_from(['Claude', 'Shaman', 1801])])})
mock.teams[2] = lua.table_from({'name': 'The Drunken Monks', 'size': 3, 'rating': 1650, 'played': 14, 'wins': 8,
                                'roster': lua.table_from([lua.table_from(['Drunks', 'Druid', 1702]),
                                                          lua.table_from(['Claude', 'Shaman', 1688]),
                                                          lua.table_from(['Elgrom', 'Warrior', 1650]),
                                                          lua.table_from(['Tijuana', 'Rogue', 1611])])})
mock.party = lua.table_from(['Claude', 'Elgrom'])
mock.leader = True
open_master()
check(pick('Rated'), 'the menu offers Rated')
c = category('3v3 Team')
check(visible(c), 'a row per arena team')
c.Click(c)
texts = visible_texts()
check('The Drunken Monks' in texts, 'the team on the sheet')
check('Drunks' in texts and '1702' in texts, 'its roster with personal ratings')
check(mock.rosterAsked[2], 'and the roster was asked for')
check('Your group: you, Claude and Elgrom' in texts, 'the group line')
jb = button_labelled('Join Battle')
check(enabled(jb), 'Join Battle on for a leader with a group')
g.CenturionArenaMasterTypeButton.Click(g.CenturionArenaMasterTypeButton)
check(g.DropDownList1._shown, 'the menu opens')
hover = None
for b in [mock.listButtons[i] for i in range(1, len(mock.listButtons) + 1)]:
    if b._shown and b.info.text == 'All-Star':
        hover = b._id
dump('2_rated_menu_open', hover)
g.CloseDropDownMenus()
jb.Click(jb)
check(last_sent() == 'ARENAMASTER:JOIN:R:3:1', 'rated joins as the group with the team size')

mock.leader = False
open_master()
pick('Rated')
check('Only your group leader can queue the group.' in visible_texts(), 'not the leader: says so')
check(not enabled(button_labelled('Join Battle')), 'and rated is greyed')
mock.leader = True
am.Hide(am)

# 3. All-Star, before and after the lineup ----------------------------------------------
mock.party = lua.table_from(['Claude'])
g.CENTURION_TEST_RESET = True
open_master()
pick('All-Star')
check('4v4 All-Star' in visible_texts(), 'All-Star opens on 4v4')
deliver(['AM:LINEUP:1:1,Elgrom;7,Claude;11,Drunks;4,Tijuana;8,;5,;9,;3,;2,'])
texts = visible_texts()
check('Elgrom' in texts and 'Standard' in texts and 'Mage' in texts, 'the lineup: own characters, then standard kits')
check('shuffled every match' in texts, 'shuffled says so')
check('Your group: you and Claude' in texts, 'two of you')
dump('3_allstar')
gb = button_labelled('Join as Group')
check(enabled(gb), 'a group of two may join 4v4 together')
gb.Click(gb)
check(last_sent() == 'ARENAMASTER:JOIN:A:4:1', 'All-Star group join')
open_master()
pick('All-Star')
category('1v1 All-Star').Click(category('1v1 All-Star'))
check(button_labelled('Join as Group') is None, '1v1 has no group join')
deliver(['AM:LINEUP:0:1,Elgrom;7,Claude;11,Drunks;4,Tijuana;8,;5,;9,;3,;2,'])
check('shuffled every match' not in visible_texts(), 'not shuffled: nothing said')
am.Hide(am)

# 4. Four Corners ------------------------------------------------------------------------
mock.party = lua.table_from(['Claude', 'Elgrom'])
open_master()
pick('Multi-team')
c = category('3v3v3v3')
c.Click(c)
texts = visible_texts()
check('Four Corners' in texts, 'Multi-team is Four Corners')
jr = button_labelled('Join Rated')
check(enabled(jr), 'Join Rated with a 3v3 team and the group')
dump('4_multiteam')
jr.Click(jr)
check(last_sent() == 'ARENAMASTER:JOIN:MR:3:1', 'Four Corners rated')
open_master()
pick('Multi-team')
category('5v5v5v5').Click(category('5v5v5v5'))
check(not enabled(button_labelled('Join Rated')), 'no 5v5 team: rated greyed')
check('You need a 5v5 arena team to play it rated.' in visible_texts(), 'and says why')
button_labelled('Join as Group').Click(button_labelled('Join as Group'))
check(last_sent() == 'ARENAMASTER:JOIN:MS:5:1', 'Four Corners skirmish as a group')

# 5. Buffle Shuffle ----------------------------------------------------------------------
mock.party = lua.table_from(['Claude', 'Elgrom', 'Drunks', 'Tijuana', 'Ptony'])
mock.raid = True
open_master()
pick('Buffle Shuffle')
check('3v3 Buffle Shuffle' in visible_texts(), 'the 3v3 shuffle by default')
gb = button_labelled('Join as Group')
check(enabled(gb), 'a raid of six may shuffle together')
dump('5_shuffle')
category('2v2 Buffle Shuffle').Click(category('2v2 Buffle Shuffle'))
check(not enabled(button_labelled('Join as Group')), 'but six is too many for a 2v2 shuffle')
button_labelled('Join Battle').Click(button_labelled('Join Battle'))
check(last_sent() == 'ARENAMASTER:JOIN:B:2:0', 'shuffle solo')
mock.raid = False
mock.party = lua.table_from([])

# 6. Replays -----------------------------------------------------------------------------
open_master()
pick('Replays')
check(last_sent() == 'REPLAYS:LIST:RECENT', 'Replays asks for the recent list')
check('Reading the replays...' in visible_texts(), 'and says it is reading')
rows = [
    (4821, 3, 4, 1134, 7200, 12, 1, 1, 1820, 1795, 'Drunken Monks', 'Kill Command', '11.Drunks,7.Claude,1.Elgrom', '3.Hallie,9.Heavens,4.Luminasity'),
    (4820, 2, 4, 617, 10800, 2, 0, 0, 0, 0, '', '', '4.Tijuana,8.Koffee', '2.Ptony,5.Bernko'),
    (4817, 5, 4, 618, 90000, 31, 1, 1, 1702, 1690, 'Old Gods', 'Sunwell Rejects', '11.A,4.B,9.C,3.D,1.E', '1.F,2.G,5.H,7.I,8.J'),
    (4815, 3, 4, 980, 100000, 4, 0, 0, 0, 0, '', '', '9.Watergate,5.Mocachlatte,1.Elgrom', '4.High,4.Koffee,11.Drunks'),
    (4811, 2, 4, 572, 180000, 7, 0, 1, 0, 0, 'Two Dots', 'Bubble Trouble', '4.Tijuana,5.Claude', '2.Arch,8.Ptony'),
    (4809, 3, 889, 562, 190000, 0, 0, 0, 0, 0, '', '', '3.Luminasity,9.Watergate,4.Koffee', '11.Drunks,7.Claude,1.Elgrom'),
    (4801, 3, 4, 1007, 400000, 1, 0, 0, 0, 0, '', '', '1.Elgrom,7.Claude,11.Drunks', '4.High,5.Koffee,9.Ptony'),
]
lines = ['RPL:BEGIN:RECENT']
for r in rows:
    lines.append('RPL:R:%d:%d:%d:%d:%d:%d:%d:%d:%d:%d' % r[:10])
    lines.append('RPL:T:%d:W:%s' % (r[0], r[10]))
    lines.append('RPL:T:%d:L:%s' % (r[0], r[11]))
    lines.append('RPL:P:%d:W:%s' % (r[0], r[12]))
    lines.append('RPL:P:%d:L:%s' % (r[0], r[13]))
lines.append('RPL:END:RECENT:%d' % len(rows))
deliver(lines)
texts = visible_texts()
check('The Tiger\'s Peak' in texts, 'the arena by its map')
check('3v3 Rated  -  Match 4821' in texts, 'kind and match number')
check('2 hours ago  -  watched 12 times' in texts, 'age and views')
check('Drunken Monks' in texts and '1820' in texts, 'a rated team and its rating')
check('Tijuana, Koffee' in texts, 'a skirmish side by its first names')
check('2v2 Rated  -  Match 4811' in texts, 'rated from the flag, even at rating 0')
check('Page 1 of 2' in texts, 'paged six at a time')
check('Replays are only recorded when both teams are fully human.' in texts, 'the human-only note')
card = next(o for o in mock.objects.values() if o._kind == 'Button' and lupa.lua_type(o.row) == 'table' and o.row.id == 4821)
card.Click(card)
check(button_labelled('Unfavorite') is not None, 'a favorite selected offers Unfavorite')
dump('6_replays')
wb = button_labelled('Watch')
watch = [o for o in mock.objects.values() if o._kind == 'Button' and o._label is not None and o._label._text == 'Watch'
         and o.IsVisible(o) and o._w == 110][0]
watch.Click(watch)
check(last_sent() == 'REPLAYS:WATCH:4821', 'Watch starts the selected replay')
card.star.Click(card.star)
check(last_sent() == 'REPLAYS:FAV:4821:0', 'the star unfavorites')
deliver(['RPL:FAV:4821:0'])
check(button_labelled('Favorite') is not None, 'and the button follows')
g.CenturionArenaMasterMatch.SetText(g.CenturionArenaMasterMatch, '4807')
small = [o for o in mock.objects.values() if o._kind == 'Button' and o._label is not None and o._label._text == 'Watch'
         and o.IsVisible(o) and o._w == 62][0]
small.Click(small)
check(last_sent() == 'REPLAYS:WATCH:4807', 'a match by its number')
deliver(['RPL:ERR:There is no replay 4807.'])
check('There is no replay 4807.' in visible_texts(), 'an error shows on the page')
nxt = [o for o in mock.objects.values() if o._kind == 'Button' and o._normal is not None and o._normal._file
       and 'NextPage' in o._normal._file][0]
nxt.Click(nxt)
check('Page 2 of 2' in visible_texts() and 'Guardian\'s Hall' in visible_texts(), 'page two')
fav = category('Favorites')
fav.Click(fav)
check(last_sent() == 'REPLAYS:LIST:FAV', 'Favorites asks for the list')
deliver(['RPL:BEGIN:FAV', 'RPL:END:FAV:0'])
check('No favorites yet.\n\nStar a match to keep it here.' in visible_texts(), 'an empty favorites list says how')
dump('6b_replays_empty_fav')
am.Hide(am)

# 7. no team, and a disabled mode ---------------------------------------------------------
mock.teams = lua.table_from({})
open_master(modes='1:1:1:1:0:1:4')
pick('Rated')
texts = visible_texts()
check(any(t and t.startswith('You are not on an arena team.') for t in texts), 'no team: a clear message')
check(button_labelled('Join Battle') is None, 'and nothing to join')
dump('7_rated_no_team')
check(not pick('All-Star'), 'a disabled mode cannot be picked')
g.CloseDropDownMenus()

json.dump(dumps, open(os.path.join(HERE, 'dumps.json'), 'w'))
print('unknown keys', sorted(k for k in mock.unknown.keys()) if mock.unknown else [])
print('FAILURES' if failures else 'ALL OK', len(failures))
sys.exit(1 if failures else 0)
