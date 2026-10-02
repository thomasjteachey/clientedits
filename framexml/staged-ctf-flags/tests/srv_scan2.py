#!/usr/bin/env python3
"""Read-only carrier scan of zipped patch archives, deflated or stored.

usage: srv_scan2.py <zip> [<zip> ...]

Each .MPQ member is decompressed to a temp file under OUT/tmp, scanned for the BG UI
files with mpqread (hash lookup, the same way the client resolves a path), every hit is
extracted to OUT/x/<zip basename>/<inner path>, and the temp MPQ is deleted. One line per
archive. `smpq -l` output for Interface files is appended so names that only differ in
directory or case are visible too.
"""
import hashlib
import os
import shutil
import subprocess
import sys
import time
import zipfile

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import mpqread  # noqa: E402

BS = chr(92)
OUT = '/tmp/centmode/bffmerge'
NAMES = [
    'Interface/FrameXML/BattlefieldFrame.lua', 'Interface/FrameXML/BattlefieldFrame.xml',
    'Interface/FrameXML/PVPBattlegroundFrame.lua', 'Interface/FrameXML/PVPBattlegroundFrame.xml',
    'Interface/FrameXML/WorldStateFrame.lua', 'Interface/FrameXML/WorldStateFrame.xml',
    'Interface/FrameXML/FrameXML.toc', 'Interface/FrameXML/WorldMapFrame.lua',
    'Interface/FrameXML/PVPFrame.lua', 'Interface/FrameXML/PVPFrame.xml',
    'Interface/AddOns/Blizzard_BattlefieldMinimap/Blizzard_BattlefieldMinimap.lua',
    'Interface/AddOns/Blizzard_BattlefieldMinimap/Blizzard_BattlefieldMinimap.toc',
]
NAMES = [n.replace('/', BS) for n in NAMES]
SHORT = {n: n.split(BS)[-1].replace('Blizzard_BattlefieldMinimap', 'BFMinimap') for n in NAMES}
KEYWORDS = ('battlefield', 'pvpbattleground', 'worldstate', 'worldmapframe', 'framexml.toc', 'pvpframe')


def scan_mpq(label, path):
    m = mpqread.MPQ(path)
    hits = []
    for n in NAMES:
        idx = m.find(n)
        if idx is None:
            continue
        ofs, csize, fsize, flags = m.block(idx)
        if not flags & 0x80000000:
            hits.append('%s:deleted' % SHORT[n])
            continue
        try:
            data = m.extract(n)
            digest = hashlib.md5(data).hexdigest()[:12]
            out = os.path.join(OUT, 'x', label, *n.split(BS))
            os.makedirs(os.path.dirname(out), exist_ok=True)
            with open(out, 'wb') as f:
                f.write(data)
        except Exception as ex:  # noqa: BLE001
            digest = 'ERR(%s)' % ex
        hits.append('%s:%d:%s' % (SHORT[n], fsize, digest))
    m.f.close()
    listed = []
    out = subprocess.run(['smpq', '-l', path], capture_output=True, text=True)
    for line in out.stdout.splitlines():
        parts = line.split(None, 3)
        if len(parts) == 4 and parts[0].isdigit() and parts[3].lower().startswith('interface'):
            if any(k in parts[3].lower() for k in KEYWORDS):
                listed.append('%s %s %s %s' % (parts[0], parts[1], parts[2], parts[3]))
    return hits, listed


tmpdir = os.path.join(OUT, 'tmp')
os.makedirs(tmpdir, exist_ok=True)
for zpath in sys.argv[1:]:
    label = os.path.basename(zpath)
    stamp = time.strftime('%Y-%m-%d %H:%M', time.localtime(os.path.getmtime(zpath)))
    try:
        with zipfile.ZipFile(zpath) as z:
            infos = [i for i in z.infolist() if i.filename.lower().endswith('.mpq')]
            if not infos:
                print('%-40s [%s] no MPQ inside' % (label, stamp))
                continue
            for info in infos:
                tmp = os.path.join(tmpdir, info.filename)
                with z.open(info) as src, open(tmp, 'wb') as dst:
                    shutil.copyfileobj(src, dst, 1 << 22)
                try:
                    hits, listed = scan_mpq(label, tmp)
                finally:
                    os.unlink(tmp)
                print('%-40s [%s] %-18s %s' % (label, stamp, info.filename, ' | '.join(hits) if hits else '-'))
                for entry in listed:
                    print('%60s smpq -l: %s' % ('', entry))
    except Exception as ex:  # noqa: BLE001
        print('%-40s [%s] ERROR %s' % (label, stamp, ex))
    sys.stdout.flush()
