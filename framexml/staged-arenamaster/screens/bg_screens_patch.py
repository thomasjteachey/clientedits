"""Battlegrounds without a loading screen get one the client ships (owner
2026-10-29, for the Battlemaster window): Twin Peaks, Battle for Gilneas,
Temple of Kotmogu. Builds on the Map.dbc published as patch-enUS-A 1.00260
(out/Map.dbc) and writes out/Map2.dbc."""
import hashlib
import os
import struct
import sys

sys.path.insert(0, r'C:\Projects\Gamedev\wow\clientedits\bountyboard')
import artlib  # noqa: E402

HERE = os.path.dirname(os.path.abspath(__file__))
WANT = {726: 220,    # Twin Peaks -> Utgarde Keep (mountain hold)
        761: 204,    # Battle for Gilneas -> Shadowfang Keep (Gilneas, worgen)
        998: 334}    # Temple of Kotmogu -> its own scenario screen, never set

for arch in ('patch-L.MPQ', 'patch-enUS-A.MPQ'):
    for a in artlib.archives():
        if os.path.basename(a) == arch:
            d = artlib.mpq(a).extract('DBFilesClient\\LoadingScreens.dbc')
    n, f, size, ss = struct.unpack_from('<4I', d, 4)
    ids = {struct.unpack_from('<I', d, 20 + i * size)[0] for i in range(n)}
    missing = [v for v in WANT.values() if v not in ids]
    print(arch, 'rows', n, 'missing', missing)
    assert not missing

data = bytearray(open(os.path.join(HERE, 'out', 'Map.dbc'), 'rb').read())
print('base md5', hashlib.md5(bytes(data)).hexdigest())
n, fields, size, ss = struct.unpack_from('<4I', data, 4)
done = set()
for i in range(n):
    rec = 20 + i * size
    mid = struct.unpack_from('<I', data, rec)[0]
    if mid in WANT:
        old = struct.unpack_from('<I', data, rec + 57 * 4)[0]
        struct.pack_into('<I', data, rec + 57 * 4, WANT[mid])
        print('map %d: %d -> %d' % (mid, old, WANT[mid]))
        done.add(mid)
assert done == set(WANT), done
open(os.path.join(HERE, 'out', 'Map2.dbc'), 'wb').write(bytes(data))
print('new md5', hashlib.md5(bytes(data)).hexdigest())
