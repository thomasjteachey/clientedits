"""Colour-grade the fixed scene so it sits behind the site instead of fighting it.

The source is wall-to-wall neon orange; the site around it is muted brown-gold. Grading
is done per palette colour (every pixel of a colour moves together), so the result is
still clean pixel art: no new gradients, no new colours per area.
  - the orange family (sky, ground, cliffs): less saturated and darker - a dusk amber
  - everything else (the fighters, banners, magic): only slightly muted, so they still read
"""
import colorsys
import sys

import numpy as np
from PIL import Image

SAT_BG, VAL_BG = float(sys.argv[1]), float(sys.argv[2])     # e.g. 0.70 0.72
SAT_FG, VAL_FG = 0.92, 0.94

img = np.asarray(Image.open('fixed.png').convert('RGB'))
out = img.copy()
cols = np.unique(img.reshape(-1, 3), axis=0)
for c in cols:
    h, s, v = colorsys.rgb_to_hsv(*(c / 255.0))
    deg = h * 360
    orange = 8 <= deg <= 48 and s > 0.35
    if orange:
        s, v = s * SAT_BG, v * VAL_BG
        h = (deg - 3) / 360.0                           # a touch redder: ember, not neon
    else:
        s, v = s * SAT_FG, v * VAL_FG
    n = np.array(colorsys.hsv_to_rgb(h % 1.0, min(s, 1), min(v, 1))) * 255
    out[(img == c).all(-1)] = np.round(n).astype(np.uint8)
im = Image.fromarray(out).resize((479 * 4, 205 * 4), Image.NEAREST)
canvas = Image.new('RGB', (1920, 820))
canvas.paste(im, (2, 0))
canvas.paste(im.crop((0, 0, 2, 820)), (0, 0))
canvas.paste(im.crop((1914, 0, 1916, 820)), (1918, 0))
canvas.save('header_new.png', optimize=True)
print('graded %d colours; bottom row median %s' % (len(cols), np.median(out[-3:].reshape(-1, 3), 0)))
