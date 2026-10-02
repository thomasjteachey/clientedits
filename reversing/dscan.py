#!/usr/bin/env python3
"""Data-side helpers peexplore.py lacks: find a dword value anywhere in the
image (VA + section), and dump the dwords surrounding a VA. Used to walk Lua
registration tables, which are {const char* name, fn} arrays in .data and so
carry no code xref to the name string."""
import sys, struct
sys.path.insert(0, r"C:\Projects\Gamedev\wow\clientedits\reversing")
from peexplore import Image

img = Image()


def off_to_va(off):
    for s in img.sections:
        if s["raw"] <= off < s["raw"] + s["rawsize"]:
            return s["va"] + (off - s["raw"]), s["name"]
    return None, None


def find_dword(val):
    """Every occurrence of val as a little-endian dword, anywhere in raw data."""
    needle = struct.pack("<I", val)
    out, start = [], 0
    while True:
        i = img.data.find(needle, start)
        if i < 0:
            break
        va, sec = off_to_va(i)
        if va is not None:
            out.append((va, sec))
        start = i + 1
    return out


def dwords_at(va, count=8, before=4):
    """Dump dwords around a VA, resolving any that look like .rdata strings."""
    rows = []
    for k in range(-before, count):
        cur = va + 4 * k
        off = img.va_to_off(cur)
        if off is None:
            continue
        (v,) = struct.unpack_from("<I", img.data, off)
        note = ""
        soff = img.va_to_off(v)
        if soff is not None and 0x400000 < v < 0x1000000:
            raw = img.data[soff:soff + 48]
            end = raw.find(b"\x00")
            if end > 0:
                txt = raw[:end]
                if all(32 <= c < 127 for c in txt):
                    note = "  -> " + txt.decode("latin1")
        rows.append((cur, v, note))
    return rows


if __name__ == "__main__":
    cmd = sys.argv[1]
    arg = int(sys.argv[2], 0)
    if cmd == "find":
        for va, sec in find_dword(arg):
            print(f"0x{va:08X}  [{sec}]")
    elif cmd == "dump":
        n = int(sys.argv[3], 0) if len(sys.argv) > 3 else 8
        for cur, v, note in dwords_at(arg, n):
            print(f"0x{cur:08X}: 0x{v:08X}{note}")
