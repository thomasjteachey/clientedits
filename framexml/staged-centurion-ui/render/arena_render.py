#!/usr/bin/env python3
"""Render ArenaFrame 1:1 from the client's own art.

Geometry: stock ArenaFrame.xml for the art and headers, the LIVE ArenaFrame.lua
for the 4+4 row layout it builds at runtime (ArenaFrame_Position).
"""
import os
from PIL import Image, ImageDraw, ImageFont
import frame_render as F

HERE = os.path.dirname(os.path.abspath(__file__))
W, H, S = F.W, F.H, F.S

RATED_X = 23
RATED_Y_TOP = 79
ROW = 16


def render(check=None):
    """check: (label, y_center, checked) drawn right-aligned at x=344, or None."""
    fr = F.Frame()

    fr.blit(F.art('UI-Battlefield-Icon.png'), 7, 6, 60, 60)
    fr.blit(F.art('UI-Battlefield-TopLeft.png'), 0, 0, 256, 256)
    fr.blit(F.art('UI-Battlefield-TopRight.png'), W - 128, 0, 128, 256)
    fr.blit(F.art('UI-Battlefield-BotLeft.png'), 0, H - 256, 256, 256)
    fr.blit(F.art('UI-Battlefield-BotRight.png'), W - 128, H - 256, 128, 256)

    # ARTWORK divider: 513x64 with its TOPLEFT at the frame's LEFT + (14,-95)
    fr.blit(F.art('UI-Battlefield-Bar.png'), 14, 256 - 95, 513, 64)

    fr.text('Arena Battles', 192, 17 + 7, 12, F.GOLD, anchor='mm')
    fr.text('Rated Battle:', 75, 55 + 7, 12, F.HILITE, anchor='lm')
    fr.text('Practice Battle:', 28, 170 + 7, 12, F.HILITE, anchor='lm')

    rated = ['2v2 Battle', '3v3 Battle', '4v4 Battle', '5v5 Battle']
    practice = ['2v2 Skirmish', '3v3 Skirmish', '4v4 Skirmish', '5v5 Skirmish']
    # selection highlight on the first rated row, as in the screenshot
    hl = F.art('UI-Listbox-Highlight2.png')
    fr.blit(hl, RATED_X, RATED_Y_TOP, 315, 16)
    for i, n in enumerate(rated):
        fr.text(n, RATED_X + 8, RATED_Y_TOP + i * ROW + 8, 12, F.GOLD, anchor='lm')
    p_top = 194
    for i, n in enumerate(practice):
        fr.text(n, RATED_X + 8, p_top + i * ROW + 8, 12, F.GOLD, anchor='lm')

    # flavour text: LEFT 23, TOP = ArenaZone8 bottom - 40, width 294
    dy = p_top + 4 * ROW + 40
    for i, line in enumerate(['Let the games begin!  The door is open to all',
                              'who wish to find glory in the arena, and the',
                              'Steamwheedle Cartel is prepared to accept yo...']):
        fr.text(line, RATED_X, dy + 8 + i * 15, 12, (40, 26, 12), anchor='lm')

    btn = F.art('UI-Panel-Button-Up.png')
    for x, w, label in ((263.5, 83, 'Cancel'), (152.5, 109, 'Join Battle')):
        fr.blit(btn, x, 423 - 11, w, 22, crop=(0.0, 0.0, 0.625, 0.6875))
        fr.text(label, x + w / 2, 423, 12, F.GOLD, anchor='mm')

    if check:
        label, cy, checked = check
        bx = 344 - 22
        fr.blit(F.art('UI-CheckBox-Up.png'), bx, cy - 11, 22, 22)
        if checked:
            fr.blit(F.art('UI-CheckBox-Check.png'), bx, cy - 11, 22, 22)
        fr.text(label, bx - 1, cy - 1, 10, F.GOLD, anchor='rm')

    return fr.im


def stamp(im, text):
    pad = 34
    out = Image.new('RGBA', (im.width, im.height + pad), (18, 18, 20, 255))
    out.alpha_composite(im, (0, pad))
    ImageDraw.Draw(out).text((10, 8), text, font=ImageFont.truetype(F.FONT, 22),
                             fill=(235, 225, 200, 255))
    return out


if __name__ == '__main__':
    tiles = [stamp(render(None).crop((0, 0, W * S, 470 * S)), 'live now  -  no toggle'),
             stamp(render(('Arena bot matches', 55, True)).crop((0, 0, W * S, 470 * S)),
                   'proposed  -  toggle on the header line')]
    gap = 20
    sheet = Image.new('RGBA', (sum(t.width for t in tiles) + gap * 3,
                               max(t.height for t in tiles) + gap * 2), (18, 18, 20, 255))
    x = gap
    for t in tiles:
        sheet.alpha_composite(t, (x, gap))
        x += t.width + gap
    sheet.convert('RGB').save(os.path.join(HERE, 'arena_preview.png'), quality=95)
    print('wrote arena_preview.png', sheet.size)
