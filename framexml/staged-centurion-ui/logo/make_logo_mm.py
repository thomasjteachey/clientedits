"""Mega Man-style battle logo: four WoW classes as NES sprites, fighting on top of a
chunky title wordmark.

Logical grid 128x64, each logical pixel 2 screen pixels, so the 256x128 area the
glue XML gives the logo is filled exactly; doubled again for the 512x256 texture.
"""
import numpy as np
from PIL import Image

import mm_fighters as F

LW, LH = 128, 64

# 9x10 title glyphs, three-pixel strokes
TITLE = {
    'C': [".#######.", "#########", "###...###", "###......", "###......",
          "###......", "###......", "###...###", "#########", ".#######."],
    'E': ["#########", "#########", "###......", "###......", "#######..",
          "#######..", "###......", "###......", "#########", "#########"],
    'N': ["###...###", "####..###", "#####.###", "#########", "###.#####",
          "###..####", "###...###", "###...###", "###...###", "###...###"],
    'T': ["#########", "#########", "...###...", "...###...", "...###...",
          "...###...", "...###...", "...###...", "...###...", "...###..."],
    'U': ["###...###", "###...###", "###...###", "###...###", "###...###",
          "###...###", "###...###", "###...###", "#########", ".#######."],
    'R': ["########.", "#########", "###...###", "###...###", "#########",
          "########.", "###.###..", "###..###.", "###...###", "###...###"],
    'I': ["###", "###", "###", "###", "###", "###", "###", "###", "###", "###"],
    'O': [".#######.", "#########", "###...###", "###...###", "###...###",
          "###...###", "###...###", "###...###", "#########", ".#######."],
}

# row colours, cream highlight on top down to burnt orange, NES-ish values
BANDS = [(252, 236, 188), (252, 188, 96), (252, 160, 68), (252, 160, 68), (240, 120, 24), (240, 120, 24),
         (228, 92, 16), (228, 92, 16), (200, 64, 0), (200, 64, 0), (184, 52, 0), (152, 36, 0)]
EXTRUDE = (88, 20, 0)


def widen(ch, g):
    """10x12 from the 9x10 drawings: repeat the middle column and two middle rows (T widens at the arms)."""
    rows = g[:5] + [g[4], g[5]] + g[5:] if len(g) == 10 else g
    rows = rows[:4] + [rows[4]] + rows[4:6] + rows[7:]      # keep the count at 12
    if ch == 'T':
        return [r[:2] + r[1] + r[2:7] + r[7] + r[7:] for r in rows]
    if ch == 'I':
        return rows
    return [r[:4] + r[4] + r[4] + r[5:] for r in rows]


def title_word(text='CENTURION', gap=1, extrude=2):
    cols = []
    for i, ch in enumerate(text):
        g = widen(ch, TITLE[ch])
        for x in range(len(g[0])):
            cols.append([g[r][x] == '#' for r in range(len(g))])
        if i < len(text) - 1:
            cols.extend([[False] * len(g)] * gap)
    ink = np.array(cols).T
    h, w = ink.shape
    H, W = h + extrude + 2, w + extrude + 2
    body = np.zeros((H, W), bool)
    body[1:1 + h, 1:1 + w] = ink
    shadow = np.zeros_like(body)
    for k in range(1, extrude + 1):
        shadow[1 + k:1 + k + h, 1 + k:1 + k + w] |= ink
    solid = body | shadow
    outline = solid.copy()
    for dy in (-1, 0, 1):
        for dx in (-1, 0, 1):
            outline |= np.roll(np.roll(solid, dy, 0), dx, 1)
    rgba = np.zeros((H, W, 4), np.uint8)
    rgba[outline] = (0, 0, 0, 255)
    rgba[shadow & ~body] = EXTRUDE + (255,)
    for y in range(h):
        rgba[1 + y][body[1 + y]] = BANDS[y] + (255,)
    return Image.fromarray(rgba, 'RGBA')


def scene():
    out = Image.new('RGBA', (LW, LH), (0, 0, 0, 0))
    word = title_word()
    wx, wy = (LW - word.width) // 2, LH - word.height
    floor = wy + 1

    war, rog = F.warrior().image(), F.rogue().image(True)
    mag, pal = F.mage().image(), F.paladin().image(True)
    fire, clash = F.fireball().image(), F.pellet().image()

    def stand(im, x, lift=0):
        out.alpha_composite(im, (x, floor - im.height - lift))

    stand(war, 2, lift=4)                          # the warrior is mid-leap
    stand(rog, 31)
    out.alpha_composite(clash, (24, floor - 22))
    stand(mag, 60)
    out.alpha_composite(fire, (85, floor - 18))
    stand(pal, LW - pal.width - 1)
    out.alpha_composite(word, (wx, wy))
    # centre the whole scene vertically
    bb = out.getchannel('A').getbbox()
    content = out.crop((0, bb[1], LW, bb[3]))
    centred = Image.new('RGBA', (LW, LH), (0, 0, 0, 0))
    centred.alpha_composite(content, (0, (LH - content.height) // 2))
    return centred


def build():
    s = scene()
    return s.resize((LW * 4, LH * 4), Image.NEAREST)       # 2 screen px, 4 texels per pixel


if __name__ == '__main__':
    im = build()
    im.save('centurion_logo_MM.png')
    t = Image.new('RGBA', im.size, (40, 44, 56, 255))
    t.alpha_composite(im)
    t.resize((1024, 512), Image.NEAREST).save('MM_zoom.png')
