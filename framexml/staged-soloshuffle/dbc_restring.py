"""Rename text inside a WDBC file's string fields without disturbing anything else.

  dbc_restring.py find <in.dbc> <old>
      every (record id, field) whose value is the start of a string containing <old>
  dbc_restring.py apply <in.dbc> <out.dbc> <field> <old> <new> <expected count>
      for each record whose <field> points at such a string, append the renamed string to the
      string block and point the field at it; refuses unless exactly <expected count> records change

The old string stays in the block, unreferenced, so no other offset moves.
Written 2026-10-22 for Solo Shuffle -> Buffle Shuffle (BattlemasterList row 889, GameTips).
"""
import struct
import sys


def load(path):
    b = open(path, 'rb').read()
    magic, nrec, nfield, recsize, strsize = struct.unpack_from('<4s4I', b, 0)
    if magic != b'WDBC':
        sys.exit('%s: not WDBC' % path)
    recs = bytearray(b[20:20 + nrec * recsize])
    strs = bytearray(b[20 + nrec * recsize:])
    if len(strs) != strsize:
        sys.exit('%s: string block is %d bytes, header says %d' % (path, len(strs), strsize))
    return nrec, nfield, recsize, recs, strs


def string_at(strs, v):
    if v == 0 or v >= len(strs) or strs[v - 1] != 0:
        return None   # not the start of a string
    return bytes(strs[v:strs.index(0, v)])


def main():
    mode = sys.argv[1]
    if mode == 'find':
        path, old = sys.argv[2], sys.argv[3].encode()
        nrec, nfield, recsize, recs, strs = load(path)
        for i in range(nrec):
            rid = struct.unpack_from('<I', recs, i * recsize)[0]
            for f in range(1, nfield):
                s = string_at(strs, struct.unpack_from('<I', recs, i * recsize + f * 4)[0])
                if s is not None and old in s:
                    print('id %d field %d: %s' % (rid, f, s.decode('utf-8', 'replace')))
        return

    if mode != 'apply':
        sys.exit(__doc__)
    inp, out, field = sys.argv[2], sys.argv[3], int(sys.argv[4])
    old, new, expected = sys.argv[5].encode(), sys.argv[6].encode(), int(sys.argv[7])
    nrec, nfield, recsize, recs, strs = load(inp)
    oldsize = len(strs)
    changed = 0
    for i in range(nrec):
        off = i * recsize + field * 4
        s = string_at(strs, struct.unpack_from('<I', recs, off)[0])
        if s is None or old not in s:
            continue
        renamed = s.replace(old, new)
        struct.pack_into('<I', recs, off, len(strs))
        strs += renamed + b'\0'
        changed += 1
        print('id %d field %d: %s -> %s' % (struct.unpack_from('<I', recs, i * recsize)[0], field,
                                            s.decode('utf-8', 'replace'), renamed.decode('utf-8', 'replace')))
    if changed != expected:
        sys.exit('expected %d record(s), would change %d - nothing written' % (expected, changed))
    with open(out, 'wb') as f:
        f.write(struct.pack('<4s4I', b'WDBC', nrec, nfield, recsize, len(strs)))
        f.write(recs)
        f.write(strs)

    # Read it back: same records apart from that one field, and the new text where it should be.
    nrec2, nfield2, recsize2, recs2, strs2 = load(out)
    assert (nrec2, nfield2, recsize2) == (nrec, nfield, recsize)
    orig = load(inp)[3]
    for i in range(nrec):
        for f in range(nfield):
            a = recs2[i * recsize + f * 4:i * recsize + f * 4 + 4]
            if f != field:
                assert a == orig[i * recsize + f * 4:i * recsize + f * 4 + 4], (i, f)
        s = string_at(strs2, struct.unpack_from('<I', recs2, i * recsize + field * 4)[0])
        assert s is None or old not in s, i
    print('ok: %d record(s) renamed, string block %d -> %d bytes' % (changed, oldsize, len(strs2)))


main()
