"""Append spell 92335 "Spectator Mode" to a 3.3.5 Spell.dbc.

A hidden aura that blanks the full-screen effect: the client's effect picker (Wow.exe
0x4F8979) takes an aura with effect 260 SCREEN_EFFECT ahead of the ghost check and
uses its MiscValue as the effect id - 0 is none. Violet Hold puts it on a player who
releases, so the dead watch the run without the ghost screen, as they did while the
client called the Hold an arena.

Cloned from 90214 Obstructing Presence (hidden, can't cancel, death persistent,
infinite), then: id 92335, castable while dead, can target dead, self target, one
effect APPLY_AURA 260 misc 0, new name, no rank/description text.

usage: python3 add_spectator_spell.py <in Spell.dbc> <out Spell.dbc>
"""
import struct
import sys

NEW_ID, TEMPLATE = 92335, 90214
NAME = b'Spectator Mode'

src, dst = sys.argv[1], sys.argv[2]
data = open(src, 'rb').read()
magic, records, fields, size, strsize = struct.unpack_from('<4s4I', data, 0)
assert magic == b'WDBC' and fields == 234 and size == 936, (magic, fields, size)
recs = data[20:20 + records * size]
strings = data[20 + records * size:]
assert len(strings) == strsize

ids = [struct.unpack_from('<I', recs, i * size)[0] for i in range(records)]
assert NEW_ID not in ids, 'spell %d already exists' % NEW_ID
row = list(struct.unpack_from('<%dI' % fields, recs, ids.index(TEMPLATE) * size))

name_offset = len(strings)
strings = strings + NAME + b'\0'

row[0] = NEW_ID
row[4] = 0x80000000 | 0x00800000        # CANT_CANCEL | CASTABLE_WHILE_DEAD
row[5] = 0x12000000                     # DONT_DISPLAY_IN_AURA_BAR (both bits the custom hidden auras carry)
row[6] = 0x4 | 0x1                      # template's AttributesEx2 | CAN_TARGET_DEAD
row[7] = 0x100000                       # DEATH_PERSISTENT
row[46] = 1                             # range: self
for e in range(3):                      # effects: only effect 1
    for base in (71, 74, 77, 80, 83, 86, 89, 92, 95, 98, 101, 104, 107, 110, 113, 116, 119, 122):
        row[base + e] = 0
row[71] = 6                             # SPELL_EFFECT_APPLY_AURA
row[74] = 1                             # die sides
row[86] = 1                             # TARGET_UNIT_CASTER
row[95] = 260                           # SPELL_AURA_SCREEN_EFFECT
row[110] = 0                            # screen effect 0 = none
row[136] = name_offset                  # Name enUS
for i in range(137, 152):               # other locales
    row[i] = 0
for start in (153, 170, 187):           # Rank, Description, AuraDescription: empty
    for i in range(start, start + 16):
        row[i] = 0

new_rec = struct.pack('<%dI' % fields, *row)
out = struct.pack('<4s4I', magic, records + 1, fields, size, len(strings)) + recs + new_rec + strings
open(dst, 'wb').write(out)

# read back
d = open(dst, 'rb').read()
m, r, f, s, ss = struct.unpack_from('<4s4I', d, 0)
st = d[20 + r * s:]
v = struct.unpack_from('<%dI' % f, d, 20 + (r - 1) * s)
nm = st[v[136]:st.index(b'\0', v[136])].decode()
assert v[0] == NEW_ID and nm == NAME.decode() and v[95] == 260 and v[110] == 0
assert d[20:20 + records * size] == recs                      # existing records untouched
assert d[20 + r * s:20 + r * s + strsize] == data[20 + records * size:]  # existing strings untouched
print('%s: %d -> %d records, strings %d -> %d; %d "%s" aura %d misc %d attrs %s'
      % (dst, records, r, strsize, ss, v[0], nm, v[95], v[110], [hex(x) for x in v[4:8]]))
