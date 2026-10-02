#!/usr/bin/env python3
"""Byte-level search for a 32-bit displacement in .text, then decode a window
around each hit.

Linear-sweep disassembly desynchronises on the jump tables the client embeds in
.text, so a capstone walk from the section start silently misses instructions.
Searching for the raw displacement bytes and decoding *backwards* from each hit
finds them regardless of alignment.
"""
import sys, struct
sys.path.insert(0, r"C:\Projects\Gamedev\wow\clientedits\reversing")
from peexplore import Image

img = Image()
text = next(s for s in img.sections if s["name"] == ".text")
raw = img.data[text["raw"]:text["raw"] + text["rawsize"]]
base = text["va"]

disp = int(sys.argv[1], 0)
needle = struct.pack("<i", disp)

found, start = [], 0
while True:
    i = raw.find(needle, start)
    if i < 0:
        break
    found.append(i)
    start = i + 1

print(f"{len(found)} raw occurrence(s) of displacement 0x{disp:X} in .text\n")

for i in found:
    va = base + i
    # The displacement trails a 2-3 byte opcode+modrm, so decode from a few
    # bytes back and keep whatever instruction actually covers the disp.
    shown = False
    for back in (2, 3, 6, 1, 4):
        s = i - back
        if s < 0:
            continue
        for insn in img.md.disasm(raw[s:s + 16], base + s):
            if insn.address <= va < insn.address + insn.size:
                print(f"0x{insn.address:08X}  {insn.mnemonic} {insn.op_str}")
                shown = True
            break
        if shown:
            break
    if not shown:
        print(f"0x{va:08X}  (data / undecodable)")
