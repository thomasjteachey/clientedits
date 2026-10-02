"""Clean the tauren's horns without redrawing them (src.webp -> hero-night-clean.png).

The original horns have the right silhouette and a sensible light/shadow layout; what is
wrong is inside them - mottled noise and smeared edges. So on the art's 6px grid, every
cell of each horn keeps the horn's own shape and shading but becomes ONE clean colour,
snapped to four tones (highlight, mid, shadow, dark edge). Lone cells whose tone differs
from all their neighbours (the mottling) take the tone most of their neighbours share.
"""
import numpy as np
from PIL import Image

src = np.asarray(Image.open('src.webp').convert('RGB')).astype(np.int32)
out = src.copy()
TONES = np.array([(242, 232, 204), (214, 194, 156), (168, 142, 108), (104, 86, 66)])


def clean(cx0, cy0, pw, ph, cols, rows):
    """cx0/cy0: the horn's own grid origin, pw/ph its measured pixel pitch (the AI's grid
    drifts, so each horn is measured separately)."""
    def box(cx0, cy0, i, j):
        x, y = cx0 + i * pw, cy0 + j * ph
        return int(round(y)), int(round(y + ph)), int(round(x)), int(round(x + pw))

    med = np.zeros((rows, cols, 3))
    for j in range(rows):
        for i in range(cols):
            ya, yb, xa, xb = box(cx0, cy0, i, j)
            med[j, i] = np.median(src[ya + 1:yb - 1, xa + 1:xb - 1].reshape(-1, 3), 0)
    r, g, b = med[..., 0], med[..., 1], med[..., 2]
    lum = (r + g + b) / 3
    horn = (lum > 118) & (r >= b) & (r - g < 45)          # the horn's lit cells
    # the shaded underside is darker: grow into connected cells that are still horn-toned
    under = (lum > 78) & (r >= b) & (r - g < 50)
    for _ in range(2):
        pad = np.pad(horn, 1)
        near = pad[:-2, 1:-1] | pad[2:, 1:-1] | pad[1:-1, :-2] | pad[1:-1, 2:]
        horn = horn | (near & under)
    # close one-cell notches in the outline (a gap with horn left, right and above it)
    for _ in range(2):
        pad = np.pad(horn, 1)
        horn = horn | (pad[1:-1, :-2] & pad[1:-1, 2:] & pad[:-2, 1:-1])
    tone = np.full((rows, cols), -1)
    for j in range(rows):
        for i in range(cols):
            if horn[j, i]:
                tone[j, i] = min(2, int(((TONES - med[j, i]) ** 2).sum(1).argmin()))   # no separate dark-edge tone
    # the underside is ONE shadow band: every horn cell with no horn below it
    pad = np.pad(horn, 1)
    tone[horn & ~pad[2:, 1:-1]] = 2
    for _ in range(2):                                     # despeckle the tones
        t2 = tone.copy()
        for j in range(1, rows - 1):
            for i in range(1, cols - 1):
                if tone[j, i] < 0:
                    continue
                nb = [tone[j + dj, i + di] for di, dj in ((1, 0), (-1, 0), (0, 1), (0, -1))]
                nb = [v for v in nb if v >= 0]
                if nb and tone[j, i] not in nb:
                    vals, cnt = np.unique(nb, return_counts=True)
                    t2[j, i] = vals[cnt.argmax()]
        tone = t2
    for j in range(rows):
        for i in range(cols):
            if tone[j, i] >= 0:
                ya, yb, xa, xb = box(cx0, cy0, i, j)
                out[ya:yb, xa:xb] = TONES[tone[j, i]]
    return tone


t1 = clean(412.5 - 6.1, 276.25 - 5.8, 6.1, 5.8, 18, 18)       # grids measured per horn
t2 = clean(566.0, 281.0 - 5.85, 5.8, 5.85, 15, 17)
Image.fromarray(out.astype(np.uint8)).save('hero-night-clean.png')
for t in (t1, t2):
    for row in t:
        print(''.join('.' if v < 0 else 'HMSD'[v] for v in row))
    print()
