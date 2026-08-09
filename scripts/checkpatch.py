"""Read the patch site out of the running Wow.exe to confirm AnimSpeedFix applied.

    python checkpatch.py

Wow.exe 3.3.5a 12340 only. Requires the client to be running.
"""
import ctypes
import sys

GATE_VA = 0x007388C1
ORIGINAL = bytes.fromhex("0f8bc8000000")           # jnp 0x73898F
SKIP_VA = 0x0073898F                               # where the original jnp went
WOW_TEXT = (0x00401000, 0x009DE3B3)                # Wow.exe .text range

PROCESS_VM_READ = 0x0010
PROCESS_QUERY_INFORMATION = 0x0400


def find_wow():
    k = ctypes.windll.kernel32
    psapi = ctypes.windll.psapi
    count = 4096
    arr = (ctypes.c_ulong * count)()
    needed = ctypes.c_ulong()
    psapi.EnumProcesses(ctypes.byref(arr), ctypes.sizeof(arr), ctypes.byref(needed))
    hits = []
    for pid in arr[:needed.value // ctypes.sizeof(ctypes.c_ulong)]:
        if not pid:
            continue
        h = k.OpenProcess(PROCESS_VM_READ | PROCESS_QUERY_INFORMATION, False, pid)
        if not h:
            continue
        buf = ctypes.create_unicode_buffer(1024)
        size = ctypes.c_ulong(1024)
        if ctypes.windll.kernel32.QueryFullProcessImageNameW(h, 0, buf, ctypes.byref(size)):
            if buf.value.lower().endswith("wow.exe"):
                hits.append((pid, buf.value, h))
                continue
        k.CloseHandle(h)
    return hits


def read(h, addr, n):
    k = ctypes.windll.kernel32
    buf = (ctypes.c_ubyte * n)()
    got = ctypes.c_size_t()
    ok = k.ReadProcessMemory(h, ctypes.c_void_p(addr), buf, n, ctypes.byref(got))
    return bytes(buf[:got.value]) if ok else None


def main():
    procs = find_wow()
    if not procs:
        print("Wow.exe is not running.")
        return 1
    rc = 0
    for pid, path, h in procs:
        print("PID %d  %s" % (pid, path))
        cur = read(h, GATE_VA, 6)
        if cur is None:
            print("  could not read 0x%08X (err %d)" % (GATE_VA, ctypes.GetLastError()))
            rc = 1
        elif cur[0] == 0xE9 and cur[5] == 0x90:
            rel = int.from_bytes(cur[1:5], "little", signed=True)
            dest = (GATE_VA + 5 + rel) & 0xFFFFFFFF
            print("  0x%08X = %s  -> PATCHED, jmp 0x%08X"
                  % (GATE_VA, cur.hex(" "), dest))
            if dest == SKIP_VA:
                print("     unconditional: ALL movement animations locked at 1.0x")
            elif WOW_TEXT[0] <= dest <= WOW_TEXT[1]:
                print("     destination is inside Wow.exe but not the skip target"
                      " - unexpected")
                rc = 1
            else:
                print("     conditional hook in the DLL: only the animations listed in")
                print("     AnimSpeedFix.ini LockAnimations are locked at 1.0x")
        elif cur == ORIGINAL:
            print("  0x%08X = %s  -> NOT patched: stock speed scaling is active"
                  % (GATE_VA, cur.hex(" ")))
            print("     is dinput8.dll next to Wow.exe, and Enabled=1 in AnimSpeedFix.ini?")
            rc = 1
        else:
            print("  0x%08X = %s  -> UNEXPECTED bytes; not a stock 12340 client?"
                  % (GATE_VA, cur.hex(" ")))
            rc = 1
        ctypes.windll.kernel32.CloseHandle(h)
    return rc


if __name__ == "__main__":
    raise SystemExit(main())
