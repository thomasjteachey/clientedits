#!/usr/bin/env python3
"""Which client archives carry given Interface files, in load-priority order (read-only).

Priority model (memory project_hd_off_green_cubes, verified in a live incident): only the
patch suffix counts, not the folder - patch-2..9 = 12..19, patch-A..Z = 21..46, the same
for Data\\patch-X.MPQ and Data\\enUS\\patch-enUS-X.MPQ. Higher wins.

usage: ui_carriers.py <client root> [--extract DIR] -- inner\\path ...
"""
import hashlib
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import mpqread  # noqa: E402

SKIP = {'common.mpq', 'common-2.mpq', 'expansion.mpq', 'lichking.mpq'}


def priority(fname):
    stem = fname.lower()[:-4]
    if stem.startswith('patch-enus'):
        stem = 'patch' + stem[len('patch-enus'):]
    if stem == 'patch':
        return 10
    if stem.startswith('patch-'):
        tag = stem[6:]
        if tag.isdigit():
            return 10 + int(tag)
        if len(tag) == 1 and tag.isalpha():
            return 20 + (ord(tag) - ord('a') + 1)
    return 5


args = sys.argv[1:]
sep = args.index('--')
opts, names = args[:sep], args[sep + 1:]
root = opts[0]
extract_dir = opts[opts.index('--extract') + 1] if '--extract' in opts else None

archives = []
for sub in ('Data', os.path.join('Data', 'enUS')):
    d = os.path.join(root, sub)
    for f in sorted(os.listdir(d)):
        if f.lower().endswith('.mpq') and f.lower() not in SKIP:
            archives.append((priority(f), os.path.join(sub, f), os.path.join(d, f)))
archives.sort(key=lambda a: (a[0], a[1]))

winners = {}
for prio, label, path in archives:
    try:
        m = mpqread.MPQ(path)
    except Exception as ex:  # noqa: BLE001
        print('%3d %-34s unreadable: %s' % (prio, label, ex))
        continue
    hits = []
    for n in names:
        idx = m.find(n)
        if idx is None:
            continue
        ofs, csize, fsize, flags = m.block(idx)
        if not flags & 0x80000000:
            hits.append('%s (deleted entry)' % n)
            continue
        digest = '?'
        try:
            data = m.extract(n)
            digest = hashlib.md5(data).hexdigest()[:12]
            if extract_dir:
                out = os.path.join(extract_dir, label.replace(os.sep, '_'), n.replace('\\', os.sep))
                os.makedirs(os.path.dirname(out), exist_ok=True)
                with open(out, 'wb') as f:
                    f.write(data)
        except Exception as ex:  # noqa: BLE001
            digest = 'extract failed: %s' % ex
        hits.append('%s %d bytes md5 %s' % (n.split('\\')[-1], fsize, digest))
        winners[n] = (prio, label, digest)
    print('%3d %-34s %s' % (prio, label, ' | '.join(hits) if hits else '-'))

print('\nwinner per file (highest priority carrier):')
for n in names:
    print('  %-50s %s' % (n, winners.get(n, 'none')))
