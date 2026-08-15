import sys
import pefile
from capstone import Cs, CS_ARCH_X86, CS_MODE_32, CS_OP_IMM, CS_OP_MEM

EXE = r"C:\Projects\Gamedev\wow\clients\centurion\Wow.exe"
pe = pefile.PE(EXE, fast_load=True)
base = pe.OPTIONAL_HEADER.ImageBase
data = pe.__data__
md = Cs(CS_ARCH_X86, CS_MODE_32)
md.detail = True

# target displacement to watch
DISP = int(sys.argv[1], 0) if len(sys.argv) > 1 else 0x10
LO = int(sys.argv[2], 0) if len(sys.argv) > 2 else 0x81B000
HI = int(sys.argv[3], 0) if len(sys.argv) > 3 else 0x836000

for s in pe.sections:
    name = s.Name.rstrip(b"\x00").decode("latin1")
    if name != ".text":
        continue
    va = base + s.VirtualAddress
    raw = s.PointerToRawData
    size = s.SizeOfRawData
    code = data[raw:raw+size]
    # disassemble the whole section linearly from LO
    startoff = LO - va
    for insn in md.disasm(bytes(code[startoff:HI-va]), LO):
        touched = False
        immval = None
        for op in insn.operands:
            if op.type == CS_OP_MEM and op.mem.disp == DISP and op.mem.base != 0 and op.mem.index == 0:
                touched = True
            if op.type == CS_OP_IMM:
                immval = op.imm
        if touched and insn.mnemonic in ("test","or","and","xor","mov","bt","bts","btr","cmp"):
            extra = ""
            if immval is not None:
                extra = f"   imm=0x{immval & 0xffffffff:08x}"
            print(f"0x{insn.address:08X}  {insn.mnemonic} {insn.op_str}{extra}")
