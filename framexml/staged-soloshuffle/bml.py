"""BattlemasterList.dbc (3.3.5a, 32 fields) reader / row adder.

usage:
  python bml.py dump <file> [id ...]
  python bml.py add  <in> <out> <newId> <copyFromId> <name> [maxGroupSize]

Field layout (WotLK 12340): 0 ID, 1-8 MapID[8], 9 InstanceType, 10 GroupsAllowed,
11-26 Name_lang[16], 27 Name_lang_flags, 28 MaxGroupSize, 29 HolidayWorldState,
30 MinLevel, 31 MaxLevel.
"""
import struct
import sys

NAME_FIRST, NAME_COUNT = 11, 16


def read(path):
    data = open(path, "rb").read()
    magic, records, fields, size, strsize = struct.unpack_from("<4s4I", data, 0)
    assert magic == b"WDBC", magic
    assert fields == 32 and size == 128, (fields, size)
    rows = []
    off = 20
    for _ in range(records):
        rows.append(list(struct.unpack_from("<32I", data, off)))
        off += size
    strings = data[off:off + strsize]
    return rows, strings


def string_at(strings, offset):
    end = strings.index(b"\0", offset)
    return strings[offset:end].decode("utf-8", "replace")


def write(path, rows, strings):
    out = bytearray(struct.pack("<4s4I", b"WDBC", len(rows), 32, 128, len(strings)))
    for row in rows:
        out += struct.pack("<32I", *row)
    out += strings
    open(path, "wb").write(out)


def signed(value):
    return value - (1 << 32) if value & 0x80000000 else value


def dump(path, ids):
    rows, strings = read(path)
    print("records", len(rows), "strings", len(strings))
    for row in rows:
        if ids and row[0] not in ids:
            continue
        maps = [signed(v) for v in row[1:9]]
        print(row[0], "maps", maps, "type", row[9], "groups", row[10],
              "name", repr(string_at(strings, row[NAME_FIRST])), "flags", hex(row[27]),
              "maxgrp", row[28], "holiday", row[29], "lvl", row[30], row[31])


def add(src, dst, new_id, copy_from, name, max_group=None):
    rows, strings = read(src)
    ids = [row[0] for row in rows]
    if new_id in ids:
        sys.exit("id %d already present" % new_id)
    base = next((row for row in rows if row[0] == copy_from), None)
    if base is None:
        sys.exit("no row %d to copy" % copy_from)
    row = list(base)
    row[0] = new_id
    offset = len(strings)
    strings = strings + name.encode("utf-8") + b"\0"
    # enUS only, like every other custom row; the rest point at the empty string
    for i in range(NAME_COUNT):
        row[NAME_FIRST + i] = 0
    row[NAME_FIRST] = offset
    if max_group is not None:
        row[28] = max_group
    # the level band the custom arenas carry (the arena templates' own)
    row[30], row[31] = 10, 80
    # appended: the file keeps its rows in the order they were added
    rows.append(row)
    write(dst, rows, strings)
    print("added", new_id, "copied from", copy_from, "->", dst)


if __name__ == "__main__":
    cmd = sys.argv[1]
    if cmd == "dump":
        dump(sys.argv[2], [int(x) for x in sys.argv[3:]])
    elif cmd == "add":
        add(sys.argv[2], sys.argv[3], int(sys.argv[4]), int(sys.argv[5]), sys.argv[6],
            int(sys.argv[7]) if len(sys.argv) > 7 else None)
    else:
        sys.exit(__doc__)
