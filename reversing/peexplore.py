#!/usr/bin/env python3
"""
peexplore.py - static reverse-engineering helper for Wow.exe 3.3.5a 12340 (x86).

The client is a release build but keeps its assertion strings, which embed the
original source file names (CMovement.cpp, CWorldCollide.cpp, ...) and often the
expression that failed. Those strings are the reliable, build-specific anchors:
find the string, find the code that pushes its address, and you are inside the
function that owns it - no reliance on remembered addresses.

Subcommands:
  strings   PAT           list .rdata strings (VA + text) matching a regex
  xrefs     VA|0xVA       find code that references an address (push/mov/lea imm32)
  disasm    VA [N]        linear-disassemble N bytes (default 256) from a VA
  func      VA [MAX]      disassemble from VA until a ret/padding boundary
  around    VA [N]        disassemble the N bytes before and after a VA
  callers   VA           find `call VA` sites (direct rel32 calls)

All addresses are virtual (imagebase 0x400000). Prefix with 0x or not.
"""
import sys, re
import pefile
from capstone import Cs, CS_ARCH_X86, CS_MODE_32, CS_OP_IMM, CS_OP_MEM

EXE = r"C:\Projects\Gamedev\wow\clients\centurion\Wow.exe"

class Image:
    def __init__(self, path=EXE):
        self.pe = pefile.PE(path, fast_load=True)
        self.base = self.pe.OPTIONAL_HEADER.ImageBase
        self.data = self.pe.__data__
        self.sections = []
        for s in self.pe.sections:
            name = s.Name.rstrip(b"\x00").decode("latin1")
            va = self.base + s.VirtualAddress
            self.sections.append({
                "name": name,
                "va": va,
                "vsize": s.Misc_VirtualSize,
                "raw": s.PointerToRawData,
                "rawsize": s.SizeOfRawData,
                "end": va + max(s.Misc_VirtualSize, s.SizeOfRawData),
            })
        self.md = Cs(CS_ARCH_X86, CS_MODE_32)
        self.md.detail = True

    def sec_of(self, va):
        for s in self.sections:
            if s["va"] <= va < s["end"]:
                return s
        return None

    def va_to_off(self, va):
        s = self.sec_of(va)
        if not s:
            return None
        off = va - s["va"]
        if off >= s["rawsize"]:
            return None  # in virtual (uninitialised) tail
        return s["raw"] + off

    def read(self, va, n):
        o = self.va_to_off(va)
        if o is None:
            return None
        return self.data[o:o+n]

    def text_sections(self):
        return [s for s in self.sections if s["name"] in (".text", "")]

    def iter_strings(self, min_len=4):
        """Yield (va, text) for ASCII strings in initialised data sections."""
        rx = re.compile(rb"[\x20-\x7e]{%d,}" % min_len)
        for s in self.sections:
            if s["name"] not in (".rdata", ".data"):
                continue
            blob = self.data[s["raw"]:s["raw"]+s["rawsize"]]
            for m in rx.finditer(blob):
                yield s["va"] + m.start(), m.group().decode("latin1")


def cmd_strings(img, pat):
    rx = re.compile(pat, re.I)
    for va, txt in img.iter_strings():
        if rx.search(txt):
            print(f"0x{va:08X}  {txt}")

def _imm_refs(img, target):
    """Find instructions anywhere in text whose immediate or mem-disp == target."""
    hits = []
    for s in img.text_sections():
        code = img.data[s["raw"]:s["raw"]+s["rawsize"]]
        base = s["va"]
        # Fast pre-filter: the 4-byte LE value must appear at all.
        needle = target.to_bytes(4, "little")
        idx = 0
        seen = set()
        while True:
            j = code.find(needle, idx)
            if j < 0:
                break
            idx = j + 1
            # Disassemble a short window ending near j to attribute the ref to an insn.
            start = max(0, j - 15)
            for insn in img.md.disasm(bytes(code[start:j+4]), base + start):
                ins_end = insn.address + insn.size
                if insn.address <= base + j < ins_end and insn.address not in seen:
                    seen.add(insn.address)
                    hits.append(insn)
    return hits

def cmd_xrefs(img, va):
    for insn in _imm_refs(img, va):
        print(f"0x{insn.address:08X}  {insn.mnemonic} {insn.op_str}")

