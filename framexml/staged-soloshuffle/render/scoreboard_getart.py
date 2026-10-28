import sys, glob, os
sys.path.insert(0, r"C:\Projects\Gamedev\wow\tools\mpqpy")
sys.path.insert(0, r"C:\Projects\Gamedev\wow\clientedits\framexml\staged-centurion-ui\render")
from mpqread import MPQ
import blp

out = sys.argv[1]
base = r"C:\Projects\Gamedev\wow\clients\centurion\Data"


def prio(p):
    n = os.path.basename(p).lower().replace('.mpq', '')
    if not n.startswith('patch'):
        return 0
    if n in ('patch', 'patch-enus'):
        return 5
    tail = n.split('-')[-1]
    if tail.isdigit():
        return 10 + int(tail)
    return 20 + (ord(tail[0]) - ord('a') + 1)


archives = sorted(glob.glob(base + r"\*.MPQ") + glob.glob(base + r"\enUS\*.MPQ"), key=prio, reverse=True)
mpqs = []
for p in archives:
    try:
        mpqs.append((p, MPQ(p)))
    except Exception:
        pass


def find(path):
    for p, m in mpqs:
        try:
            d = m.extract(path)
        except Exception:
            d = None
        if d:
            return p, d
    return None, None


names = ["WorldStateFinalScoreFrame-TopLeft", "WorldStateFinalScoreFrame-TopRight", "WorldStateFinalScoreFrame-Top",
         "WorldStateFinalScoreFrame-BotLeft", "WorldStateFinalScoreFrame-BotRight", "WorldStateFinalScoreFrame-Bot",
         "WorldStateFinalScoreFrame-TopBackground", "WorldStateFinalScore-Highlight", "Icons-Classes"]
for n in names:
    p, d = find("Interface\\WorldStateFrame\\" + n + ".blp")
    if d:
        blp.Blp(d).image().save(os.path.join(out, n + ".png"))
        print(n, "from", os.path.basename(p))
    else:
        print("MISSING", n)

for path, nm in [("Interface\\Buttons\\UI-Panel-Button-Up.blp", "UI-Panel-Button-Up.png")]:
    p, d = find(path)
    if d:
        blp.Blp(d).image().save(os.path.join(out, nm))
        print(nm, "from", os.path.basename(p))

p, d = find("Interface\\FrameXML\\GlobalStrings.lua")
if d:
    open(os.path.join(out, "GlobalStrings.lua"), "wb").write(d)
    print("GlobalStrings.lua from", os.path.basename(p))
