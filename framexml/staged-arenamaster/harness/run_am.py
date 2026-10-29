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
          os.path.join(ADDON, 'ArenaMaps.lua'), os.path.join(ADDON, 'BgMaps.lua'),
          os.path.join(ADDON, 'CENTURION_ArenaMaster.lua')):
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
            bc = o._backdropColor
            if bc is not None:
                rec['backdrop']['colour'] = [bc[i] for i in range(1, 5)]
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
check('3v3 Arena' in texts, 'Standard 3v3 by default, on the plate and the sheet')
check('You are not in a group.' in texts, 'alone: says so')
gb = button_labelled('Join as Group')
check(visible(gb) and not enabled(gb), 'Join as Group greyed alone')
check('Arenas in rotation' in texts and "Blade's Edge Arena" in texts or 'Blade\'s Edge' in ' '.join(texts),
      'the rotation from AM:POOL')
dump('1_skirmish')

deliver(['AM:POOL:559,562,572,617,618,980,982,983,984,985,986,1007,1008,1134,1401,1402,1403,1504,1552,1683,559'])
check('Arenas in rotation (20)' in visible_texts(), 'a pool of 20 (one listed twice) shows all 20, counted')
dump('1b_skirmish_pool20')
deliver(['AM:POOL:559,562,572,617,618'])

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

# 2. Standard with a team and a group: Join Rated, and the menu open ---------------------
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
check(pick('Standard'), 'the menu offers Standard')
c = category('3v3 Arena')
check(visible(c), 'a row per bracket')
c.Click(c)
texts = visible_texts()
check('The Drunken Monks' in texts, 'the 3v3 team named on the sheet')
check('1650' in texts, 'its rating beside the 3v3 row')
check('Your group: you, Claude and Elgrom' in texts, 'the group line')
jr = button_labelled('Join Rated')
check(enabled(jr), 'Join Rated on for a leader with a group and the team')
check(enabled(button_labelled('Join Battle')), 'and the skirmish beside it')
g.CenturionArenaMasterTypeButton.Click(g.CenturionArenaMasterTypeButton)
check(g.DropDownList1._shown, 'the menu opens')
shown = [b for b in [mock.listButtons[i] for i in range(1, len(mock.listButtons) + 1)] if b._shown]
check(not any(b.info.icon for b in shown), 'no icons in the menu')
check([b.info.text for b in shown if b.info.text and not b.info.isTitle] ==
      ['Standard', 'Multi-team', 'Buffle Shuffle', 'All-Star', 'Replays'], 'Standard, Multi-team, Buffle Shuffle, All-Star, Replays')
hover = None
for b in shown:
    if b.info.text == 'All-Star':
        hover = b._id
dump('2_standard_menu_open', hover)
g.CloseDropDownMenus()
jr.Click(jr)
check(last_sent() == 'ARENAMASTER:JOIN:R:3:1', 'Join Rated queues the 3v3 team as the group')

mock.leader = False
open_master()
pick('Standard')
check('Only your group leader can queue the group.' in visible_texts(), 'not the leader: says so')
check(not enabled(button_labelled('Join Rated')), 'and rated is greyed')
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
pick('Standard')
check(not enabled(button_labelled('Join Rated')), 'no team: Join Rated greyed')
check(enabled(button_labelled('Join Battle')), 'the skirmish still joins')
dump('7_standard_no_team')
check(not pick('All-Star'), 'a disabled mode cannot be picked')
g.CloseDropDownMenus()
am.Hide(am)

# 8. the Battlemaster: every battleground, the same window -------------------------------
MASTER_GUID = '17379391051418026151'
BG_LIST = ['BG:BEGIN',
           'BG:B:2:489:10:10:80:0:Warsong Gulch', 'BG:B:3:529:15:10:60:0:Arathi Basin',
           'BG:B:1:30:40:60:69:0:Alterac Valley', 'BG:B:7:566:15:60:69:0:Eye of the Storm',
           'BG:B:9:607:15:60:79:0:Strand of the Ancients', 'BG:B:30:628:40:60:79:0:Isle of Conquest',
           'BG:B:100:1189:5:10:80:1:Scarlet Chapel', 'BG:B:101:1230:10:10:80:1:Blackrock Throne',
           'BG:B:102:1615:20:10:80:1:The Obsidian Colosseum', 'BG:B:104:1620:10:1:80:0:Tanaris',
           'BG:B:105:1608:10:10:80:0:The Violet Hold', 'BG:B:106:998:10:10:80:0:Temple of Kotmogu',
           "BG:B:107:1690:5:10:80:0:Summoner's Rift", 'BG:B:108:726:10:10:80:0:Twin Peaks',
           'BG:B:120:761:10:1:60:0:Battle for Gilneas', 'BG:B:32:30:10:60:69:0:Random Battleground',
           'BG:B:31:801:10:60:80:0:Slavery Valley', 'BG:END:17']


def open_bg(preselect=2):
    deliver(['BGQUEUE:MASTER:%s:%d' % (MASTER_GUID, preselect)] + BG_LIST)
    g.ShowUIPanel(g.BattlefieldFrame)
    mock.update(0.02)


def row_button(index):
    for o in mock.objects.values():
        if o._kind == 'Button' and o.row == index and o.label is not None and o.IsVisible(o):
            return o
    return None


mock.party = lua.table_from([])
g.ShowUIPanel(g.BattlefieldFrame)
check(not am._shown and g.BattlefieldFrame._shown, 'an ordinary battleground list stays the stock one')
g.HideUIPanel(g.BattlefieldFrame)

