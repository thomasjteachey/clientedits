"""Put the owner's AI battle scene on a true pixel grid.

1. one palette for the whole scene (k-means), so every "same" colour really is the same
2. each 4x4 cell takes the palette colour that most of its pixels vote for - no blending,
   so the drifting, soft-edged AI pixels become crisp single pixels
3. isolated one-off pixels (the checkered noise) take their neighbours' colour
Writes lowres.png (the editable 479x205 art) and palette.json.
"""
import json
import sys

import numpy as np
from PIL import Image

K = int(sys.argv[1]) if len(sys.argv) > 1 else 40
GW, GH = 479, 205

src = np.asarray(Image.open('src.webp').convert('RGB')).astype(np.float32)
H, W, _ = src.shape

# palette from a sample, k-means in RGB
rng = np.random.default_rng(3)
flat = src.reshape(-1, 3)
sample = flat[rng.choice(len(flat), 120000, replace=False)]
cent = sample[rng.choice(len(sample), K, replace=False)].copy()
for _ in range(30):
    d = ((sample[:, None, :] - cent[None]) ** 2).sum(2)
    lab = d.argmin(1)
    for k in range(K):
        sel = lab == k
        if sel.any():
            cent[k] = sample[sel].mean(0)

# small-area colours (skin, the troll's blue, the banners' navy, magic purple) are too few
# pixels for k-means to keep - faces went cream, the troll lavender. Pin them.
FIXED = np.array([(246, 212, 162), (234, 184, 130),      # skin light / shadow
                  (88, 160, 236), (56, 112, 196),        # troll skin + its shadow
                  (25, 55, 128),                         # Alliance navy
                  (212, 88, 244), (118, 35, 180)],       # magenta, warlock purple
                 np.float32)
cent = np.vstack([cent, FIXED])
K = len(cent)

# label every pixel
lab = np.empty(H * W, np.int32)
for i in range(0, H * W, 200000):
    chunk = flat[i:i + 200000]
    lab[i:i + 200000] = ((chunk[:, None, :] - cent[None]) ** 2).sum(2).argmin(1)
lab = lab.reshape(H, W)

# majority vote per 4x4 cell
xs = np.linspace(0, W, GW + 1)
ys = np.linspace(0, H, GH + 1)
grid = np.zeros((GH, GW), np.int32)
for j in range(GH):
    y0, y1 = int(ys[j]), int(ys[j + 1])
    my = 0
    for i in range(GW):
        x0, x1 = int(xs[i]), int(xs[i + 1])
        mx = 0
        cell = lab[y0:y1, x0:x1].ravel()
        grid[j, i] = np.bincount(cell, minlength=K).argmax()


def despeckle(g, passes=2):
    g = g.copy()
    for _ in range(passes):
        s = g.copy()
        for y in range(1, GH - 1):
            for x in range(1, GW - 1):
                me = s[y, x]
                four = [s[y - 1, x], s[y + 1, x], s[y, x - 1], s[y, x + 1]]
                if me in four:
                    continue
                eight = four + [s[y - 1, x - 1], s[y - 1, x + 1], s[y + 1, x - 1], s[y + 1, x + 1]]
                vals, counts = np.unique(eight, return_counts=True)
                if counts.max() >= 4:
                    g[y, x] = vals[counts.argmax()]
    return g


grid = despeckle(grid)
pal = [tuple(int(round(v)) for v in c) for c in cent]
img = Image.fromarray(np.array(pal, np.uint8)[grid], 'RGB')
img.save('lowres.png')
json.dump(pal, open('palette.json', 'w'))
img.resize((GW * 4, GH * 4), Image.NEAREST).save("regrid_x4.png")
print('grid %dx%d, %d colours' % (GW, GH, K))
