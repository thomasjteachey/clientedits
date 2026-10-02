"""Recolour the Heaven theme from blue to orange, one colour function for CSS and images.

Every colour whose hue is blue/cyan/indigo is moved to the orange family, keeping its
brightness and (mostly) its saturation, so the theme's light/dark structure survives:
  - vivid blues (buttons, links, accents)    -> orange 26-34 deg
  - light cyan highlights                    -> amber/gold ~40 deg
  - dark navy panels/backgrounds (V < 0.40)  -> dark warm brown-charcoal (saturation cut)
Reds, golds, greys and everything else are left alone.

usage: orangeify.py <src_root> <out_root>
  CSS: every *.css under the theme + CMS css + module css; hex, rgb()/rgba(), and
       url-encoded %23rrggbb inside SVG data URLs.
  Images: theme graphics/, banners/, patterns/ (not screenshots/thumbnails).
"""
import colorsys
import os
import re
import sys

import numpy as np
from PIL import Image

BLUE_LO, BLUE_HI = 170 / 360, 290 / 360


def smoothstep(a, b, x):
    t = np.clip((x - a) / (b - a), 0, 1)
    return t * t * (3 - 2 * t)


def remap_hsv(h, s, v, painting=False):
    """h, s, v in 0..1 (scalars or numpy arrays). Returns the same.

    Saturation is scaled by a SMOOTH ramp on brightness - a hard threshold splits every
    gradient that crosses it into a visible seam. Paintings (backgrounds, banners) get a
    lower ramp so the navy art becomes smoky warm brown, not mustard."""
    blue = (h >= BLUE_LO) & (h <= BLUE_HI) & (s > 0.06)
    # cyan-ish (170-200) goes a little more golden, deep blue/indigo a little redder
    t = np.clip((h - BLUE_LO) / (BLUE_HI - BLUE_LO), 0, 1)
    nh = ((34 - 12 * t) if painting else (40 - 16 * t)) / 360
    lo, hi = (0.40, 0.80) if painting else (0.60, 1.05)
    ns = np.minimum(1.0, s * (lo + (hi - lo) * smoothstep(0.25, 0.65, v)))
    if not painting:
        # greyish blues are text and chrome: make them warm GREY, not tan/orange
        ns = ns * (0.55 + 0.45 * smoothstep(0.25, 0.45, s))
    return np.where(blue, nh, h), np.where(blue, ns, s), v


def luma(r, g, b):
    return 0.2126 * r + 0.7152 * g + 0.0722 * b


def keep_luma(old, new, v, painting):
    """Orange reads far brighter than blue at the same HSV value, so a straight hue swap
    lightens everything. Paintings and dark panels take the ORIGINAL perceived brightness;
    bright accents (buttons, links) keep their value so they still pop."""
    w = np.ones_like(v) if painting else 1 - smoothstep(0.35, 0.60, v)
    lo, ln = luma(*old), luma(*new)
    k = np.where(ln > 1e-6, lo / np.maximum(ln, 1e-6), 1.0)
    k = (1 - w) + w * k
    return [np.clip(c * k, 0, 1) for c in new]


def remap_rgb(r, g, b):
    h, s, v = colorsys.rgb_to_hsv(r / 255, g / 255, b / 255)
    nh, ns, nv = remap_hsv(np.float64(h), np.float64(s), np.float64(v))
    rr, gg, bb = colorsys.hsv_to_rgb(float(nh), float(ns), float(nv))
    if (float(nh), float(ns)) != (h, s):
        rr, gg, bb = (float(c) for c in keep_luma((r / 255, g / 255, b / 255), (rr, gg, bb), np.float64(v), False))
    return round(rr * 255), round(gg * 255), round(bb * 255)


HEX = re.compile(r'(#|%23)([0-9a-fA-F]{6}|[0-9a-fA-F]{3})\b')
RGB = re.compile(r'rgba?\(\s*(\d{1,3})\s*,\s*(\d{1,3})\s*,\s*(\d{1,3})')
RGB_SP = re.compile(r'rgba?\(\s*(\d{1,3})\s+(\d{1,3})\s+(\d{1,3})(?=\s*[/)])')
IMG_URL = re.compile(r"""(url\(\s*['"]?)([^'")?]*images/(?:graphics|banners|patterns)/[^'")?]+)(?=['"]?\s*\))""")
BUST = 'v=1.1'   # the theme version these recoloured images ship with
BASE3 = re.compile(r'(--baseColor-r:\s*)(\d+)(;\s*--baseColor-g:\s*)(\d+)(;\s*--baseColor-b:\s*)(\d+)')


def fix_hex(m):
    prefix, hx = m.group(1), m.group(2)
    if len(hx) == 3:
        hx = ''.join(c * 2 for c in hx)
    r, g, b = int(hx[0:2], 16), int(hx[2:4], 16), int(hx[4:6], 16)
    nr, ng, nb = remap_rgb(r, g, b)
    if (nr, ng, nb) == (r, g, b):
        return m.group(0)
    return '%s%02x%02x%02x' % (prefix, nr, ng, nb)


