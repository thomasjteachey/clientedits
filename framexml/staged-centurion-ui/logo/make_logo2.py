"""Centurion logo, second pass: flat colour, hard edges, no bloom or chrome.

Variants (argv[1]):
  A  wordmark only, LifeCraft, two-tone orange with a hard black keyline
  B  Roman tabula ansata (the dovetail-handled plaque) with engraved serif caps
  C  A's wordmark on B's plaque
Output is 512x256 RGBA, the canvas of Glues-WoW-WotLKLogo.blp.
"""
import sys

import numpy as np
from PIL import Image, ImageChops, ImageDraw, ImageFilter, ImageFont

S = 4
W, H = 512 * S, 256 * S
CX, CY = W // 2, H // 2
LIFECRAFT = r'C:\Users\broki\AppData\Local\Microsoft\Windows\Fonts\LifeCraft_Font.ttf'
ENGR = r'C:\Windows\Fonts\ENGR.TTF'

ORANGE_HI = (255, 170, 60)
ORANGE_LO = (228, 96, 16)
INK = (26, 12, 6)
CREAM = (255, 226, 168)
BRONZE_DARK = (48, 28, 18)
BRONZE_RIM = (196, 110, 40)


def blank():
    return Image.new('L', (W, H), 0)


def grow(mask, px):
    for _ in range(px // 6):
        mask = mask.filter(ImageFilter.MaxFilter(13))
    r = px % 6
    if r:
        mask = mask.filter(ImageFilter.MaxFilter(r * 2 + 1))
    return mask


def shift(mask, dx, dy):
    return ImageChops.offset(mask, dx, dy)


class Canvas:
    def __init__(self):
        self.im = Image.new('RGBA', (W, H), (0, 0, 0, 0))

    def paint(self, mask, colour):
        layer = Image.new('RGBA', (W, H), colour + (255,))
        layer.putalpha(mask)
        self.im.alpha_composite(layer)

    def paint_split(self, mask, top, bottom, split_y):
        """Two flat tones with a hard horizontal break, like painted sign lettering."""
        rgb = np.zeros((H, W, 3), np.uint8)
        rgb[:split_y] = top
        rgb[split_y:] = bottom
        layer = Image.fromarray(np.dstack([rgb, np.asarray(mask)]), 'RGBA')
        self.im.alpha_composite(layer)


def wordmark_lifecraft(size_big=104, size_small=84, cy=CY):
    big, small = ImageFont.truetype(LIFECRAFT, size_big * S), ImageFont.truetype(LIFECRAFT, size_small * S)
    word = 'CENTURION'
    glyphs = [(c, big if i in (0, len(word) - 1) else small) for i, c in enumerate(word)]
    track = 1 * S
    widths = [f.getbbox(c)[2] - f.getbbox(c)[0] for c, f in glyphs]
    x = CX - (sum(widths) + track * (len(word) - 1)) / 2
    m = blank()
    d = ImageDraw.Draw(m)
    for (c, f), w in zip(glyphs, widths):
        bb = f.getbbox(c)
        d.text((x - bb[0], cy - (bb[1] + bb[3]) / 2), c, font=f, fill=255)
        x += w + track
    return m


def wordmark_engraved(size=62, cy=CY, spacing=6):
    f = ImageFont.truetype(ENGR, size * S)
    word = 'CENTURION'
    widths = [f.getbbox(c)[2] - f.getbbox(c)[0] for c in word]
    x = CX - (sum(widths) + spacing * S * (len(word) - 1)) / 2
    m = blank()
    d = ImageDraw.Draw(m)
    cap = f.getbbox('N')
    for c, w in zip(word, widths):
        bb = f.getbbox(c)
        d.text((x - bb[0], cy - (cap[1] + cap[3]) / 2), c, font=f, fill=255)
        x += w + spacing * S
    return m


def tabula(x0, x1, y0, y1, ear):
    """Rectangle with a dovetail handle (ansa) on each end."""
    m = blank()
    d = ImageDraw.Draw(m)
    ym = (y0 + y1) / 2
    d.rectangle([x0, y0, x1, y1], fill=255)
    h = (y1 - y0) * 0.36
    d.polygon([(x0 + 2, ym), (x0 - ear, ym - h), (x0 - ear, ym + h)], fill=255)
    d.polygon([(x1 - 2, ym), (x1 + ear, ym - h), (x1 + ear, ym + h)], fill=255)
    return m


def rivets(x0, x1, y0, y1, ear, r):
    m = blank()
    d = ImageDraw.Draw(m)
    ym = (y0 + y1) / 2
    for x in (x0 - ear * 0.62, x1 + ear * 0.62):
        d.ellipse([x - r, ym - r, x + r, ym + r], fill=255)
    return m


def plate(cv, x0, x1, y0, y1, ear):
    outer = tabula(x0, x1, y0, y1, ear)
    cv.paint(shift(grow(outer, 3 * S), 2 * S, 3 * S), INK)          # hard offset shadow
    cv.paint(grow(outer, 3 * S), INK)                                # keyline
    cv.paint(outer, BRONZE_RIM)
    inner = tabula(x0 + 6 * S, x1 - 6 * S, y0 + 6 * S, y1 - 6 * S, ear - 10 * S)
    cv.paint(inner, BRONZE_DARK)
    ring = ImageChops.subtract(inner, tabula(x0 + 9 * S, x1 - 9 * S, y0 + 9 * S, y1 - 9 * S, ear - 15 * S))
    cv.paint(ring, (120, 64, 26))
    cv.paint(rivets(x0, x1, y0, y1, ear, 5 * S), INK)
    cv.paint(rivets(x0, x1, y0, y1, ear, 3 * S), BRONZE_RIM)


def painted_word(cv, word):
    bb = word.getbbox()
    split = (bb[1] + bb[3]) // 2 - 2 * S
    cv.paint(shift(grow(word, 4 * S), 2 * S, 3 * S), INK)
    cv.paint(grow(word, 4 * S), INK)
    cv.paint(grow(word, 1 * S), CREAM)
    cv.paint_split(word, ORANGE_HI, ORANGE_LO, split)


def variant(v):
    cv = Canvas()
    if v == 'A':
        painted_word(cv, wordmark_lifecraft())
    elif v == 'B':
        word = wordmark_engraved(size=34, spacing=4)
        x0, y0, x1, y1 = word.getbbox()
        px, py = 22 * S, 16 * S
        plate(cv, x0 - px, x1 + px, y0 - py, y1 + py, 28 * S)
        cv.paint(shift(word, -1 * S, -1 * S), INK)             # cut edge in shadow
        cv.paint(shift(word, 1 * S, 1 * S), (150, 84, 34))     # cut edge catching light
        cv.paint(word, ORANGE_HI)
    elif v == 'C':
        word = wordmark_lifecraft()
        x0, y0, x1, y1 = word.getbbox()
        # a band narrower than the letters, so the caps overhang it top and bottom
        band = (y1 - y0) * 0.22
        plate(cv, x0 - 10 * S, x1 + 10 * S, y0 + band, y1 - band, 30 * S)
        painted_word(cv, word)
    im = cv.im
    bb = im.getchannel('A').getbbox()
    im = im.crop(bb)
    k = min((W - 4 * S) / im.width, (H - 4 * S) / im.height)
    im = im.resize((round(im.width * k), round(im.height * k)), Image.LANCZOS)
    out = Image.new('RGBA', (W, H), (0, 0, 0, 0))
    out.paste(im, ((W - im.width) // 2, (H - im.height) // 2))
    return out.resize((512, 256), Image.LANCZOS)


if __name__ == '__main__':
    for v in sys.argv[1:] or ['A', 'B', 'C']:
        variant(v).save('centurion_logo_%s.png' % v)
