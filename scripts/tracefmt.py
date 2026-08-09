"""Format and analyse an AnimSpeedFix trace.

    python tracefmt.py AnimTrace.csv              # analysis + combat timeline
    python tracefmt.py AnimTrace.csv --analyze    # analysis only
    python tracefmt.py AnimTrace.csv --all        # don't filter noisy animations

The analysis answers the question the trace exists for:

  * packet -> swing latency: how long after SMSG_ATTACKERSTATEUPDATE (0x14A)
    does the attack animation actually start?
  * swing intervals: is the swing rhythm steady, or does one get skipped and
    two arrive together?
  * which opcodes shared the dispatch batch with each swing.

A skipped-then-doubled swing shows up as one interval near 2x the others.
"""
import os
import re
import struct
import sys
from collections import Counter

OPCODES_H = r"C:\Projects\Gamedev\wow\servers\tc-lplus\src\server\game\Server\Protocol\Opcodes.h"
ANIM_DBC = r"C:\Projects\Gamedev\wow\clients\centurion\dbc\AnimationData.dbc"

ATTACK_UPDATE = 0x14A
PROC_OPCODES = {0x131, 0x132, 0x24E, 0x250}
SAME_BATCH_MS = 5.0
# animations that are re-played constantly and bury everything else
NOISE_ANIMS = {0, 15, 147}


def load_opcodes(path=OPCODES_H):
    names = {}
    try:
        text = open(path, "r", errors="replace").read()
    except OSError:
        return names
    for m in re.finditer(r"^\s*(SMSG_\w+|CMSG_\w+)\s*=\s*(0x[0-9A-Fa-f]+|\d+)", text, re.M):
        try:
            names[int(m.group(2), 0)] = m.group(1)
        except ValueError:
            pass
    return names


def load_anims(path=ANIM_DBC):
    names = {}
    try:
        d = open(path, "rb").read()
    except OSError:
        return names
    magic, nrec, nfld, recsz, _ = struct.unpack_from("<4sIIII", d, 0)
    if magic != b"WDBC":
        return names
    off, sb = 20, 20 + nrec * recsz
    for i in range(nrec):
        f = struct.unpack_from("<%dI" % nfld, d, off + i * recsz)
        if f[1]:
            e = d.index(b"\0", sb + f[1])
            names[f[0]] = d[sb + f[1]:e].decode(errors="replace")
    return names


def parse(path):
    freq = start = None
    rows = []
    for line in open(path, "r", errors="replace"):
        line = line.strip()
        if not line:
            continue
        if line.startswith("#"):
            m = re.search(r"qpc_freq=(\d+)", line)
            if m:
                freq = int(m.group(1))
            m = re.search(r"qpc_start=(\d+)", line)
            if m:
                start = int(m.group(1))
            continue
        p = line.split(",")
        if len(p) < 4:
            continue
        try:                       # 5th column (model) added later; tolerate 4
            rows.append((int(p[0]), p[1], int(p[2]), int(p[3]),
                         int(p[4]) if len(p) > 4 else 0))
        except ValueError:
            continue
    if freq is None or start is None:
        raise SystemExit("trace header missing qpc_freq/qpc_start")
    return freq, start, rows