open_bg(2)
check(am._shown, 'BGQUEUE:MASTER opens the Battlemaster')
check(not g.BattlefieldFrame._shown, 'and the stock list is put away')
texts = visible_texts()
check('Battlemaster' in texts, 'headed Battlemaster')
check('Warsong Gulch' in texts, "on the battlemaster's own battleground")
check(not visible(g.CenturionArenaMasterType) and not visible(g.CenturionArenaMasterBots),
      'no arena type menu, no bot box')
first = row_button(1)
check(first is not None and first.label._text == 'Random Battleground', 'Random first in the list')
check(row_button(17) is None, 'sixteen rows show; the seventeenth waits for the wheel')
check('10 vs 10  -  levels 10-80' in texts, 'team size and levels under the title')
check('You are not in a group.' in texts, 'alone: says so')
check(not visible(button_labelled('Join as Group')) or not enabled(button_labelled('Join as Group')),
      'Join as Group greyed alone')
dump('8_bg_wsg')
am._scripts.OnMouseWheel(am, -1)
check(row_button(17) is not None and row_button(17).label._text == 'Slavery Valley', 'the wheel scrolls the list')
check('Warsong Gulch' in visible_texts(), 'and the pick stays')
jb = button_labelled('Join Battle')
jb.Click(jb)
check(last_sent() == 'BGMASTER:JOIN:%s:2:0:N' % MASTER_GUID, 'Join Battle queues Warsong Gulch alone, any side')
check(not am._shown, 'and the window closes')
g.ShowUIPanel(g.BattlefieldFrame)
check(not am._shown, 'the next battleground list is the stock one again')
g.HideUIPanel(g.BattlefieldFrame)

open_bg(100)
texts = visible_texts()
check('Scarlet Chapel' in texts and 'Side' in texts, 'Scarlet Chapel takes a side')
check('Any side' in texts and 'Alliance' in texts and 'Horde' in texts, 'any side, Alliance or Horde')
check(g.CenturionArenaMasterSide1._checked and not g.CenturionArenaMasterSide3._checked, 'any side to start')
g.CenturionArenaMasterSide3.Click(g.CenturionArenaMasterSide3)
check(g.CenturionArenaMasterSide3._checked and not g.CenturionArenaMasterSide1._checked, 'Horde picked, one box at a time')
dump('8b_bg_scarlet_sides')
jb = button_labelled('Join Battle')
jb.Click(jb)
check(last_sent() == 'BGMASTER:JOIN:%s:100:0:H' % MASTER_GUID, 'the side goes with the join')

mock.party = lua.table_from(['Claude', 'Elgrom', 'Drunks', 'Tijuana', 'Grolem'])
mock.leader = True
open_bg(100)
check(not enabled(button_labelled('Join as Group')), 'a party of six is too big for five a side')
check('Your group is too big: 6 of you, 5 at most.' in visible_texts(), 'and says so')
c = category('Warsong Gulch')
c.Click(c)
gb = button_labelled('Join as Group')
check(enabled(gb), 'ten a side takes the six of them')
check(not visible(g.CenturionArenaMasterSide1), 'no side choice where there is none')
gb.Click(gb)
check(last_sent() == 'BGMASTER:JOIN:%s:2:1:N' % MASTER_GUID, 'Join as Group queues the party')
mock.leader = False
open_bg(3)
check(not enabled(button_labelled('Join as Group')), 'not the leader: greyed')
check('Only your group leader can queue the group.' in visible_texts(), 'and says so')
am.Hide(am)
mock.leader = True
mock.party = lua.table_from([])

open_bg(105)
check('The Violet Hold' in visible_texts(), 'a long list opens scrolled to its pick')
dump('8c_bg_violet_hold')
am.Hide(am)

# A server that sends each battleground's party cap (BattlemasterList MaxGroupSize).
BG_LIST_CAP = ['BG:BEGIN', 'BG:B:2:489:10:10:80:0:10:Warsong Gulch',
               'BG:B:1:30:40:60:69:0:5:Alterac Valley', 'BG:END:2']
mock.party = lua.table_from(['Claude', 'Elgrom', 'Drunks', 'Tijuana', 'Grolem'])
mock.leader = True
deliver(['BGQUEUE:MASTER:%s:1' % MASTER_GUID] + BG_LIST_CAP)
g.ShowUIPanel(g.BattlefieldFrame)
mock.update(0.02)
texts = visible_texts()
check('Alterac Valley' in texts and '40 vs 40  -  levels 60-69' in texts, 'the capped line still names it, 40 a side')
check(not enabled(button_labelled('Join as Group')), 'its party cap of five turns a party of six away')
c = category('Warsong Gulch')
c.Click(c)
check(enabled(button_labelled('Join as Group')), 'Warsong Gulch still takes the six')
am.Hide(am)
mock.party = lua.table_from([])

open_master()
check(am._shown and 'Arena Master' in visible_texts(), 'the Arena Master still opens after the Battlemaster')
check(visible(g.CenturionArenaMasterType) and not visible(g.CenturionArenaMasterSide1),
      'with its menu, and no side boxes')
am.Hide(am)

json.dump(dumps, open(os.path.join(HERE, 'dumps.json'), 'w'))
print('unknown keys', sorted(k for k in mock.unknown.keys()) if mock.unknown else [])
print('FAILURES' if failures else 'ALL OK', len(failures))
sys.exit(1 if failures else 0)
