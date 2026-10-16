"""Preview of ArenaFrame in Solo Shuffle mode, next to the ordinary window.

Uses clientedits' renderer (frame_render.py, the client's own art and font).
"""
import os
import sys
import textwrap

RENDER = r"C:\Projects\Gamedev\wow\clientedits\framexml\staged-centurion-ui\render"
sys.path.insert(0, RENDER)
os.chdir(RENDER)

from PIL import Image, ImageDraw, ImageFont  # noqa: E402
import frame_render as F  # noqa: E402
import arena_render as A  # noqa: E402

W, H, S = F.W, F.H, F.S
RATED_X, RATED_Y_TOP, ROW = 23, 79, 16
PRACTICE_HEADER_Y = 177
OUT = os.path.dirname(os.path.abspath(__file__))


def frame_base(title, rated_header):
    fr = F.Frame()
    fr.blit(F.art('UI-Battlefield-Icon.png'), 7, 6, 60, 60)
    fr.blit(F.art('UI-Battlefield-TopLeft.png'), 0, 0, 256, 256)
    fr.blit(F.art('UI-Battlefield-TopRight.png'), W - 128, 0, 128, 256)
    fr.blit(F.art('UI-Battlefield-BotLeft.png'), 0, H - 256, 256, 256)
    fr.blit(F.art('UI-Battlefield-BotRight.png'), W - 128, H - 256, 128, 256)
    fr.blit(F.art('UI-Battlefield-Bar.png'), 14, 256 - 95, 513, 64)
    fr.text(title, 192, 17 + 7, 12, F.GOLD, anchor='mm')
    fr.text(rated_header, 75, 55 + 7, 12, F.HILITE, anchor='lm')
    fr.text('Practice Battle:', 28, 170 + 7, 12, F.HILITE, anchor='lm')
    return fr


def buttons(fr, group_join):
    btn = F.art('UI-Panel-Button-Up.png')
    items = [(263.5, 83, 'Cancel'), (152.5, 109, 'Join Battle')]
    if group_join:
        items.append((41.5, 109, 'Join as Group'))
    for x, w, label in items:
        fr.blit(btn, x, 423 - 11, w, 22, crop=(0.0, 0.0, 0.625, 0.6875))
        fr.text(label, x + w / 2, 423, 12, F.GOLD, anchor='mm')


def bot_box(fr, checked=True):
    bx = 344 - 22
    cy = PRACTICE_HEADER_Y
    fr.blit(F.art('UI-CheckBox-Up.png'), bx, cy - 11, 22, 22)
    if checked:
        fr.blit(F.art('UI-CheckBox-Check.png'), bx, cy - 11, 22, 22)
    fr.text('Arena bot matches', bx - 1, cy - 1, 10, F.GOLD, anchor='rm')


def description(fr, text):
    p_top = 194
    dy = p_top + 4 * ROW + 40
    for i, line in enumerate(textwrap.wrap(text, 46)[:3]):
        fr.text(line, RATED_X, dy + 8 + i * 15, 12, (40, 26, 12), anchor='lm')


def ordinary():
    fr = frame_base('Arena Battles', 'Rated Battle:')
    hl = F.art('UI-Listbox-Highlight2.png')
    p_top = 194
    fr.blit(hl, RATED_X, p_top + ROW, 315, 16)
    for i, n in enumerate(['2v2 Battle', '3v3 Battle', '4v4 Battle', '5v5 Battle']):
        fr.text(n, RATED_X + 8, RATED_Y_TOP + i * ROW + 8, 12, F.GOLD, anchor='lm')
    for i, n in enumerate(['2v2 Skirmish', '3v3 Skirmish', '4v4 Skirmish', '5v5 Skirmish']):
        fr.text(n, RATED_X + 8, p_top + i * ROW + 8, 12, F.GOLD, anchor='lm')
    bot_box(fr)
    description(fr, 'Let the games begin!  The door is open to all who wish to find glory in the arena, and the Steamwheedle Cartel is prepared to accept you.')
    buttons(fr, True)
    return fr.im


def shuffle():
    fr = frame_base('Solo Shuffle', 'Queue alone. New teams every round.')
    hl = F.art('UI-Listbox-Highlight2.png')
    p_top = 194
    fr.blit(hl, RATED_X, p_top + ROW, 315, 16)
    fr.text('2v2 Solo Shuffle', RATED_X + 8, p_top + 8, 12, F.GOLD, anchor='lm')
    fr.text('3v3 Solo Shuffle', RATED_X + 8, p_top + ROW + 8, 12, F.GOLD, anchor='lm')
    bot_box(fr)
    description(fr, 'The teams are dealt again before every round: six rounds of 3v3, or three of 2v2. Win more rounds than you lose.')
    buttons(fr, False)
    return fr.im


if __name__ == '__main__':
    tiles = [A.stamp(ordinary().crop((0, 0, W * S, 470 * S)), 'Arena battlemaster (as now)'),
             A.stamp(shuffle().crop((0, 0, W * S, 470 * S)), 'Solo Shuffle battlemaster')]
    gap = 20
    sheet = Image.new('RGBA', (sum(t.width for t in tiles) + gap * 3, max(t.height for t in tiles) + gap * 2), (18, 18, 20, 255))
    x = gap
    for t in tiles:
        sheet.alpha_composite(t, (x, gap))
        x += t.width + gap
    path = os.path.join(OUT, 'solo_shuffle_window.png')
    sheet.convert('RGB').save(path, quality=95)
    print('wrote', path, sheet.size)