def main():
    args = [a for a in sys.argv[1:] if not a.startswith("--")]
    flags = {a for a in sys.argv[1:] if a.startswith("--")}
    if not args:
        print(__doc__)
        return 2
    if not os.path.exists(args[0]):
        print("no such file: %s" % args[0])
        return 2

    freq, start, rows = parse(args[0])
    ops, anims = load_opcodes(), load_anims()
    ms = lambda t: (t - start) * 1000.0 / freq
    attack_ids = {i for i, n in anims.items() if n.lower().startswith("attack")}

    updates = [(ms(t), c) for t, k, a, b, c in rows if k == "OP" and a == ATTACK_UPDATE]
    swings = [(ms(t), a, c) for t, k, a, b, c in rows if k == "ANIM" and a in attack_ids]

    print("=== trace summary ===")
    print("  records %d over %.1f s" % (len(rows), ms(rows[-1][0]) / 1000 if rows else 0))
    print("  SMSG_ATTACKERSTATEUPDATE : %d" % len(updates))
    print("  attack animations        : %d" % len(swings))
    if not updates:
        print("\n  No 0x14A. Did you actually melee something with [Trace] Opcodes=1?")
        return 0

    print("\n=== packet -> swing animation latency ===")
    lat = []
    for u, _ in updates:
        nxt = next((s for s, _a, _c in swings if s >= u), None)
        if nxt is None:
            print("  %10.3f ms  -> no attack animation followed" % u)
            continue
        lat.append(nxt - u)
        print("  %10.3f ms  -> +%.3f ms" % (u, nxt - u))
    if lat:
        print("  min %.3f  max %.3f ms" % (min(lat), max(lat)))
        if max(lat) < 5.0:
            print("  => the client acts on the packet immediately; animation is NOT deferred")

    print("\n=== swing intervals (0x14A to 0x14A) ===")
    times = [u for u, _ in updates]
    gaps = [times[i] - times[i - 1] for i in range(1, len(times))]
    if not gaps:
        print("  only one swing captured - nothing to compare")
    else:
        base = sorted(gaps)[len(gaps) // 2]
        for g in gaps:
            flag = ""
            if base > 0 and g > base * 1.6:
                flag = "  <<< SKIPPED SWING (%.1fx the median)" % (g / base)
            print("  %8.1f ms%s" % (g, flag))
        print("  median %.1f ms" % base)
        if not any(g > base * 1.6 for g in gaps):
            print("  => rhythm is steady; this capture does NOT contain the bug")

    print("\n=== opcodes sharing each swing's dispatch batch ===")
    for u, _ in updates:
        near = [(ms(t) - u, a) for t, k, a, b, c in rows
                if k == "OP" and a != ATTACK_UPDATE and 0 <= ms(t) - u <= SAME_BATCH_MS]
        procs = [a for _d, a in near if a in PROC_OPCODES]
        tag = "  PROC IN BATCH" if procs else ""
        print("  %10.3f ms%s" % (u, tag))
        for d, a in near[:6]:
            print("        +%6.3f ms  %s (0x%X)" % (d, ops.get(a, "?"), a))

    # ---- animation slots: do attacks and casts share a track? ----
    slots = [(ms(t), a, b, c) for t, k, a, b, c in rows if k == "SLOT"]
    if slots:
        anim_rows = [(ms(t), a, c) for t, k, a, b, c in rows if k == "ANIM"]
        by_anim = {}
        for at, aid, model in anim_rows:
            # the sequence setter runs microseconds after PlayAnimation
            near = [s for s in slots if s[3] == model and 0 <= s[0] - at <= 2.0]
            if near:
                by_anim.setdefault(aid, set()).add(near[0][2])

        print("\n=== animation slots (track each animation was written to) ===")
        atk_slots, cast_slots = set(), set()
        for aid in sorted(by_anim):
            nm = anims.get(aid, "?")
            ss = sorted(by_anim[aid])
            low = nm.lower()
            if low.startswith("attack"):
                atk_slots |= set(ss)
            if low.startswith(("spellcast", "readyspell", "channelcast")):
                cast_slots |= set(ss)
            print("  %-22s id=%-4d slot(s) %s" % (nm, aid, ss))

        print("\n  attack animations use slot(s): %s" % (sorted(atk_slots) or "none seen"))
        print("  cast   animations use slot(s): %s" % (sorted(cast_slots) or "none seen"))
        overlap = atk_slots & cast_slots
        if not atk_slots or not cast_slots:
            print("  => not enough data; need a capture with both in it")
        elif overlap:
            print("  => SHARED slot(s) %s - a cast CAN overwrite a swing;" % sorted(overlap))
            print("     SwingGuard is the right shape, tune WindowMs")
        else:
            print("  => DISJOINT slots - a cast can NOT overwrite a swing.")
            print("     SwingGuard is treating a coincidence; the real cause is")
            print("     elsewhere (look at the blend logic around 0x826C95).")

    if "--analyze" in flags:
        return 0

    print("\n=== combat timeline ===")
    keep = lambda k, a: k == "OP" or (a not in NOISE_ANIMS or "--all" in flags)
    marks = [i for i, (t, k, a, b, c) in enumerate(rows)
             if (k == "OP" and a == ATTACK_UPDATE) or (k == "ANIM" and a in attack_ids)]
    if not marks:
        return 0
    lo, hi = ms(rows[max(0, marks[0])][0]) - 500, ms(rows[marks[-1]][0]) + 1500
    for t, k, a, b, c in rows:
        if not (lo <= ms(t) <= hi) or not keep(k, a):
            continue
        if k == "OP":
            detail = "%-34s 0x%X" % (ops.get(a, "?"), a)
        else:
            rate = struct.unpack("<f", struct.pack("<I", b))[0]
            detail = "%-34s id=%-4d rate=%.2f model=%08X" % (anims.get(a, "?"), a, rate, c)
        star = " <<<" if (k == "OP" and a == ATTACK_UPDATE) or (k == "ANIM" and a in attack_ids) else ""
        print("  %10.3f %-5s %s%s" % (ms(t), k, detail, star))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
