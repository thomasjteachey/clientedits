"""Hand fixes on the regridded scene (lowres.png, 479x205) -> fixed.png.

Each fix targets a specific AI error, in grid coordinates:
  1. paladin: a second body fused onto his back (with a floating gold head) -> cut away,
     and a flanged mace he actually grips, raised over his shoulder
  2. Alliance banner: gold mush emblem -> a symmetric crowned shield
  3. Horde banner: lopsided emblem -> mirrored about its own centre line
  4. troll hunter: garbled bow and string -> redrawn bow, string to his drawing hand, nocked arrow
"""
import json

import numpy as np
from PIL import Image

img = np.asarray(Image.open('lowres.png').convert('RGB')).copy()
pal = [tuple(c) for c in json.load(open('palette.json'))]
BLACK = min(pal, key=sum)
HILL = tuple(img[112, 45])          # the cliff face behind the paladin
GOLD = (245, 165, 28)               # snapped to the palette below (sampling a pixel
LGOLD = (252, 200, 72)              # position breaks whenever the palette is rebuilt)
DGOLD = (184, 96, 16)               # shadow gold, snapped to the palette below
BROWN = (110, 62, 30)               # hammer haft
NAVY = tuple(img[40, 82])
RED = tuple(img[45, 400])


def snap(c):
    return min(pal, key=lambda p: sum((a - b) ** 2 for a, b in zip(p, c)))


GOLD, LGOLD, DGOLD, BROWN = snap(GOLD), snap(LGOLD), snap(DGOLD), snap(BROWN)


def put(x, y, c):
    img[y, x] = c


def stamp(rows, ox, oy, key):
    for y, row in enumerate(rows):
        for x, ch in enumerate(row):
            if ch != '.':
                put(ox + x, oy + y, key[ch])


# 1. paladin: cut away the second body the AI fused onto his back ------------------
# Everything left of his own torso (x < 37) between his shoulders and feet was that body
# plus its floating gold head. Refill it with the cliff/ground just left of the cut
# (columns 0-11, no figures), mirrored back and forth so the texture continues, then
# close his silhouette with a black edge.
CUT_X0, CUT_X1, CUT_Y0, CUT_Y1 = 12, 37, 104, 160
strip = img[CUT_Y0:CUT_Y1, 0:CUT_X0].copy()
for i, x in enumerate(range(CUT_X0, CUT_X1)):
    k = i % (2 * CUT_X0)
    col = CUT_X0 - 1 - k if k < CUT_X0 else k - CUT_X0     # 11,10..0,0,1..11,11,10..
    img[CUT_Y0:CUT_Y1, x] = strip[:, col]
for y in range(116, 156):
    xs = [x for x in range(CUT_X1, CUT_X1 + 8) if sum(int(v) for v in img[y, x]) < 60]
    if xs:                                           # his outline already starts here
        continue
    put(CUT_X1 - 1, y, BLACK)

# ...and give him a weapon he actually holds: a flanged mace raised back over his
# shoulder, the gauntlet gripping the haft right at his back pauldron.
STEEL, LSTEEL = snap((150, 150, 165)), snap((225, 225, 230))


def line(x0, y0, x1, y1):
    n = max(abs(x1 - x0), abs(y1 - y0))
    return [(round(x0 + (x1 - x0) * t / n), round(y0 + (y1 - y0) * t / n)) for t in range(n + 1)]


WOOD = snap((214, 150, 84))                         # light tan: the cliff behind is brown
haft = line(36, 124, 31, 114)
for x, y in haft:                                   # outline first, then the wood
    for dx in (-1, 0, 1, 2):
        for dy in (-1, 0, 1):
            put(x + dx, y + dy, BLACK)
for x, y in haft:
    put(x, y, WOOD)
    put(x + 1, y, WOOD)
stamp(["..K.K.K..",
       ".KKKKKKK.",
       "KLLLSSSSK",
       "KLSSSSSSK",
       "KGGGGGGGK",
       "KSSSSSSSK",
       "KSSSSSSDK",
       ".KKKKKKK.",
       "..K.K.K.."], 26, 104, {'K': BLACK, 'L': LSTEEL, 'S': STEEL, 'G': GOLD, 'D': BLACK})
