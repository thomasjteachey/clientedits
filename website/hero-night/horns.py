"""Redraw the tauren's horns as clean pixel art (hero-night.jpg -> hero-night-fixed.png).

The generated horns are mottled grey/tan noise with smeared, lumpy edges. Both horns are
drawn by hand here on the art's own 6px grid (phase measured from the image), following
the original's silhouette: thick at the head, tapering to a sharp tip, with a lit upper
edge, a mid tone, a shadowed underside, a dark keyline and a couple of growth rings.
Horn-coloured cells the new silhouette doesn't cover (the old smeared edges) are filled
with the fur or sky around them. Nothing outside the two horn boxes is touched.
"""
import numpy as np
from PIL import Image

P, PX, PY = 6.0, 305.5, 200.75            # grid pitch and phase (measured around the head)
src = np.asarray(Image.open('src.webp').convert('RGB')).astype(np.int32)
out = src.copy()
PAL = {'H': (242, 232, 204), 'M': (214, 194, 156), 'S': (160, 134, 100), 'K': (26, 17, 12)}

LEFT = [                                    # from cell x=407.5, y=272.75 (rows 2..15)
    ".......K.........",
    "......KHK........",
    ".....KHMK........",
    "....KHMSK........",
    "....KHMSK........",
    "...KHMMSK........",
    "..KHMMSK.........",
    "..KHMMSK.........",
    ".KHMMMSKKKKKKKK..",
    ".KHMMMMHHSHHHSHK.",
    ".KSMMMMMMSMMMSMMK",
    "..KSSMMMMSMMMSSSK",
    "...KKSSSSSSSSSKK.",
    ".....KKKKKKKKK...",
]
RIGHT = [                                   # from cell x=569.5, y=278.75 (rows 5..15)
    "........K....",
    ".......KHK...",
    ".......KHMK..",
    "......KHMSK..",
    "......KHMSK..",
    ".....KHMMSK..",
    "KHHHSHMMMSK..",
    "KMMMSMMMMSK..",
    "KSSMSMMMSSK..",
    ".KKSSSSSKK...",
    "...KKKKK.....",
]


def cell_box(cx0, cy0, i, j):
    x, y = cx0 + i * P, cy0 + j * P
    return int(round(y)), int(round(y + P)), int(round(x)), int(round(x + P))


def median(cx0, cy0, i, j):
    ya, yb, xa, xb = cell_box(cx0, cy0, i, j)
    return np.median(src[ya + 1:yb - 1, xa + 1:xb - 1].reshape(-1, 3), 0)


def hornish(c):
    r, g, b = c
    return (r + g + b) / 3 > 92 and r >= b and r - g < 45


def draw(grid, cx0, cy0, row0, cols, rows, extra_clear=()):
    shape = set()
    for j, line in enumerate(grid):
        for i, ch in enumerate(line):
            if ch != '.':
                shape.add((i, row0 + j))
    # 1. leftover smeared horn cells outside the new silhouette -> surrounding fur/sky
    for j in range(rows):
        for i in range(cols):
            if (i, j) in shape or not hornish(median(cx0, cy0, i, j)):
                continue
            ring = [median(cx0, cy0, i + di, j + dj) for di in (-1, 0, 1) for dj in (-1, 0, 1)
                    if (di or dj) and (i + di, j + dj) not in shape]
            ring = [c for c in ring if not hornish(c)]
            if ring:
                ya, yb, xa, xb = cell_box(cx0, cy0, i, j)
                out[ya:yb, xa:xb] = np.median(ring, 0)
    # 1b. named cells the old tip smeared into (part fur, part horn - they pass no test)
    for i, j in extra_clear:
        ring = [median(cx0, cy0, i + di, j + dj) for di in (-1, 0, 1) for dj in (-1, 0, 1)
                if (di or dj) and (i + di, j + dj) not in shape]
        ring = [c for c in ring if c[2] > c[0]]           # sky only
        if ring:
            ya, yb, xa, xb = cell_box(cx0, cy0, i, j)
            out[ya:yb, xa:xb] = np.median(ring, 0)
    # 2. the horn itself
    for j, line in enumerate(grid):
        for i, ch in enumerate(line):
            if ch != '.':
                ya, yb, xa, xb = cell_box(cx0, cy0, i, row0 + j)
                out[ya:yb, xa:xb] = PAL[ch]


draw(LEFT, 407.5, 272.75, 2, 17, 17)
draw(RIGHT, 569.5, 278.75, 5, 13, 16, extra_clear=[(6, 4), (7, 4), (6, 5), (7, 3)])
Image.fromarray(out.astype(np.uint8)).save('hero-night-fixed.png')
print('horns redrawn')
