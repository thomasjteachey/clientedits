"""ArenaFrame.lua with the All-Star mode, built from the live patch-enUS-A copy
(bf1252c1aed2e58729feccd7b3e9aa03). usage: make_arenaframe.py <live.lua> <out.lua>"""
import hashlib
import sys

src, dst = sys.argv[1], sys.argv[2]
raw = open(src, 'rb').read()
assert hashlib.md5(raw).hexdigest() == 'bf1252c1aed2e58729feccd7b3e9aa03', 'not the live 1.00255 copy'
s = raw.decode('utf-8').replace('\r\n', '\n')


def rep(old, new):
    global s
    n = s.count(old)
    if n != 1:
        sys.exit('expected one match, found %d for:\n%s' % (n, old))
    s = s.replace(old, new)


rep('''-- Picked when the window opens on anything else: the 3v3 row.
local SHUFFLE_DEFAULT_ROW = MAX_ARENA_TEAMS + 2
''', '''-- Picked when the window opens on anything else: the 3v3 row.
local SHUFFLE_DEFAULT_ROW = MAX_ARENA_TEAMS + 2

-- CENTURION: All-Star. Its battlemaster sends CCGAME ARENAQUEUE:ALLSTAR. The
-- practice line becomes the four All-Star skirmishes - 1v1, 2v2, 3v3 and 5v5,
-- one a row; the server reads the row as the size - beside the "Arena bot
-- matches" box, and the rated rows are put away: there is no rated All-Star.
-- A party joins together as ever, up to the row's size.
CENTURION_ARENA_ALLSTAR = false
CENTURION_ALL_STAR_TITLE = "All-Star"
CENTURION_ALL_STAR_HEADER = "Nine lives: one for each class."
CENTURION_ALL_STAR_ROWS = {
    [MAX_ARENA_TEAMS + 1] = "1v1 All-Star",
    [MAX_ARENA_TEAMS + 2] = "2v2 All-Star",
    [MAX_ARENA_TEAMS + 3] = "3v3 All-Star",
    [MAX_ARENA_TEAMS + 4] = "5v5 All-Star",
}
CENTURION_ALL_STAR_TEXT = "Every time you fall you are straight back in as the next level 60 character in your character list - or a starter loadout for a class you have none of - until you have fallen as all nine classes. The last team with lives left wins."
-- Picked when the window opens on anything else: the 2v2 row.
local ALL_STAR_DEFAULT_ROW = MAX_ARENA_TEAMS + 2
''')

rep('''            and not CENTURION_ARENA_SHUFFLE
        then''', '''            and not CENTURION_ARENA_SHUFFLE
            and not CENTURION_ARENA_ALLSTAR
        then''')

rep('''            if CENTURION_ARENA_SHUFFLE then
                ArenaFrameZoneDescription:SetText(CENTURION_SOLO_SHUFFLE_TEXT)
            elseif''', '''            if CENTURION_ARENA_SHUFFLE then
                ArenaFrameZoneDescription:SetText(CENTURION_SOLO_SHUFFLE_TEXT)
            elseif CENTURION_ARENA_ALLSTAR then
                ArenaFrameZoneDescription:SetText(CENTURION_ALL_STAR_TEXT)
            elseif''')

rep('''        if not CENTURION_SOLO_SHUFFLE_ROWS[ArenaFrame.selection or 0] then
            ArenaFrame.selection = SHUFFLE_DEFAULT_ROW
        end
    end
''', '''        if not CENTURION_SOLO_SHUFFLE_ROWS[ArenaFrame.selection or 0] then
            ArenaFrame.selection = SHUFFLE_DEFAULT_ROW
        end
    end
    local allstar = CENTURION_ARENA_ALLSTAR and not shuffle
    if allstar then
        for row, label in pairs(CENTURION_ALL_STAR_ROWS) do
            practiceLabels[row - MAX_ARENA_TEAMS] = label
        end
        if not CENTURION_ALL_STAR_ROWS[ArenaFrame.selection or 0] then
            ArenaFrame.selection = ALL_STAR_DEFAULT_ROW
        end
    end
    -- The rows a mode keeps; every other row is put away.
    local modeRows = (shuffle and CENTURION_SOLO_SHUFFLE_ROWS) or (allstar and CENTURION_ALL_STAR_ROWS) or nil
''')

rep('''        if shuffle then
            ArenaFrameFrameLabel:SetText(CENTURION_SOLO_SHUFFLE_TITLE)
        else''', '''        if shuffle then
            ArenaFrameFrameLabel:SetText(CENTURION_SOLO_SHUFFLE_TITLE)
        elseif allstar then
            ArenaFrameFrameLabel:SetText(CENTURION_ALL_STAR_TITLE)
        else''')

rep('''        ArenaFrameNameHeader:SetText(shuffle and CENTURION_SOLO_SHUFFLE_HEADER or ARENA_RATED_BATTLE)''',
    '''        ArenaFrameNameHeader:SetText((shuffle and CENTURION_SOLO_SHUFFLE_HEADER) or (allstar and CENTURION_ALL_STAR_HEADER) or ARENA_RATED_BATTLE)''')

rep('''        if button and shuffle and not CENTURION_SOLO_SHUFFLE_ROWS[i] then''',
    '''        if button and modeRows and not modeRows[i] then''')

rep('''    if message == "ARENAQUEUE:SHUFFLE" then
        CENTURION_ARENA_SHUFFLE = true
        CENTURION_ARENA_SQUADS = 0''', '''    if message == "ARENAQUEUE:SHUFFLE" then
        CENTURION_ARENA_SHUFFLE = true
        CENTURION_ARENA_ALLSTAR = false
        CENTURION_ARENA_SQUADS = 0''')

rep('''    local squads = string.match(message, "^ARENAQUEUE:(%d+)$")
    if squads then
        CENTURION_ARENA_SHUFFLE = false''', '''    if message == "ARENAQUEUE:ALLSTAR" then
        CENTURION_ARENA_ALLSTAR = true
        CENTURION_ARENA_SHUFFLE = false
        CENTURION_ARENA_SQUADS = 0
        if ArenaFrame and ArenaFrame:IsShown() then
            ArenaFrame_Update()
        end
        return
    end

    local squads = string.match(message, "^ARENAQUEUE:(%d+)$")
    if squads then
        CENTURION_ARENA_SHUFFLE = false
        CENTURION_ARENA_ALLSTAR = false''')

out = s.replace('\n', '\r\n').encode('utf-8')
open(dst, 'wb').write(out)
print(dst, hashlib.md5(out).hexdigest())
