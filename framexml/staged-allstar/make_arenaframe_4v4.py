"""ArenaFrame.lua with All-Star's fifth row (4v4; owner 2026-10-28: "1/2/3/4/5
should be the amount"), built from the live patch-enUS-A 1.00256 copy
(db0da183b64ba1649a3302a20c88dbee, make_arenaframe.py's output).

The practice line holds four rows (ArenaZone5..8), so All-Star gets a ninth
button under them that only it shows. Its row is practice index 5, which
JoinBattlefield sends as arena slot 4 - the client writes index - 1 into
CMSG_BATTLEMASTER_JOIN_ARENA without a range check (Wow.exe 0x54D990 ->
0x54CC70); the server reads an All-Star slot as size - 1.
usage: make_arenaframe_4v4.py <live.lua> <out.lua>"""
import hashlib
import sys

src, dst = sys.argv[1], sys.argv[2]
raw = open(src, 'rb').read()
assert hashlib.md5(raw).hexdigest() == 'db0da183b64ba1649a3302a20c88dbee', 'not the live 1.00256 copy'
s = raw.decode('utf-8').replace('\r\n', '\n')


def rep(old, new):
    global s
    n = s.count(old)
    if n != 1:
        sys.exit('expected one match, found %d for:\n%s' % (n, old))
    s = s.replace(old, new)


rep('''-- CENTURION: All-Star. Its battlemaster sends CCGAME ARENAQUEUE:ALLSTAR. The
-- practice line becomes the four All-Star skirmishes - 1v1, 2v2, 3v3 and 5v5,
-- one a row; the server reads the row as the size - beside the "Arena bot
-- matches" box, and the rated rows are put away: there is no rated All-Star.
-- A party joins together as ever, up to the row's size.''', '''-- CENTURION: All-Star. Its battlemaster sends CCGAME ARENAQUEUE:ALLSTAR. The
-- practice line becomes the five All-Star skirmishes - 1v1 to 5v5, one a row,
-- the fifth on a button of its own (ALL_STAR_EXTRA_ROW) that only this mode
-- shows; the server reads the row as the size - beside the "Arena bot
-- matches" box, and the rated rows are put away: there is no rated All-Star.
-- A party joins together as ever, up to the row's size.''')

rep('''    [MAX_ARENA_TEAMS + 3] = "3v3 All-Star",
    [MAX_ARENA_TEAMS + 4] = "5v5 All-Star",
}''', '''    [MAX_ARENA_TEAMS + 3] = "3v3 All-Star",
    [MAX_ARENA_TEAMS + 4] = "4v4 All-Star",
    [MAX_ARENA_TEAMS + 5] = "5v5 All-Star",
}
-- The button past the last practice row; nothing but All-Star shows it.
local ALL_STAR_EXTRA_ROW = MAX_ARENA_BATTLES + 1''')

rep('''    if not _G.ArenaZone8 then
        local btn = CreateFrame("Button", "ArenaZone8", ArenaFrame, "ArenaButtonTemplate")
        btn:SetID(8)
        btn:SetScript("OnClick", ArenaButton_OnClick)
    end
end''', '''    if not _G.ArenaZone8 then
        local btn = CreateFrame("Button", "ArenaZone8", ArenaFrame, "ArenaButtonTemplate")
        btn:SetID(8)
        btn:SetScript("OnClick", ArenaButton_OnClick)
    end
    -- All-Star's fifth row
    if not _G["ArenaZone" .. ALL_STAR_EXTRA_ROW] then
        local btn = CreateFrame("Button", "ArenaZone" .. ALL_STAR_EXTRA_ROW, ArenaFrame, "ArenaButtonTemplate")
        btn:SetID(ALL_STAR_EXTRA_ROW)
        btn:SetScript("OnClick", ArenaButton_OnClick)
        btn:Hide()
    end
end''')

rep('''        -- row 8
        if ArenaZone8 then
            ArenaZone8:ClearAllPoints()
            ArenaZone8:SetPoint("LEFT", ArenaFrame, "LEFT", RATED_X, 0)
            ArenaZone8:SetPoint("TOP", ArenaZone7, "BOTTOM", 0, 0)
        end
    end''', '''        -- row 8
        if ArenaZone8 then
            ArenaZone8:ClearAllPoints()
            ArenaZone8:SetPoint("LEFT", ArenaFrame, "LEFT", RATED_X, 0)
            ArenaZone8:SetPoint("TOP", ArenaZone7, "BOTTOM", 0, 0)
        end
        -- All-Star's fifth row, in the gap above the description
        local extra = _G["ArenaZone" .. ALL_STAR_EXTRA_ROW]
        if extra and ArenaZone8 then
            extra:ClearAllPoints()
            extra:SetPoint("LEFT", ArenaFrame, "LEFT", RATED_X, 0)
            extra:SetPoint("TOP", ArenaZone8, "BOTTOM", 0, 0)
        end
    end''')

# Old selections from All-Star's fifth row mean nothing to the other modes.
rep('''    -- The rows a mode keeps; every other row is put away.
    local modeRows = (shuffle and CENTURION_SOLO_SHUFFLE_ROWS) or (allstar and CENTURION_ALL_STAR_ROWS) or nil''', '''    -- The rows a mode keeps; every other row is put away.
    local modeRows = (shuffle and CENTURION_SOLO_SHUFFLE_ROWS) or (allstar and CENTURION_ALL_STAR_ROWS) or nil
    if not allstar and (ArenaFrame.selection or 0) > MAX_ARENA_BATTLES then
        ArenaFrame.selection = MAX_ARENA_TEAMS + 1
    end''')

rep('''    for i = 1, MAX_ARENA_BATTLES do
        local button = _G["ArenaZone"..i]
        if button and modeRows and not modeRows[i] then
            button:Hide()''', '''    for i = 1, ALL_STAR_EXTRA_ROW do
        local button = _G["ArenaZone"..i]
        if button and ((modeRows and not modeRows[i]) or (i > MAX_ARENA_BATTLES and not modeRows)) then
            button:Hide()''')

rep('''    -- only practice queues are joinable from this frame (a shuffle's rows are
    -- all practice rows, alone or as a group)''', '''    -- only practice queues are joinable from this frame (a shuffle's rows are
    -- all practice rows, alone or as a group); All-Star's fifth row is one too
    -- (practice index 5, arena slot 4)''')

rep('''        -- practice 5..8 → 1..4''', '''        -- practice 5..8 → 1..4 (All-Star's fifth row 9 → 5)''')

rep('''CENTURION_ALL_STAR_TEXT = "Every time you fall you are straight back in as the next level 60 character''',
    '''CENTURION_ALL_STAR_TEXT = "Every time you fall you get back up where you fell as the next level 60 character''')

out = s.replace('\n', '\r\n').encode('utf-8')
open(dst, 'wb').write(out)
print(dst, hashlib.md5(out).hexdigest())
