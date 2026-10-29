#!/usr/bin/env python3
"""Obsidian Colosseum is 15 a side (owner 2026-10-29: "obsidian colosseum
should be 15v15, not 20v20. please change it everywhere that's relevant").
BattlemasterList row 102: MaxGroupSize (field 28, the party cap) 20 -> 15.

usage: obc15.py <BattlemasterList.dbc> [--apply TAG]"""
import hashlib
import os
import struct
import sys

BG_OBC, OLD, NEW = 102, 20, 15

path = sys.argv[1]
tag = sys.argv[3] if len(sys.argv) > 3 and sys.argv[2] == '--apply' else None
old = open(path, 'rb').read()
data = bytearray(old)
assert data[:4] == b'WDBC'
n, fields, size, ss = struct.unpack_from('<4I', data, 4)
assert fields == 32, fields
found = False
for i in range(n):
    rec = 20 + i * size
    if struct.unpack_from('<I', data, rec)[0] != BG_OBC:
        continue
    found = True
    cap = struct.unpack_from('<I', data, rec + 28 * 4)[0]
    print('battlemaster %d MaxGroupSize %d' % (BG_OBC, cap))
    if cap == OLD:
        struct.pack_into('<I', data, rec + 28 * 4, NEW)
assert found
print(path, hashlib.md5(old).hexdigest(), '->', hashlib.md5(bytes(data)).hexdigest())
if tag and bytes(data) != old:
    open(path + '.bak-' + tag, 'wb').write(old)
    open(path + '.new', 'wb').write(bytes(data))
    os.replace(path + '.new', path)
    print('written (backup .bak-%s)' % tag)
