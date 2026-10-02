"""Hand-built pixel-art redraw of the owner's logo (same composition, clean pixels).

The generated original reads as AI because nothing in it is deliberate: the crest is
random colour, the laurels and helmet are lopsided, every letter is shaded differently.
Here every part is built on purpose on an 89x49 grid:
  - six colours only, used the same way everywhere (cream top light -> dark red bottom)
  - exact left/right symmetry about column 44 (the halves are mirrored, not redrawn)
  - one pixel font, every letter lit by the same bands
  - a one-pixel black keyline around the whole silhouette
Scaled x5 with hard edges to 445x245 inside the 512x256 texture.
"""
import math

import numpy as np
from PIL import Image, ImageDraw

W, H = 89, 49
CX = 44
PAL = {
    'K': (0, 0, 0),
    'D': (128, 32, 0),
    'R': (208, 64, 0),
    'O': (248, 136, 0),
    'Y': (252, 204, 48),
    'C': (255, 244, 196),
}
grid = [['.'] * W for _ in range(H)]


def put(x, y, c):
    if 0 <= x < W and 0 <= y < H:
        grid[y][x] = c


def put_sym(x, y, c):
    put(x, y, c)
    put(2 * CX - x, y, c)


def stamp_sym(rows, ox, oy):
    for y, row in enumerate(rows):
        for x, c in enumerate(row):
            if c != '.':
                put_sym(ox + x, oy + y, c)


def poly_mask(pts):
    m = Image.new('1', (W, H), 0)
    ImageDraw.Draw(m).polygon(pts, fill=1)
    return np.asarray(m, bool)


# ---------------------------------------------------------------- crest (behind the helmet)
def crest():
    """A fan of bristle bundles: each bundle dark at the root, gold at the tip, with a dark
    seam between bundles so the fan reads as bristles and not a blob."""
    cy = 14
    bundles = 6                                        # per side
    for y in range(0, 14):
        for x in range(CX - 15, CX + 16):
            dx, dy = abs(x - CX), cy - y
            r = math.hypot(dx * 0.95, dy)
            a = math.atan2(dy, dx + 0.001)             # 0 at the side, pi/2 straight up
            pos = a / (math.pi / 2) * bundles
            k = min(int(pos), bundles - 1)
            tip = 13.2 + (0.9 if k % 2 == 0 else 0)
            if r > tip or r < 6 or dy < 1:
                continue
            if pos - k < 0.16 and k > 0:
                c = 'D'                                # seam between bundles
            elif r > tip - 2.0:
                c = 'Y' if k % 2 == 0 else 'O'         # gold tips
            elif r > 8.5:
                c = 'R'                                # red body, like the original's crest
            else:
                c = 'D'
            put(x, y, c)


# ---------------------------------------------------------------- helmet
HELMET_LEFT = [           # left half incl. the centre column (index 8)
    "....KKKKK",
    "..KKOYYYC",
    ".KOYYYYCC",
    ".KOYYYYCC",
    "KROYYYYYC",
    "KROOYYYYY",
    "KRRKKKKKK",
    "KRRKKKKKK",
    "KRROOOOKK",
    "KRROOOOKK",
    "KRROOOOKK",
    ".KRROOOKK",
    ".KDRROOKK",
    "..KDRRRKK",
    "...KDDRKK",
    "....KKKKK",
]


def helmet():
    rows = [r + r[:-1][::-1] for r in HELMET_LEFT]
    for y, row in enumerate(rows):
        for x, c in enumerate(row):
            if c != '.':
                put(CX - 8 + x, 9 + y, c)


# ---------------------------------------------------------------- laurels
LEAF_UP = ["CY.", "YYO", ".OO"]
LEAF_DOWN = [".OO", "YOR", "YR."]


def laurels():
    stem = []
    for x in range(34, 11, -1):
        y = 23 - round((34 - x) * 4 / 22)
        stem.append((x, y))
        put_sym(x, y, 'R')
    for i, (x, y) in enumerate(stem):
        if i % 4 == 1:
            stamp_sym(LEAF_UP, x - 2, y - 3)
        if i % 4 == 3:
            stamp_sym(LEAF_DOWN, x - 2, y + 1)
    x, y = stem[-1]
    stamp_sym(["YY", "OR"], x - 2, y - 1)                   # the branch's end bud


