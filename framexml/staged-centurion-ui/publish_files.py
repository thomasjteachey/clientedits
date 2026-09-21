#!/usr/bin/env python3
"""Replace files inside a published patch archive, prove it, swap it in and bump its version.

usage: publish_files.py <spec.json>

spec: {"name": "patch-Y", "change": "centurion-ui", "stage": "/tmp/.../Y",
       "backup_dir": null | "/home/brokilodeluxe/patch-backups",
       "files": {"Interface\\FrameXML\\X.lua": "<md5 of the live copy, or null if new>"}}

Refuses unless every live copy still has the md5 the change was built from. Checks the
archive's format version and its file listing before/after (same names, plus any new
ones), reads every replaced file back, and compares sample untouched files byte for byte.
"""
import hashlib
import json
import os
import shutil
import struct
import subprocess
import sys
import zipfile

PATCHES = '/var/www/html/downloads/patches'
spec = json.load(open(sys.argv[1]))
NAME, CHANGE, STAGE = spec['name'], spec['change'], spec['stage']
FILES = spec['files']
WORK = '/tmp/centmode/publish-%s' % NAME


def md5_bytes(data):
    return hashlib.md5(data).hexdigest()


def md5(path):
    h = hashlib.md5()
    with open(path, 'rb') as f:
        for chunk in iter(lambda: f.read(1 << 20), b''):
            h.update(chunk)
    return h.hexdigest()


def sh(*args, cwd=None, ok_codes=(0,)):
    out = subprocess.run(list(args), capture_output=True, text=True, cwd=cwd)
    if out.returncode not in ok_codes:
        sys.exit('command failed (%d): %s\n%s%s' % (out.returncode, ' '.join(args), out.stdout, out.stderr))
    return out.stdout + out.stderr


def extract(mpq, inner, tag):
    dest = os.path.join(WORK, 'x', tag, inner.replace('\\', '_'))
    if os.path.isdir(dest):
        shutil.rmtree(dest)
    os.makedirs(dest)
    sh('smpq', '-x', os.path.abspath(mpq), inner, cwd=dest, ok_codes=(0, 1))
    base = inner.split('\\')[-1].lower()
    for root, _, files in os.walk(dest):
        for f in files:
            if f.lower() == base:
                with open(os.path.join(root, f), 'rb') as fh:
                    return fh.read()
    return None


def listing(mpq):
    # "    31103 2026-05-28 09:30 Interface/GlueXML/CharacterCreate.lua"
    names = []
    for line in sh('smpq', '-l', os.path.abspath(mpq)).splitlines():
        parts = line.split(None, 3)
        if len(parts) == 4 and parts[0].isdigit():
            names.append(parts[3].replace('/', '\\'))
    return names


zip_path = os.path.join(PATCHES, NAME + '.zip')
ver_path = os.path.join(PATCHES, NAME + '.version')
old_version = open(ver_path).read().strip()
major, minor = old_version.split('.')
new_version = '%s.%05d' % (major, int(minor) + 1)
print('%s %s -> %s (%s)' % (NAME, old_version, new_version, CHANGE))

staged = {}
for inner in FILES:
    path = os.path.join(STAGE, *inner.split('\\'))
    staged[inner] = md5(path)
    print('staged %-50s %s' % (inner, staged[inner]))

if os.path.isdir(WORK):
    shutil.rmtree(WORK)
os.makedirs(WORK)
with zipfile.ZipFile(zip_path) as z:
    members = z.infolist()
    assert len(members) == 1 and members[0].filename == NAME + '.MPQ', [m.filename for m in members]
    compress = members[0].compress_type
    z.extract(members[0], WORK)
mpq = os.path.join(WORK, NAME + '.MPQ')
with open(mpq, 'rb') as f:
    head = f.read(16)
assert head[:4] == b'MPQ\x1a', head[:4]
fmt_before = struct.unpack_from('<H', head, 0x0C)[0]
print('extracted %s: %d bytes, formatVersion %d, zip compression %d' % (mpq, os.path.getsize(mpq), fmt_before, compress))

