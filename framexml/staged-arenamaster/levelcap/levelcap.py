#!/usr/bin/env python3
"""Centurion's level cap is 60 (owner 2026-10-29: "the max level is 60. so
60-69 and 60-79 and 10-80 don't make sense"), and Summoner's Rift gets a stock
loading screen ("pick one from stock").

usage: levelcap.py <dir with PvpDifficulty.dbc, BattlemasterList.dbc, Map.dbc> [--apply TAG]

 - PvpDifficulty: a band that starts at or below 60 and ends above it ends at 60.
   Bands wholly above 60 (Tanaris's 80-80 disable, Slavery Valley) are left.
   Proves that every map answers every level 1-60 with the SAME row, both the
   server's way (whole-store scan) and the client's (file order, giving up at
   the first other map after its own - the honor tab's canEnter).
 - BattlemasterList: fields 30/31 (MinLevel/MaxLevel, unread by the server) the
   same way.
 - Map: Summoner's Rift (1690) loading screen 161 (Zul'Gurub).
Only those uint32 fields change; record count, order and strings are kept.
Without --apply it reports and verifies only. With it, each changed file is
backed up as <file>.bak-<TAG> and replaced atomically."""
import hashlib
import os
import struct
import sys

CAP = 60
SR_MAP, SR_SCREEN = 1690, 161


def load(path):
    data = bytearray(open(path, 'rb').read())
    assert data[:4] == b'WDBC', path
    n, fields, size, ss = struct.unpack_from('<4I', data, 4)
    return data, n, fields, size


def rows(data, n, size, fields):
    return [struct.unpack_from('<%dI' % fields, data, 20 + i * size) for i in range(n)]


def server_pick(recs, mapid, level):
    for r in recs:
        if r[1] == mapid and r[3] <= level <= r[4]:
            return r[0]
    return None


def client_pick(recs, mapid, level):
    seen = False
    for r in recs:
        if r[1] != mapid:
            if seen:
                return None
            continue
        if r[3] <= level <= r[4]:
            return r[0]
        seen = True
    return None


def pvp(path):
    data, n, fields, size = load(path)
    assert fields == 6, fields
    before = rows(data, n, size, fields)
    changes = []
    for i, r in enumerate(before):
        if r[3] <= CAP < r[4]:
            struct.pack_into('<I', data, 20 + i * size + 4 * 4, CAP)
            changes.append('pvp %d map %d bracket %d: %d-%d -> %d-%d' % (r[0], r[1], r[2], r[3], r[4], r[3], CAP))
    after = rows(data, n, size, fields)
    for m in sorted({r[1] for r in before}):
        for lvl in range(1, CAP + 1):
            assert server_pick(before, m, lvl) == server_pick(after, m, lvl), ('server', m, lvl)
            assert client_pick(before, m, lvl) == client_pick(after, m, lvl), ('client', m, lvl)
    return data, changes


def bml(path):
    data, n, fields, size = load(path)
    assert fields == 32, fields             # ... 28 MaxGroupSize, 29 Holiday, 30 MinLevel, 31 MaxLevel
    changes = []
    for i, r in enumerate(rows(data, n, size, fields)):
        lo, hi = r[30], r[31]
        if lo <= CAP < hi:
            struct.pack_into('<I', data, 20 + i * size + 31 * 4, CAP)
            changes.append('battlemaster %d: levels %d-%d -> %d-%d' % (r[0], lo, hi, lo, CAP))
    return data, changes


def mapdbc(path):
    data, n, fields, size = load(path)
    assert fields == 66, fields
    changes = []
    for i, r in enumerate(rows(data, n, size, fields)):
        if r[0] == SR_MAP and r[57] != SR_SCREEN:
            struct.pack_into('<I', data, 20 + i * size + 57 * 4, SR_SCREEN)
            changes.append('map %d loading screen %d -> %d' % (SR_MAP, r[57], SR_SCREEN))
    return data, changes


def main():
    folder = sys.argv[1]
    tag = sys.argv[3] if len(sys.argv) > 3 and sys.argv[2] == '--apply' else None
    for name, fn in (('PvpDifficulty.dbc', pvp), ('BattlemasterList.dbc', bml), ('Map.dbc', mapdbc)):
        path = os.path.join(folder, name)
        if not os.path.exists(path):
            print(name, 'absent')
            continue
        old = open(path, 'rb').read()
        data, changes = fn(path)
        print('%s %s -> %s, %d change(s)' % (name, hashlib.md5(old).hexdigest(), hashlib.md5(bytes(data)).hexdigest(), len(changes)))
        for c in changes:
            print('   ', c)
        if tag and changes:
            open(path + '.bak-' + tag, 'wb').write(old)
            open(path + '.new', 'wb').write(bytes(data))
            os.replace(path + '.new', path)
            print('    written (backup %s.bak-%s)' % (name, tag))


main()
