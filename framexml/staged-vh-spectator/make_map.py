#!/usr/bin/env python3
"""Mark Violet Hold's map (1608) as an arena in the CLIENT's Map.dbc.

usage: make_map.py <live Map.dbc from patch-enUS-A> <out Map.dbc>

Map.dbc field 2 is InstanceType (record +8). The client reads it for the map named
in the battlefield status packet and stores it at 0xBEA570; every arena behaviour
it has keys off that value being 4 - the death popup's spectator text, the missing
ghost screen, arena1-5 slots. Arenas are 4, battlegrounds 3. The SERVER's Map.dbc
keeps 1608 at 3: its battleground code depends on that, and the client copy is
the only one that decides how death looks.

Only that one dword changes; the script refuses to write anything else.
"""
import struct
import sys

MAP_ID = 1608
OLD_TYPE, NEW_TYPE = 3, 4  # MAP_BATTLEGROUND -> MAP_ARENA

src, dst = sys.argv[1], sys.argv[2]
data = bytearray(open(src, 'rb').read())
magic, nrec, nfld, rsize, ssize = struct.unpack_from('<4s4I', data, 0)
assert magic == b'WDBC' and nfld == 66 and rsize == 264, (magic, nfld, rsize)

hits = [i for i in range(nrec) if struct.unpack_from('<I', data, 20 + i * rsize)[0] == MAP_ID]
assert len(hits) == 1, 'map %d rows: %d' % (MAP_ID, len(hits))
at = 20 + hits[0] * rsize + 8
cur = struct.unpack_from('<I', data, at)[0]
if cur == NEW_TYPE:
    sys.exit('map %d is already InstanceType %d' % (MAP_ID, NEW_TYPE))
assert cur == OLD_TYPE, 'map %d InstanceType is %d, expected %d' % (MAP_ID, cur, OLD_TYPE)
struct.pack_into('<I', data, at, NEW_TYPE)

before = open(src, 'rb').read()
diff = [i for i in range(len(data)) if data[i] != before[i]]
assert len(data) == len(before) and diff == [at], diff
open(dst, 'wb').write(data)
print('map %d InstanceType %d -> %d at byte %d; %d bytes, nothing else changed'
      % (MAP_ID, OLD_TYPE, NEW_TYPE, at, len(data)))
