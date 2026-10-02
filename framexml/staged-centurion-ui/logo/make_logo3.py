"""Mega Man-inspired Centurion logos, built around the hand-pixelled helmet in sprites.py.

  D  NES title screen: helmet sprite over a banded, extruded pixel wordmark
  E  Mega Man box-art logo: smooth slanted block letters with a hard extrusion,
     the helmet sprite in front of the C
  F  boss-select panel: helmet in a framed portrait box, wordmark beside it
  G  D's wordmark with the helmet standing in for the O

Every design is laid out on the 256x128 screen area the glue XML gives the logo
and doubled to the 512x256 texture, so pixel art stays on whole pixels.
"""
import sys

import numpy as np
from PIL import Image, ImageChops, ImageDraw, ImageFilter, ImageFont

from sprites import FONT, HELMET, sprite

SW, SH = 256, 128
BLACK = (0, 0, 0)
BANDS = [(252, 224, 168), (252, 160, 68), (252, 160, 68), (228, 92, 16), (228, 92, 16), (184, 52, 0), (184, 52, 0)]
EXTRUDE = (104, 24, 0)


def glyph_run(text, gap=1, skip=None):
    """Chunk grid for text: list of rows of band index or -1. `skip` leaves an index blank."""
    cols = []
    for i, ch in enumerate(text):
        g = FONT[ch]
        w = len(g[0])
        for x in range(w):
            cols.append([(r if (i != skip and g[r][x] == '#') else -1) for r in range(7)])
        if i < len(text) - 1:
            cols.extend([[-1] * 7 for _ in range(gap)])
    return np.array(cols).T                         # 7 x width


def word_image(text, scale, extrude=1, gap=1, skip=None):
    """Banded pixel word with an extruded shadow and a one-chunk black outline."""
    bands = glyph_run(text, gap, skip)
    h, w = bands.shape
    pad = 1 + extrude
    ink = np.zeros((h + pad + 1, w + pad + 1), bool)
    ink[1:1 + h, 1:1 + w] = bands >= 0
    body = ink.copy()
    shadow = np.zeros_like(ink)
    for k in range(1, extrude + 1):
        shadow[1 + k:1 + k + h, 1 + k:1 + k + w] |= bands >= 0
    solid = body | shadow
    outline = solid.copy()
    for dy in (-1, 0, 1):
        for dx in (-1, 0, 1):
            outline |= np.roll(np.roll(solid, dy, 0), dx, 1)
    rgba = np.zeros(ink.shape + (4,), np.uint8)
    rgba[outline] = BLACK + (255,)
    rgba[shadow & ~body] = EXTRUDE + (255,)
    for y in range(h):
        for x in range(w):
            if bands[y, x] >= 0:
                rgba[1 + y, 1 + x] = BANDS[bands[y, x]] + (255,)
    im = Image.fromarray(rgba, 'RGBA')
    return im.resize((im.width * scale, im.height * scale), Image.NEAREST)


def big_sprite(scale):
    im = sprite(HELMET)
    return im.resize((im.width * scale, im.height * scale), Image.NEAREST)


