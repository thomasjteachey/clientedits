#!/usr/bin/env python3
"""Write the single .MPQ member of a patch zip to a path. usage: unzip_mpq.py <zip> <out.MPQ>"""
import shutil
import sys
import zipfile

with zipfile.ZipFile(sys.argv[1]) as z:
    members = [i for i in z.infolist() if i.filename.lower().endswith('.mpq')]
    if len(members) != 1:
        sys.exit('%s: expected one MPQ member, found %r' % (sys.argv[1], [m.filename for m in members]))
    with z.open(members[0]) as src, open(sys.argv[2], 'wb') as dst:
        shutil.copyfileobj(src, dst, 1 << 22)