def cmd_callers(img, target):
    """Direct `call rel32` sites that resolve to target (E8 rel32)."""
    for s in img.text_sections():
        code = img.data[s["raw"]:s["raw"]+s["rawsize"]]
        base = s["va"]
        i = 0
        n = len(code)
        while i < n:
            if code[i] == 0xE8 and i + 5 <= n:
                rel = int.from_bytes(code[i+1:i+5], "little", signed=True)
                dest = (base + i + 5 + rel) & 0xFFFFFFFF
                if dest == target:
                    print(f"0x{base+i:08X}  call 0x{target:08X}")
            i += 1

def _disasm_range(img, va, n):
    code = img.read(va, n)
    if code is None:
        print(f"; 0x{va:08X} not in an initialised section", file=sys.stderr)
        return
    for insn in img.md.disasm(code, va):
        b = " ".join(f"{x:02x}" for x in insn.bytes)
        print(f"0x{insn.address:08X}  {b:<24}  {insn.mnemonic} {insn.op_str}")

def cmd_disasm(img, va, n=256):
    _disasm_range(img, va, n)

def cmd_around(img, va, n=64):
    _disasm_range(img, va - n, 2 * n)

def cmd_dataref(img, target):
    """Find every 4-byte LE occurrence of target across all sections, and print
    the 6 dwords around it (registration tables pair name-ptr with func-ptr)."""
    needle = target.to_bytes(4, "little")
    for s in img.sections:
        blob = img.data[s["raw"]:s["raw"]+s["rawsize"]]
        idx = 0
        while True:
            j = blob.find(needle, idx)
            if j < 0:
                break
            idx = j + 1
            va = s["va"] + j
            ctx = []
            for k in range(-2, 4):
                off = j + k*4
                if 0 <= off <= len(blob) - 4:
                    ctx.append(f"{int.from_bytes(blob[off:off+4],'little'):08X}")
                else:
                    ctx.append("--------")
            print(f"[{s['name']}] 0x{va:08X}  ... {' '.join(ctx)}")

def cmd_entry(img, va):
    """Scan backward for the function entry: the byte after a run of int3/nop
    padding, i.e. the previous function's tail. MSVC release pads with 0xCC."""
    o = img.va_to_off(va)
    if o is None:
        print(f"; 0x{va:08X} not readable", file=sys.stderr)
        return
    s = img.sec_of(va)
    lo = s["raw"]
    i = o
    while i > lo + 1:
        if img.data[i-1] in (0xCC, 0x90) and img.data[i-2] in (0xCC, 0x90):
            # i-1 is padding; the entry is the first non-pad byte at/after i
            j = i
            while j < o and img.data[j] in (0xCC, 0x90):
                j += 1
            va_entry = s["va"] + (j - s["raw"])
            print(f"0x{va_entry:08X}  (entry; {va - va_entry} bytes before target)")
            return
        i -= 1
    print("; no padding boundary found", file=sys.stderr)

def cmd_func(img, va, maxn=4096):
    """Disassemble until an obvious end: ret/retn followed by int3/padding, capped."""
    code = img.read(va, maxn)
    if code is None:
        print(f"; 0x{va:08X} not readable", file=sys.stderr)
        return
    prev_ret = False
    for insn in img.md.disasm(code, va):
        b = " ".join(f"{x:02x}" for x in insn.bytes)
        print(f"0x{insn.address:08X}  {b:<24}  {insn.mnemonic} {insn.op_str}")
        if prev_ret and insn.mnemonic in ("int3", "nop"):
            break
        prev_ret = insn.mnemonic.startswith("ret")

def main():
    if len(sys.argv) < 2:
        print(__doc__)
        return
    img = Image()
    cmd = sys.argv[1]
    def val(i):
        return int(sys.argv[i], 0)
    if cmd == "strings":
        cmd_strings(img, sys.argv[2])
    elif cmd == "xrefs":
        cmd_xrefs(img, val(2))
    elif cmd == "callers":
        cmd_callers(img, val(2))
    elif cmd == "disasm":
        cmd_disasm(img, val(2), val(3) if len(sys.argv) > 3 else 256)
    elif cmd == "around":
        cmd_around(img, val(2), val(3) if len(sys.argv) > 3 else 64)
    elif cmd == "func":
        cmd_func(img, val(2), val(3) if len(sys.argv) > 3 else 4096)
    elif cmd == "entry":
        cmd_entry(img, val(2))
    elif cmd == "dataref":
        cmd_dataref(img, val(2))
    else:
        print(f"unknown command: {cmd}", file=sys.stderr)
        print(__doc__)

if __name__ == "__main__":
    main()