# ---------------------------------------------------------------- banner
TOP, BOT, MID = 25, 45, 35


def banner():
    outer = poly_mask([(1, MID), (11, TOP), (77, TOP), (87, MID), (77, BOT), (11, BOT)])
    inner = poly_mask([(5, MID), (13, TOP + 3), (75, TOP + 3), (83, MID), (75, BOT - 3), (13, BOT - 3)])
    for y in range(H):
        for x in range(W):
            if inner[y, x]:
                grid[y][x] = 'K'
            elif outer[y, x]:
                # the rim is lit from above: cream edge, gold, orange, red, dark red at the foot
                grid[y][x] = ('C' if y <= TOP else 'Y' if y < MID - 3 else 'O' if y < MID + 3
                              else 'R' if y < BOT else 'D')
    stamp_sym(["OOOOOOOOO", ".RRRRRRRR", "..DDDDDDD"], CX - 8, BOT + 1)   # the tab under the banner


# ---------------------------------------------------------------- lettering
FONT = {
    'C': [".#####", "##..##", "##....", "##....", "##....", "##....", "##....", "##....", "##....", "##..##", ".#####"],
    'E': ["######", "##....", "##....", "##....", "#####.", "##....", "##....", "##....", "##....", "##....", "######"],
    'N': ["##..##", "###.##", "###.##", "######", "##.###", "##.###", "##..##", "##..##", "##..##", "##..##", "##..##"],
    'T': ["######", "..##..", "..##..", "..##..", "..##..", "..##..", "..##..", "..##..", "..##..", "..##..", "..##.."],
    'U': ["##..##", "##..##", "##..##", "##..##", "##..##", "##..##", "##..##", "##..##", "##..##", "##..##", ".####."],
    'R': ["#####.", "##..##", "##..##", "##..##", "#####.", "####..", "##.##.", "##.##.", "##..##", "##..##", "##..##"],
    'I': ["####", ".##.", ".##.", ".##.", ".##.", ".##.", ".##.", ".##.", ".##.", ".##.", "####"],
    'O': [".####.", "##..##", "##..##", "##..##", "##..##", "##..##", "##..##", "##..##", "##..##", "##..##", ".####."],
}
BANDS = ['C', 'Y', 'Y', 'Y', 'Y', 'O', 'O', 'O', 'O', 'R', 'R']


def lettering(word='CENTURION', y0=30, gap=1):
    total = sum(len(FONT[c][0]) for c in word) + gap * (len(word) - 1)
    x = CX - total // 2
    ink = set()
    for ch in word:
        for gy, row in enumerate(FONT[ch]):
            for gx, v in enumerate(row):
                if v == '#':
                    ink.add((x + gx, y0 + gy))
        x += len(FONT[ch][0]) + gap
    for (px, py) in ink:                                   # a dark drop shadow, down and right
        if (px + 1, py + 1) not in ink:
            put(px + 1, py + 1, 'D')
    for (px, py) in ink:
        put(px, py, BANDS[py - y0])


def keyline():
    """One black pixel around everything, so every shape reads against any background."""
    filled = {(x, y) for y in range(H) for x in range(W) if grid[y][x] != '.'}
    for x, y in filled:
        for dx, dy in ((1, 0), (-1, 0), (0, 1), (0, -1)):
            if (x + dx, y + dy) not in filled:
                put(x + dx, y + dy, 'K')


crest()
laurels()
helmet()
banner()
lettering()
keyline()

img = Image.new('RGBA', (W, H), (0, 0, 0, 0))
for y in range(H):
    for x in range(W):
        if grid[y][x] != '.':
            img.putpixel((x, y), PAL[grid[y][x]] + (255,))
img = img.crop(img.getchannel('A').getbbox())
img.save('logo_redraw_lowres.png')
scale = min(512 // img.width, 256 // img.height)
big = img.resize((img.width * scale, img.height * scale), Image.NEAREST)
out = Image.new('RGBA', (512, 256), (0, 0, 0, 0))
out.alpha_composite(big, ((512 - big.width) // 2, (256 - big.height) // 2))
out.save('centurion_logo_redraw.png')
print('%dx%d x%d' % (img.width, img.height, scale))
