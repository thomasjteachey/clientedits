#!/usr/bin/env python3
"""Make Violet Hold's map (1608) a battleground again in the CLIENT's Map.dbc.

usage: revert_map.py <live Map.dbc from patch-enUS-A> <out Map.dbc>

The undo of make_map.py (patch-enUS-A 1.00167): InstanceType 4 -> 3, one dword,
record +8. As an arena the client held the Hold to its own arena rules and refused
potions and Rebirth there before the server heard of them; the spectator popup and
the missing ghost screen are now done by WorldStateFrame.lua and spell 92335.
Refuses to change anything else.
"""
import struct
import sys

MAP_ID = 1608
OLD_TYPE, NEW_TYPE = 4, 3  # MAP_ARENA -> MAP_BATTLEGROUND

src, dst = sys.argv[1], sys.argv[2]
data = bytearray(open(src, 'rb').read())
magic, nrec, nfld, rsize, ssize = struct.unpack_from('<4s4I', data, 0)
assert magic == b'WDBC' and nfld == 66 and rsize == 264, (magic, nfld, rsize)

hits = [i for i in range(nrec) if struct.unpack_from('<I', data, 20 + i * rsize)[0] == MAP_ID]
assert len(hits) == 1, 'map %d rows: %d' % (MAP_ID, len(hits))
at = 20 + hits[0] * rsize + 8
cur = struct.unpack_from('<I', data, at)[0]
assert cur == OLD_TYPE, 'map %d InstanceType is %d, expected %d' % (MAP_ID, cur, OLD_TYPE)
struct.pack_into('<I', data, at, NEW_TYPE)

before = open(src, 'rb').read()
diff = [i for i in range(len(data)) if data[i] != before[i]]
assert len(data) == len(before) and diff == [at], diff
open(dst, 'wb').write(data)
print('map %d InstanceType %d -> %d at byte %d; %d bytes, nothing else changed'
      % (MAP_ID, OLD_TYPE, NEW_TYPE, at, len(data)))
