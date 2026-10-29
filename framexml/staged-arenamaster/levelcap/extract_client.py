"""Copy the three DBCs out of the local client's patch-enUS-A into ./client/."""
import hashlib
import os
import sys

sys.path.insert(0, r'C:\Projects\Gamedev\wow\clientedits\bountyboard')
import artlib  # noqa: E402

HERE = os.path.dirname(os.path.abspath(__file__))
OUT = os.path.join(HERE, 'client')
os.makedirs(OUT, exist_ok=True)
for a in artlib.archives():
    if os.path.basename(a) == 'patch-enUS-A.MPQ':
        m = artlib.mpq(a)
        for n in ('PvpDifficulty.dbc', 'BattlemasterList.dbc', 'Map.dbc'):
            d = m.extract('DBFilesClient\\' + n)
            open(os.path.join(OUT, n), 'wb').write(d)
            print(n, hashlib.md5(d).hexdigest())