stamp([".KKKKK",
       "KGYYGK",
       "KGGGGK",
       "KGGGGK",
       ".KKKK."], 34, 121, {'K': BLACK, 'G': GOLD, 'Y': LGOLD})   # gauntlet on the haft

# 2. Alliance banner emblem ----------------------------------------------------
img[38:61, 80:96] = NAVY
half = [                      # left half, mirrored about the banner's centre (87.5)
    "......Y",
    "..Y...Y",
    "..YY.YY",
    "..YYYYY",
    ".......",
    ".GGGGGG",
    ".GYYYYY",
    ".GY.GGG",
    ".GY.G..",
    ".GY.G.G",
    "..GY.G.",
    "..GY.GG",
    "...GY.G",
    "...GY.G",
    "....GY.",
    ".....GY",
    "......G",
]
rows = [r + r[::-1] for r in half]
stamp(rows, 81, 40, {'Y': LGOLD, 'G': GOLD})

# 3. Horde banner emblem: mirror the left half about the emblem's measured axis --
box = img[39:67, 396:414]
dark = box.sum(2) < 120
ys, xs = np.nonzero(dark)
axis2 = int(round(2 * xs.mean()))           # 2x the centre column, so half-pixel axes work
left = dark.copy()
for y in range(dark.shape[0]):
    for x in range(dark.shape[1]):
        mx = axis2 - x
        if 2 * x > axis2 and 0 <= mx < dark.shape[1]:
            left[y, x] = dark[y, mx]
box[left] = BLACK
box[dark & ~left] = RED
img[39:67, 396:414] = box
print('horde emblem axis at column %.1f' % (396 + axis2 / 2))

# 4. troll hunter's bow: the AI's string wandered off in stray diagonal strokes -----
# Clear the bow area to the ground (each row takes that row's commonest ground colour,
# which keeps the ground's horizontal streaks), then draw it properly: the limb curving
# through the grip in his front fist, the string from both tips back to his drawing hand
# at his cheek, and the arrow nocked on the string with its glowing head out front.
BX0, BX1, BY0, BY1 = 357, 378, 140, 178
for y in range(BY0, BY1):
    if 148 <= y <= 161:                             # the arrowhead's rows: plain ground colour
        row = [tuple(int(v) for v in img[y, x]) for x in range(280, 334)]
        img[y, BX0:BX1] = max(set(row), key=row.count)
    else:                                           # elsewhere continue the ground to the left
        img[y, BX0:BX1] = img[y, BX0 - (BX1 - BX0):BX0]
TSKIN, TSHADE = snap((88, 160, 236)), snap((56, 112, 196))
WHITE = snap((245, 245, 235))
GRIP = (361, 156)
HAND = (379, 154)                                   # drawing hand, at his cheek
limb = [(365, 141), (363, 144), (362, 148), (361, 152), (361, 156), (361, 160), (362, 164),
        (363, 168), (365, 171)]
path = []
for a, b in zip(limb, limb[1:]):
    path += line(*a, *b)
string = line(366, 141, *HAND) + line(366, 171, *HAND)
arrow = line(346, 154, HAND[0] - 1, 154)            # from just behind the glowing head
for x, y in arrow:
    put(x, y - 1, BLACK)
    put(x, y + 1, BLACK)
arm = line(GRIP[0] + 1, 157, 381, 157)
for x, y in path + arm:                             # outlines
    for dx in (-1, 0, 1):
        for dy in (-1, 0, 1):
            put(x + dx, y + dy, BLACK)
for x, y in path:
    put(x, y, WOOD)
    put(x - 1, y, DGOLD)
for x, y in arm:
    put(x, y, TSKIN)
    put(x, y + 1, TSHADE)
for x, y in string:
    put(x, y, WHITE)
for x, y in arrow:
    put(x, y, WOOD)
stamp(["KKKK", "KTTK", "KTSK", "KKKK"], GRIP[0] - 2, 154, {'K': BLACK, 'T': TSKIN, 'S': TSHADE})  # bow fist
stamp(["KKK", "KTK", "KKK"], HAND[0], HAND[1] - 1, {'K': BLACK, 'T': TSKIN})                     # drawing fist

Image.fromarray(img).save('fixed.png')
Image.fromarray(img).resize((479 * 4, 205 * 4), Image.NEAREST).save('fixed_x4.png')