def fix_rgb_exact(m):
    r, g, b = (int(m.group(i)) for i in (1, 2, 3))
    nr, ng, nb = remap_rgb(r, g, b)
    head = m.group(0)[:m.group(0).index('(') + 1]
    return '%s%d, %d, %d' % (head, nr, ng, nb)


def fix_rgb_space(m):
    nr, ng, nb = remap_rgb(*(int(m.group(i)) for i in (1, 2, 3)))
    head = m.group(0)[:m.group(0).index('(') + 1]
    return '%s%d %d %d' % (head, nr, ng, nb)


def fix_base3(m):
    nr, ng, nb = remap_rgb(int(m.group(2)), int(m.group(4)), int(m.group(6)))
    return '%s%d%s%d%s%d' % (m.group(1), nr, m.group(3), ng, m.group(5), nb)


def css(text):
    text = HEX.sub(fix_hex, text)
    text = RGB.sub(fix_rgb_exact, text)
    text = RGB_SP.sub(fix_rgb_space, text)
    text = BASE3.sub(fix_base3, text)
    # same image URLs, new pixels: version them so a week-long browser cache can't serve blue
    text = IMG_URL.sub(lambda m: m.group(1) + m.group(2) + '?' + BUST, text)
    return text


def image(src, dst, painting=False):
    im = Image.open(src)
    rgba = im.convert('RGBA')
    a = np.asarray(rgba).astype(np.float32) / 255.0
    r, g, b, al = a[..., 0], a[..., 1], a[..., 2], a[..., 3]
    mx, mn = a[..., :3].max(-1), a[..., :3].min(-1)
    v = mx
    d = mx - mn
    s = np.where(mx > 0, d / np.maximum(mx, 1e-6), 0)
    h = np.zeros_like(mx)
    dd = np.maximum(d, 1e-6)
    h = np.where(mx == r, ((g - b) / dd) % 6, h)
    h = np.where(mx == g, (b - r) / dd + 2, h)
    h = np.where(mx == b, (r - g) / dd + 4, h)
    h = np.where(d == 0, 0, h) / 6.0
    nh, ns, nv = remap_hsv(h, s, v, painting)
    # hsv -> rgb, vectorised
    i = np.floor(nh * 6) % 6
    f = nh * 6 - np.floor(nh * 6)
    p, q, t = nv * (1 - ns), nv * (1 - f * ns), nv * (1 - (1 - f) * ns)
    out = np.zeros_like(a[..., :3])
    for k, (x, y, z) in enumerate([(nv, t, p), (q, nv, p), (p, nv, t), (p, q, nv), (t, p, nv), (nv, p, q)]):
        sel = i == k
        out[..., 0] = np.where(sel, x, out[..., 0])
        out[..., 1] = np.where(sel, y, out[..., 1])
        out[..., 2] = np.where(sel, z, out[..., 2])
    changed = (nh != h) | (ns != s)
    fixed = keep_luma((r, g, b), (out[..., 0], out[..., 1], out[..., 2]), v, painting)
    for c in range(3):
        out[..., c] = np.where(changed, fixed[c], out[..., c])
    res = Image.fromarray((np.dstack([out, al]) * 255).round().astype(np.uint8), 'RGBA')
    os.makedirs(os.path.dirname(dst), exist_ok=True)
    if src.lower().endswith(('.jpg', '.jpeg')):
        res.convert('RGB').save(dst, quality=92)
    else:
        res.save(dst)


if __name__ == '__main__':
    SRC, OUT = sys.argv[1], sys.argv[2]
    css_roots = ['application/themes/heaven', 'application/css', 'application/modules']
    changed = 0
    for root in css_roots:
        for dp, _, fs in os.walk(os.path.join(SRC, root)):
            for f in fs:
                if not f.endswith('.css'):
                    continue
                p = os.path.join(dp, f)
                rel = os.path.relpath(p, SRC)
                t = open(p, encoding='utf-8', errors='surrogateescape').read()
                nt = css(t)
                if nt != t:
                    o = os.path.join(OUT, rel)
                    os.makedirs(os.path.dirname(o), exist_ok=True)
                    open(o, 'w', encoding='utf-8', errors='surrogateescape', newline='').write(nt)
                    changed += 1
                    print('css ', rel)
    img_root = os.path.join(SRC, 'application/themes/heaven/assets/images')
    for sub in ('graphics', 'banners', 'patterns'):
        for f in sorted(os.listdir(os.path.join(img_root, sub))):
            if f == 'logo.png':
                continue
            p = os.path.join(img_root, sub, f)
            rel = os.path.relpath(p, SRC)
            image(p, os.path.join(OUT, rel), painting=Image.open(p).mode == 'RGB')   # opaque = painted art
            print('img ', rel)
    print(changed, 'css files changed')
