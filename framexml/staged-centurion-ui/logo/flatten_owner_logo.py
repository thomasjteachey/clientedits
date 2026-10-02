"""Rebuild the owner's logo as real pixel art.

The source (owner_logo_source.webp) imitates pixel art but its "pixels" drift off any
grid, carry gradients and compression noise. This finds the grid it was imitating,
takes one colour per cell (median of the cell's middle), snaps every cell to a small
palette, and scales the result up by a whole number with hard edges.

Black that the background flood-fill reaches is transparent; black the frame encloses
stays opaque, as in the source.
"""
import sys

import numpy as np
from PIL import Image, ImageDraw

SRC = 'owner_logo_source.webp'
COLOURS = int(sys.argv[1]) if len(sys.argv) > 1 else 12

a = np.asarray(Image.open(SRC).convert('RGB')).astype(np.float32)
H, W, _ = a.shape
lum = a.sum(2)


def best_grid(edge, lo=12.4, hi=14.2):
    """(pitch, phase) whose grid lines sit on the most edge energy."""
    best = (0, 0, 0)
    n = len(edge)
    for p in np.arange(lo, hi, 0.02):
        for ph in np.arange(0, p, 0.25):
            idx = np.round(np.arange(ph, n - 1, p)).astype(int)
            score = edge[idx].mean()
            if score > best[0]:
                best = (score, p, ph)
    return best[1], best[2]


ex = np.abs(np.diff(lum, axis=1)).sum(0)
ey = np.abs(np.diff(lum, axis=0)).sum(1)
px, phx = best_grid(ex)
py, phy = best_grid(ey)
print('grid x pitch %.2f phase %.2f | y pitch %.2f phase %.2f' % (px, phx, py, phy))

xs = np.arange(phx, W - px, px)
ys = np.arange(phy, H - py, py)
cells = np.zeros((len(ys), len(xs), 3), np.float32)
for j, y in enumerate(ys):
    for i, x in enumerate(xs):
        y0, y1 = int(y + py * 0.25), int(y + py * 0.75) + 1
        x0, x1 = int(x + px * 0.25), int(x + px * 0.75) + 1
        cells[j, i] = np.median(a[y0:y1, x0:x1].reshape(-1, 3), axis=0)

# palette: k-means over the non-black cells, black kept as its own entry
flat = cells.reshape(-1, 3)
ink = flat[flat.max(1) > 45]
rng = np.random.default_rng(1)
cent = ink[rng.choice(len(ink), COLOURS, replace=False)]
for _ in range(40):
    d = ((ink[:, None, :] - cent[None]) ** 2).sum(2)
    lab = d.argmin(1)
    for k in range(COLOURS):
        if (lab == k).any():
            cent[k] = ink[lab == k].mean(0)
palette = np.vstack([[0, 0, 0], cent])
d = ((flat[:, None, :] - palette[None]) ** 2).sum(2)
d[:, 0] = np.where(flat.max(1) <= 45, -1, d[:, 0] + 1e9)   # dark cells -> black, others never
q = palette[d.argmin(1)].reshape(cells.shape).astype(np.uint8)

def despeckle(q, passes=2):
    """Replace a pixel that matches none of its 4 neighbours with the colour most of its
    8 neighbours share - the one-off noise pixels the generator scattered everywhere."""
    q = q.copy()
    h, w, _ = q.shape
    key = lambda c: tuple(int(v) for v in c)
    for _ in range(passes):
        src = q.copy()
        for y in range(1, h - 1):
            for x in range(1, w - 1):
                me = key(src[y, x])
                four = [key(src[y + dy, x + dx]) for dx, dy in ((1, 0), (-1, 0), (0, 1), (0, -1))]
                if me in four or me == (0, 0, 0):
                    continue
                eight = four + [key(src[y + dy, x + dx]) for dx, dy in ((1, 1), (1, -1), (-1, 1), (-1, -1))]
                best = max(set(eight), key=eight.count)
                if best != (0, 0, 0) and eight.count(best) >= 4:
                    q[y, x] = best
    return q


q = despeckle(q)

# transparency: black goes transparent unless the art encloses it outright (the O, the
# visor) or it is the banner's field. The frame has one-cell gaps, so a flood fill alone
# would let the banner's black interior leak out.
dark = (q.max(2) == 0)


def fill_outside(walls):
    m = Image.fromarray(np.where(walls, 255, 0).astype(np.uint8)).copy()   # floodfill is a no-op on a numpy-backed image
    ImageDraw.floodfill(m, (0, 0), 128)
    return np.asarray(m) == 128


truly_enclosed = dark & ~fill_outside(~dark)                 # the O's hole, the visor


def longest_run(row):
    best = cur = 0
    for v in row:
        cur = cur + 1 if v else 0
        best = max(best, cur)
    return best


# the banner: between its long top and bottom bars, inside the frame on each row
bars = [y for y in range(dark.shape[0]) if longest_run(~dark[y]) >= 40]
keep = truly_enclosed.copy()
if len(bars) >= 2:
    for y in range(bars[0] + 1, bars[-1]):
        # the frame has gaps on some rows, so take its extent from this row and its neighbours
        xs_ = np.concatenate([np.nonzero(~dark[yy])[0] for yy in (y - 1, y, y + 1)])
        if len(xs_):
            x0, x1 = xs_.min(), xs_.max()
            keep[y, x0:x1 + 1] |= dark[y, x0:x1 + 1]
outside = dark & ~keep
alpha = np.where(outside, 0, 255).astype(np.uint8)
low = Image.fromarray(np.dstack([q, alpha]), 'RGBA')
low = low.crop(low.getchannel('A').getbbox())
low.save('logo_flat_lowres.png')

scale = min(512 // low.width, 256 // low.height)
big = low.resize((low.width * scale, low.height * scale), Image.NEAREST)
out = Image.new('RGBA', (512, 256), (0, 0, 0, 0))
out.alpha_composite(big, ((512 - big.width) // 2, (256 - big.height) // 2))
out.save('centurion_logo_final.png')
print('low-res %dx%d, x%d -> %dx%d, %d colours' % (low.width, low.height, scale, big.width, big.height, COLOURS + 1))
