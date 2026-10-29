"""Give every arena map without a loading screen one the client already has
(owner 2026-10-29: "fill in the rest of them and make sure they're represented
in the addon"; "use your best judgement"). Ascension's own Map.dbc picks
(C:/Ascension/ExtractedMaps) guide the theme; the art is stock / patch-L.

Writes out/Map.dbc (only LoadingScreenID of the listed rows changed), prints
the live md5 for the publish spec, and renders out/screens_sheet.png."""
import hashlib
import os
import struct
import sys

sys.path.insert(0, r'C:\Projects\Gamedev\wow\clientedits\bountyboard')
import artlib  # noqa: E402
from PIL import Image, ImageDraw, ImageFont  # noqa: E402

HERE = os.path.dirname(os.path.abspath(__file__))
OUT = os.path.join(HERE, 'out')
os.makedirs(OUT, exist_ok=True)

# map id -> (screen, why). Ascension's pick in brackets where it differs.
PICKS = {
    980: ('LoadScreenAhnQiraj20man', "titan ruins in the desert [Tol'vir arena]"),
    982: ('LoadScreenUlduarRaid', 'Ulduar coliseum [Ulduar colliseum]'),
    983: ('LoadScreenAzjolNerub76', 'Nerubian empire [Nerub arena]'),
    984: ('LoadScreenNaxxramas', 'necropolis, undead war [Maldraxxus]'),
    985: ('LoadScreenNagrandArenaBattlegrounds', 'Nagrand Arena'),
    986: ('LoadScreenBladesEdgeArena', "Blade's Edge Arena"),
    1007: ('LoadScreenKarazhan', 'Karazhan (as Ascension)'),
    1008: ('LoadScreenUlduar80', "titan forge [Inventor's Library]"),
    1134: ('LoadScreenZulAman', 'temple of the cat gods [Shado-Pan]'),
    1401: ('LoadScreenIsleOfConquest', 'island fortress war [Tol Barad]'),
    1402: ('LoadScreenNexus80', 'starlit arcane sky [Obelisk]'),
    1403: ('LoadScreenNetherBattlegrounds', 'the Nether [Twisting Nether]'),
    1504: ('LoadScreenShadowFangKeep', 'gothic keep [Black Rook Hold]'),
    1552: ('LoadScreenDireMaul', "night elf ruins [Val'sharah]"),
    1683: ('LoadScreenScholomance', "a library [Inventor's Library]"),
    1684: ('LoadScreenGunDrak', "Zul'Drak troll arena [Amphitheater]"),
}


def read_dbc(data):
    n, fields, size, ssize = struct.unpack_from('<4I', data, 4)
    rows = [struct.unpack_from('<%dI' % fields, data, 20 + i * size) for i in range(n)]
    strings = data[20 + n * size:]

    def s(off):
        return strings[off:strings.index(b'\0', off)].decode('utf-8', 'replace')
    return n, fields, size, rows, s


def archive_file(archive_name, inner):
    for a in artlib.archives():
        if os.path.basename(a).lower() == archive_name.lower():
            return artlib.mpq(a).extract(inner)
    raise SystemExit('no archive ' + archive_name)


# Every LoadingScreens.dbc a client may read must have the row: patch-L (the
# HD pack, highest) and patch-enUS-A.
screen_ids = {}
for arch in ('patch-L.MPQ', 'patch-enUS-A.MPQ'):
    data = archive_file(arch, 'DBFilesClient\\LoadingScreens.dbc')
    _, _, _, rows, s = read_dbc(data)
    ids = {}
    for r in rows:
        base = os.path.splitext(s(r[2]).split('\\')[-1])[0].lower()
        ids[base] = r[0]
    screen_ids[arch] = ids
chosen = {}
for mid, (screen, _) in PICKS.items():
    got = {arch: ids.get(screen.lower()) for arch, ids in screen_ids.items()}
    if len(set(got.values())) != 1 or None in got.values():
        raise SystemExit('screen %s: %s' % (screen, got))
    chosen[mid] = got['patch-enUS-A.MPQ']

data = bytearray(archive_file('patch-enUS-A.MPQ', 'DBFilesClient\\Map.dbc'))
print('live Map.dbc md5', hashlib.md5(bytes(data)).hexdigest(), len(data))
n, fields, size, rows, s = read_dbc(bytes(data))
assert fields == 66, fields
done = set()
for i, r in enumerate(rows):
    if r[0] in chosen:
        off = 20 + i * size + 57 * 4
        old = struct.unpack_from('<I', data, off)[0]
        struct.pack_into('<I', data, off, chosen[r[0]])
        print('%5d %-30s %4d -> %4d %s' % (r[0], s(r[5]), old, chosen[r[0]], PICKS[r[0]][0]))
        done.add(r[0])
missing = set(chosen) - done
assert not missing, missing
open(os.path.join(OUT, 'Map.dbc'), 'wb').write(bytes(data))
print('new  Map.dbc md5', hashlib.md5(bytes(data)).hexdigest())

# contact sheet: every arena with its picture (4:3), the Ascension hint below
font = ImageFont.truetype(r'C:\Projects\Gamedev\wow\clientedits\bountyboard\art\FRIZQT__.TTF', 15)
W, H, PADX, PADY = 320, 240, 16, 52
cols = 4
items = sorted(PICKS.items())
sheet = Image.new('RGB', (cols * (W + PADX) + PADX, ((len(items) + cols - 1) // cols) * (H + PADY) + PADX), (26, 26, 32))
d = ImageDraw.Draw(sheet)
for k, (mid, (screen, why)) in enumerate(items):
    raw, _ = artlib.extract('Interface\\Glues\\LoadingScreens\\%s.blp' % screen)
    import blp  # noqa: E402  (artlib put its folder on the path)
    im = blp.Blp(raw).image().convert('RGB').resize((W, H))
    x, y = PADX + (k % cols) * (W + PADX), PADX + (k // cols) * (H + PADY)
    sheet.paste(im, (x, y))
    d.text((x, y + H + 4), '%d %s' % (mid, s(next(r for r in rows if r[0] == mid)[5])), font=font, fill=(255, 209, 0))
    d.text((x, y + H + 24), why, font=font, fill=(200, 200, 200))
sheet.save(os.path.join(OUT, 'screens_sheet.png'))
print('sheet ok')