def centre(parts, gap=0):
    """Stack images vertically, centred, into the 256x128 screen area."""
    total = sum(p.height for p in parts) + gap * (len(parts) - 1)
    out = Image.new('RGBA', (SW, SH), (0, 0, 0, 0))
    y = (SH - total) // 2
    for p in parts:
        out.alpha_composite(p, ((SW - p.width) // 2, y))
        y += p.height + gap
    return out


def variant_D():
    helm = big_sprite(3)
    word = word_image('CENTURION', 3)
    out = Image.new('RGBA', (SW, SH), (0, 0, 0, 0))
    total = helm.height + word.height - 9            # the word tucks up under the chin
    y = (SH - total) // 2
    out.alpha_composite(helm, ((SW - helm.width) // 2, y))
    out.alpha_composite(word, ((SW - word.width) // 2, y + helm.height - 9))
    return out


def variant_E():
    X = 8                                            # hi-res pixels per screen pixel
    W, H = SW * X, SH * X
    # size the word to the 176px left of the helmet, slant included
    probe = ImageFont.truetype(r'C:\Windows\Fonts\impact.ttf', 100 * X)
    pb = probe.getbbox('CENTURION')
    size = int(100 * X * (172 * X) / ((pb[2] - pb[0]) + 0.22 * (pb[3] - pb[1])))
    f = ImageFont.truetype(r'C:\Windows\Fonts\impact.ttf', size)
    m = Image.new('L', (W, H), 0)
    d = ImageDraw.Draw(m)
    bb = d.textbbox((0, 0), 'CENTURION', font=f)
    tw, th = bb[2] - bb[0], bb[3] - bb[1]
    ox, oy = 70 * X, (SH * X - th) // 2 - 2 * X
    d.text((ox - bb[0], oy - bb[1]), 'CENTURION', font=f, fill=255)
    shear = 0.22                                      # forward slant
    m = m.transform((W, H), Image.AFFINE, (1, shear, -shear * (oy + th), 0, 1, 0), Image.BICUBIC)
    m = m.point(lambda v: 255 if v > 127 else 0)

    def grow(mask, px):
        for _ in range(px // 6):
            mask = mask.filter(ImageFilter.MaxFilter(13))
        if px % 6:
            mask = mask.filter(ImageFilter.MaxFilter((px % 6) * 2 + 1))
        return mask

    ext = Image.new('L', (W, H), 0)
    for k in range(1, 5 * X + 1, 2):                 # block extrusion down and right
        ext = ImageChops.lighter(ext, ImageChops.offset(m, k // 2, k))
    solid = ImageChops.lighter(m, ext)
    canvas = Image.new('RGBA', (W, H), (0, 0, 0, 0))

    def paint(mask, colour):
        layer = Image.new('RGBA', (W, H), colour + (255,))
        layer.putalpha(mask)
        canvas.alpha_composite(layer)

    paint(grow(solid, 2 * X), BLACK)
    paint(ext, EXTRUDE)
    paint(grow(m, 1 * X), BLACK)
    # hard horizontal bands inside the letters, lightest at the top
    rgb = np.zeros((H, W, 3), np.uint8)
    y0, y1 = oy, oy + th
    stops = [0.0, 0.16, 0.42, 0.70, 1.01]
    cols = [(252, 224, 168), (252, 160, 68), (228, 92, 16), (184, 52, 0)]
    for i, c in enumerate(cols):
        a, b = int(y0 + stops[i] * th), int(y0 + stops[i + 1] * th)
        rgb[max(a, 0):min(b, H)] = c
    rgb[:y0] = cols[0]
    rgb[y1:] = cols[-1]
    inner = m.filter(ImageFilter.MinFilter(2 * X + 1))
    paint(m, (252, 236, 200))                        # cream inline
    canvas.alpha_composite(Image.fromarray(np.dstack([rgb, np.asarray(inner)]), 'RGBA'))
    out = canvas.resize((SW * 2, SH * 2), Image.LANCZOS)
    helm = big_sprite(4)                              # texture pixels: 2 screen px per sprite px
    out.alpha_composite(helm, (8, (SH * 2 - helm.height) // 2 - 6))
    return out                                        # already texture sized


def variant_F():
    helm = big_sprite(2)
    word = word_image('CENTURION', 2)
    pad = 4
    box_w, box_h = helm.width + pad * 2 + 8, helm.height + pad * 2 + 8
    panel = Image.new('RGBA', (box_w, box_h), (0, 0, 0, 0))
    d = ImageDraw.Draw(panel)
    # NES frame: black, cream, orange, black, then a dark field
    d.rectangle([0, 0, box_w - 1, box_h - 1], fill=BLACK)
    d.rectangle([2, 2, box_w - 3, box_h - 3], fill=(252, 224, 168))
    d.rectangle([4, 4, box_w - 5, box_h - 5], fill=(228, 92, 16))
    d.rectangle([6, 6, box_w - 7, box_h - 7], fill=BLACK)
    d.rectangle([8, 8, box_w - 9, box_h - 9], fill=(40, 16, 8))
    panel.alpha_composite(helm, (pad + 4, pad + 4))
    gap = 6
    total_w = panel.width + gap + word.width
    out = Image.new('RGBA', (SW, SH), (0, 0, 0, 0))
    x = (SW - total_w) // 2
    out.alpha_composite(panel, (x, (SH - panel.height) // 2))
    out.alpha_composite(word, (x + panel.width + gap, (SH - word.height) // 2))
    return out


def variant_G():
    scale = 3
    o_index = 7                                       # CENTURI(O)N
    word = word_image('CENTURION', scale, skip=o_index)
    helm = big_sprite(1)                              # one screen px per sprite px: O-sized
    # x of the O's first column inside word_image: 1 (outline) + glyphs before it
    col = 1 + sum(len(FONT[c][0]) + 1 for c in 'CENTURI')
    ow = len(FONT['O'][0])
    cx = (col + ow / 2) * scale
    out = Image.new('RGBA', (SW, SH), (0, 0, 0, 0))
    wx, wy = (SW - word.width) // 2, (SH - word.height) // 2 + 8
    out.alpha_composite(word, (wx, wy))
    out.alpha_composite(helm, (int(wx + cx - helm.width / 2), wy + scale * 8 + 3 - helm.height))
    return out


def to_texture(screen):
    if screen.size == (SW * 2, SH * 2):
        return screen
    return screen.resize((SW * 2, SH * 2), Image.NEAREST)


if __name__ == '__main__':
    for v in sys.argv[1:] or 'DEFG':
        to_texture(globals()['variant_' + v]()).save('centurion_logo_%s.png' % v)
