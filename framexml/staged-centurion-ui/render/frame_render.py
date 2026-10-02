#!/usr/bin/env python3
"""Render PVPBattlegroundFrame at 1:1 from the client's own art, so a layout can
be judged before it costs a 1.23GB patch push.

Geometry comes straight from PVPBattlegroundFrame.xml / PVPFrame.xml /
BattlefieldFrame.xml; art and FRIZQT__.TTF are extracted from the live client.
Coordinates here are UI units with y growing DOWNWARD from the frame's top-left,
so an XML offset of y="-47" is y=47.
"""
import os
from PIL import Image, ImageDraw, ImageFont

HERE = os.path.dirname(os.path.abspath(__file__))
ART = os.path.join(HERE, 'art')
FONT = os.path.join(HERE, 'FRIZQT__.TTF')

W, H = 384, 512
S = 2                      # render scale

GOLD = (255, 209, 0)
WHITE = (255, 255, 255)
GREY = (128, 128, 128)
HILITE = (255, 255, 255)


def art(name):
    return Image.open(os.path.join(ART, name)).convert('RGBA')


def font(height):
    return ImageFont.truetype(FONT, int(round(height * S)))


class Frame:
    def __init__(self):
        self.im = Image.new('RGBA', (W * S, H * S), (0, 0, 0, 0))
        self.d = ImageDraw.Draw(self.im)

    def blit(self, image, x, y, w, h, crop=None):
        if crop:
            l, t, r, b = crop
            image = image.crop((int(l * image.width), int(t * image.height),
                                int(r * image.width), int(b * image.height)))
        image = image.resize((max(1, int(w * S)), max(1, int(h * S))), Image.LANCZOS)
        self.im.alpha_composite(image, (int(x * S), int(y * S)))

    def text(self, s, x, y, height, colour, anchor='lm'):
        f = font(height)
        self.d.text((x * S + 1, y * S + 1), s, font=f, fill=(0, 0, 0, 255), anchor=anchor)
        self.d.text((x * S, y * S), s, font=f, fill=colour, anchor=anchor)

    def width_of(self, s, height):
        f = font(height)
        return (self.d.textlength(s, font=f)) / S


def render(layout, title='PVPBattlegroundFrame'):
    """layout: dict with 'checks' [(label, y, checked, enabled, x_right)],
    'list_y', 'info_height', optional 'row' for a horizontal options row."""
    fr = Frame()

    # --- BACKGROUND: portrait (60x60 at 7,-6; the texture is 64x64)
    fr.blit(art('UI-Battlefield-Icon.png'), 7, 6, 60, 60)

    # --- BORDER: the four frame quadrants
    fr.blit(art('UI-Battlefield-TopLeft.png'), 0, 0, 256, 256)
    fr.blit(art('UI-Battlefield-TopRight.png'), W - 128, 0, 128, 256)
    fr.blit(art('UI-QueueFromAnywhere-BotLeft.png'), 0, H - 256, 256, 256)
    fr.blit(art('UI-QueueFromAnywhere-BotRight.png'), W - 128, H - 256, 128, 256)

    # --- ARTWORK: the selected battleground's picture (333x241, BOTTOM -11,105)
    fr.blit(art('PvpRandomBg.png'), 192 - 11 - 333 / 2, H - 105 - 241, 333, 241,
            crop=(0.0, 0.0, 0.6504, 0.9414))

    # frame title + column header
    fr.text('BATTLEGROUNDS', 192, 17 + 7, 12, GOLD, anchor='mm')
    fr.text('Battleground', 70, 55 + 7, 12, HILITE, anchor='lm')

    # --- battleground list: 5 rows of 16 at x=23, plus the scrollbar frame art
    ly = layout['list_y']
    names = ['Warsong Gulch', 'Scarlet Chapel', 'Blackrock Throne',
             'The Obsidian Colosseum', 'The Violet Hold']
    sb = art('UI-ClassTrainer-ScrollBar.png')
    fr.blit(sb, 23 + 295 - 3, ly + 2 - 5, 30, 84, crop=(0.0, 0.0, 0.46875, 0.65625))
    fr.blit(sb, 23 + 295 - 3, ly + 80 - 5 + 2 - 84 + 2, 30, 84, crop=(0.53125, 0.34375, 1.0, 1.0))
    for i, n in enumerate(names):
        fr.text(n, 23 + 20, ly + i * 16 + 8, 12, GOLD, anchor='lm')

    # --- description panel (298 x info_height, BOTTOMLEFT 20,105)
    ih = layout['info_height']
    itop = H - 105 - ih
    fr.text('Call to Arms', 30, itop + 16, 16, GOLD, anchor='lm')
    fr.text('Your urgent assistance is needed to combat', 30, itop + 40, 12, WHITE, anchor='lm')
    fr.text('our foes! Completing this Battleground will', 30, itop + 54, 12, WHITE, anchor='lm')
    fr.text('earn you extra rewards.', 30, itop + 68, 12, WHITE, anchor='lm')
    fr.text('Rewards', 30, itop + 94, 16, GOLD, anchor='lm')
    fr.d.rectangle([30 * S, (itop + 108) * S, 320 * S, (itop + 140) * S], fill=(62, 84, 40, 190))
    fr.d.rectangle([30 * S, (itop + 140) * S, 320 * S, (itop + 172) * S], fill=(120, 62, 32, 190))
    fr.text('Win', 45, itop + 124, 13, WHITE, anchor='lm')
    fr.text('100', 150, itop + 124, 13, WHITE, anchor='lm')
    fr.text('Loss', 45, itop + 156, 13, WHITE, anchor='lm')
    fr.text('10', 150, itop + 156, 13, WHITE, anchor='lm')

    # --- bottom buttons
    # Cancel is CENTER at (305,-419) from TOPLEFT; the other two hang off its left.
    btn = art('UI-Panel-Button-Up.png')
    for x, w, label in ((263.5, 83, 'Cancel'), (152.5, 109, 'Join Battle'), (16.5, 136, 'Join as Group')):
        # UIPanelButtonUpTexture crops to 0..0.625 / 0..0.6875 - the rest of the
        # file is other states, and stretching all of it paints a red slab.
        fr.blit(btn, x, 419 - 11, w, 22, crop=(0.0, 0.0, 0.625, 0.6875))
        fr.text(label, x + w / 2, 419, 12, GOLD, anchor='mm')

    # --- the toggles
    box = art('UI-CheckBox-Up.png')
    chk = art('UI-CheckBox-Check.png')
    chkd = art('UI-CheckBox-Check-Disabled.png')
    for label, cy, checked, enabled, xr in layout['checks']:
        bx = xr - 22
        fr.blit(box, bx, cy - 11, 22, 22)
        if checked:
            fr.blit(chk if enabled else chkd, bx, cy - 11, 22, 22)
        fr.text(label, bx - 1, cy - 1, 10, GOLD if enabled else GREY, anchor='rm')

    # the Gurubashi timer addon rides to the left of the chest label
    if layout.get('timer'):
        tx, ty = layout['timer']
        fr.text('Next chest 30:00', tx, ty, 12, WHITE, anchor='rm')

    return fr.im


LIVE = dict(
    list_y=97, info_height=214,
    checks=[('Gurubashi chest', 47, True, True, 344),
            ('Tournament queue', 65, True, False, 344),
            ('Arena bot matches', 83, True, True, 344)],
    timer=(344 - 22 - 1 - 79 - 8, 47),
)

if __name__ == '__main__':
    render(LIVE).save(os.path.join(HERE, 'preview_live.png'))
    print('wrote preview_live.png')
