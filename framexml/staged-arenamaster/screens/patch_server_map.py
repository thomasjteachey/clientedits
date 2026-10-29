#!/usr/bin/env python3
"""Set Map.dbc LoadingScreenID (field 57) for the arena maps the client patch
changed (patch-enUS-A 1.00260), in a server's own Map.dbc - nothing else.
usage: patch_server_map.py <Map.dbc> <backup tag>"""
import hashlib
import shutil
import struct
import sys

WANT = {980: 184, 982: 244, 983: 234, 984: 197, 985: 219, 986: 218, 1007: 200, 1008: 231,
        1134: 214, 1401: 245, 1402: 227, 1403: 210, 1504: 204, 1552: 82, 1683: 102, 1684: 237,
        # battlegrounds (patch-enUS-A 1.00263): Twin Peaks, Gilneas, Kotmogu
        726: 220, 761: 204, 998: 334}

path, tag = sys.argv[1], sys.argv[2]
data = bytearray(open(path, 'rb').read())
assert data[:4] == b'WDBC'
n, fields, size, ssize = struct.unpack_from('<4I', data, 4)
assert fields == 66, fields
before = hashlib.md5(bytes(data)).hexdigest()
changed = []
for i in range(n):
    rec = 20 + i * size
    mid = struct.unpack_from('<I', data, rec)[0]
    if mid in WANT:
        off = rec + 57 * 4
        old = struct.unpack_from('<I', data, off)[0]
        if old != WANT[mid]:
            struct.pack_into('<I', data, off, WANT[mid])
            changed.append((mid, old, WANT[mid]))
missing = set(WANT) - {struct.unpack_from('<I', data, 20 + i * size)[0] for i in range(n)}
print(path, 'md5', before, 'rows', n, 'missing maps', sorted(missing))
if not changed:
    print('nothing to change')
    sys.exit(0)
shutil.copy2(path, path + '.bak-' + tag)
open(path, 'wb').write(bytes(data))
diff = sum(1 for a, b in zip(open(path + '.bak-' + tag, 'rb').read(), bytes(data)) if a != b)
print('changed', len(changed), 'rows,', diff, 'bytes differ; new md5', hashlib.md5(bytes(data)).hexdigest())
for c in changed:
    print('  map %d: %d -> %d' % c)
