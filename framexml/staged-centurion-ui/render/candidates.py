#!/usr/bin/env python3
"""Render the layout candidates side by side, labelled."""
import os
from PIL import Image, ImageDraw, ImageFont
import frame_render as F

HERE = os.path.dirname(os.path.abspath(__file__))

# --- A: what is live right now -------------------------------------------------
A = dict(
    list_y=97, info_height=214,
    checks=[('Gurubashi chest', 47, True, True, 344),
            ('Tournament queue', 65, True, False, 344),
            ('Arena bot matches', 83, True, True, 344)],
    timer=(344 - 22 - 1 - 79 - 8, 47),
)

# --- B: the stack moves below the list; header and list go back to stock -------
B = dict(
    list_y=79, info_height=178,
    checks=[('Gurubashi chest', 178, True, True, 344),
            ('Tournament queue', 195, True, False, 344),
            ('Arena bot matches', 212, True, True, 344)],
    timer=(344 - 22 - 1 - 79 - 8, 178),
)

# --- C: one options row below the list, short labels ---------------------------
#   timer(79) + 8 + [chest 26.5+1+22] + gap + [tournament 58+1+22] + gap + [arena 51.5+1+22]
C = dict(
    list_y=79, info_height=196,
    checks=[('Chest', 180, True, True, 23 + 79 + 8 + 26.5 + 1 + 22),
            ('Tournament', 180, True, False, 23 + 79 + 8 + 26.5 + 1 + 22 + 14 + 58 + 1 + 22),
            ('Arena bots', 180, True, True, 344)],
    timer=(23 + 79, 180),
)

# --- D: nothing in the frame but an Options button; the toggles live in its menu
D = dict(
    list_y=79, info_height=232,
    checks=[],
    timer=(344 - 62 - 8, 55),
)

TITLES = {
    'A': 'A  -  live now (third box sits on the list border)',
    'B': 'B  -  toggles below the list, header back to stock',
    'C': 'C  -  one options row, short labels',
    'D': 'D  -  an Options menu (shown open); frame otherwise stock',
}


def render_d():
    im = F.render(D)
    fr = F.Frame()
    fr.im = im
    fr.d = ImageDraw.Draw(im)
    S = F.S
    # the button itself, where the Wintergrasp timer used to sit
    fr.blit(F.art('UI-Panel-Button-Up.png'), 344 - 66, 55 - 11, 66, 22,
            crop=(0.0, 0.0, 0.625, 0.6875))
    fr.text('Options', 344 - 33, 55, 12, F.GOLD, anchor='mm')

    # the menu it opens
    mx, my, mw, mh = 190, 70, 154, 66
    fr.d.rectangle([mx * S, my * S, (mx + mw) * S, (my + mh) * S], fill=(8, 8, 10, 242),
                   outline=(120, 100, 60, 255), width=2)
    box = F.art('UI-CheckBox-Up.png')
    chk = F.art('UI-CheckBox-Check.png')
    chkd = F.art('UI-CheckBox-Check-Disabled.png')
    for i, (label, enabled) in enumerate((('Gurubashi chest', True),
                                          ('Tournament queue', False),
                                          ('Arena bot matches', True))):
        cy = my + 13 + i * 20
        fr.blit(box, mx + 6, cy - 9, 18, 18)
        fr.blit(chk if enabled else chkd, mx + 6, cy - 9, 18, 18)
        fr.text(label, mx + 29, cy, 10, F.WHITE if enabled else F.GREY, anchor='lm')
    return im


def stamp(im, text):
    pad = 34
    out = Image.new('RGBA', (im.width, im.height + pad), (18, 18, 20, 255))
    out.alpha_composite(im, (0, pad))
    d = ImageDraw.Draw(out)
    f = ImageFont.truetype(F.FONT, 22)
    d.text((10, 8), text, font=f, fill=(235, 225, 200, 255))
    return out


if __name__ == '__main__':
    tiles = []
    for key, layout in (('A', A), ('B', B), ('C', C), ('D', None)):
        im = (render_d() if key == 'D' else F.render(layout)).crop((0, 0, 384 * F.S, 470 * F.S))
        tiles.append(stamp(im, TITLES[key]))
    w = sum(t.width for t in tiles) + 20 * (len(tiles) + 1)
    h = max(t.height for t in tiles) + 40
    sheet = Image.new('RGBA', (w, h), (18, 18, 20, 255))
    x = 20
    for t in tiles:
        sheet.alpha_composite(t, (x, 20))
        x += t.width + 20
    sheet.convert('RGB').save(os.path.join(HERE, 'candidates.png'), quality=95)
    print('wrote candidates.png', sheet.size)