names_before = listing(mpq)
lower_before = {n.lower() for n in names_before}
for inner, base in FILES.items():
    data = extract(mpq, inner, 'before')
    got = md5_bytes(data) if data is not None else None
    if got != base:
        sys.exit('live %s %s is %s, not the build base %s - rebuild first' % (NAME, inner, got, base))
wanted = {i.lower() for i in FILES}
pool = [n for n in names_before if n.lower() not in wanted and not n.startswith('(')]
samples = [pool[int(k * (len(pool) - 1) / 4)] for k in range(5)] if pool else []
sample_md5 = {}
for s in samples:
    data = extract(mpq, s, 'before')
    sample_md5[s] = md5_bytes(data) if data is not None else None
print('%d files listed; samples %s' % (len(names_before), ', '.join(samples)))

print(sh('smpq', '-a', '-f', os.path.abspath(mpq), *[i.replace('\\', '/') for i in FILES], cwd=STAGE).strip())

with open(mpq, 'rb') as f:
    head = f.read(16)
assert head[:4] == b'MPQ\x1a', head[:4]
fmt_after = struct.unpack_from('<H', head, 0x0C)[0]
assert fmt_after == fmt_before, 'formatVersion %d -> %d' % (fmt_before, fmt_after)
for inner in FILES:
    data = extract(mpq, inner, 'after')
    assert data is not None and md5_bytes(data) == staged[inner], '%s readback mismatch' % inner
for s, digest in sample_md5.items():
    data = extract(mpq, s, 'after')
    assert (md5_bytes(data) if data is not None else None) == digest, '%s changed' % s
names_after = listing(mpq)
lower_after = {n.lower() for n in names_after}
missing = lower_before - lower_after
added = lower_after - lower_before
assert not missing, 'names lost: %s' % sorted(missing)[:10]
assert added <= wanted, 'unexpected new names: %s' % sorted(added - wanted)[:10]
print('MPQ verified: formatVersion %d, %d -> %d names, %d file(s) read back, %d samples unchanged'
      % (fmt_after, len(names_before), len(names_after), len(FILES), len(sample_md5)))

new_zip = zip_path + '.new-' + CHANGE
with zipfile.ZipFile(new_zip, 'w', compression=compress) as z:
    z.write(mpq, NAME + '.MPQ')
with zipfile.ZipFile(new_zip) as z:
    assert [m.filename for m in z.infolist()] == [NAME + '.MPQ']
    h = hashlib.md5()
    with z.open(NAME + '.MPQ') as fh:
        for chunk in iter(lambda: fh.read(1 << 20), b''):
            h.update(chunk)
assert h.hexdigest() == md5(mpq), 'zip member does not match the verified MPQ'
print('zip verified: %s (%d bytes)' % (new_zip, os.path.getsize(new_zip)))

backup_dir = spec.get('backup_dir') or PATCHES
os.makedirs(backup_dir, exist_ok=True)
bak_zip = os.path.join(backup_dir, '%s.zip.bak-%s-%s' % (NAME, CHANGE, old_version))
bak_ver = os.path.join(backup_dir, '%s.version.bak-%s-%s' % (NAME, CHANGE, old_version))
if os.path.exists(bak_zip) or os.path.exists(bak_ver):
    sys.exit('backup already exists: %s' % bak_zip)
shutil.copy2(zip_path, bak_zip)
shutil.copy2(ver_path, bak_ver)
shutil.copymode(zip_path, new_zip)
os.replace(new_zip, zip_path)
tmp_ver = ver_path + '.new-' + CHANGE
with open(tmp_ver, 'w') as f:
    f.write(new_version)
shutil.copymode(ver_path, tmp_ver)
os.replace(tmp_ver, ver_path)
print('published %s (%d bytes), version %s; backups %s, %s' % (zip_path, os.path.getsize(zip_path), new_version, bak_zip, bak_ver))
shutil.rmtree(WORK)
