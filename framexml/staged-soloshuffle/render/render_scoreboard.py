"""Render the Buffle Shuffle end-of-match scoreboard at 1:1 from the client's own art.

Geometry is WorldStateFrame.xml's (live patch-enUS-2 copy) with the live
WorldStateFrame.lua column anchors and CenturionSquadFrames.lua's Wins column
and widening. UI units, y growing downward from the frame's top-left.
"""
import os
import sys
from PIL import Image, ImageDraw, ImageFont

ART = sys.argv[1]
OUT = sys.argv[2]
FONT = r"C:\Projects\Gamedev\wow\clientedits\framexml\staged-centurion-ui\render\FRIZQT__.TTF"
S = 2

GOLD_TEXT = (255, 209, 0)
WHITE = (255, 255, 255)
SQUAD = {0: {'name': (255, 199, 0), 'band': (0.85, 0.71, 0.26)},
         1: {'name': (51, 224, 77), 'band': (0.19, 0.57, 0.11)}}
CLASS = {'WARRIOR': (0, 0.25, 0, 0.25), 'MAGE': (0.25, 0.49609375, 0, 0.25),
         'ROGUE': (0.49609375, 0.7421875, 0, 0.25), 'DRUID': (0.7421875, 0.98828125, 0, 0.25),
         'WARLOCK': (0.7421875, 0.98828125, 0.25, 0.5)}

# name, class, squad at the end (0 = won more rounds than lost), KB, damage, healing, rounds won
ROWS = [
    ('Drunks Brokilo', 'DRUID', 0, 2, 21480, 64210, 4),
    ('Very Smol', 'MAGE', 1, 4, 58340, 0, 3),
    ('Glizzy Guzzler', 'MAGE', 1, 3, 51220, 1200, 2),
    ('Stonedsoul Lastname', 'WARLOCK', 0, 5, 66710, 9800, 4),
    ('Aldric Bladewright', 'WARRIOR', 1, 3, 47950, 0, 3),
    ('Roderic Venomshade', 'ROGUE', 1, 1, 39880, 0, 2),
]


def art(name):
    return Image.open(os.path.join(ART, name + '.png')).convert('RGBA')


def font(h):
    return ImageFont.truetype(FONT, int(round(h * S)))


def tint(im, rgb):
    r, g, b, a = im.split()
    r = r.point(lambda v: int(v * rgb[0]))
    g = g.point(lambda v: int(v * rgb[1]))
    b = b.point(lambda v: int(v * rgb[2]))
    return Image.merge('RGBA', (r, g, b, a))


class Canvas:
    def __init__(self, w, h):
        self.w, self.h = w, h
        self.im = Image.new('RGBA', (w * S, h * S), (0, 0, 0, 0))
        self.d = ImageDraw.Draw(self.im)

    def blit(self, image, x, y, w, h, crop=None, flip=False):
        if crop:
            l, r, t, b = crop
            image = image.crop((int(l * image.width), int(t * image.height),
                                int(r * image.width), int(b * image.height)))
        if flip:
            image = image.transpose(Image.FLIP_LEFT_RIGHT)
        image = image.resize((max(1, int(w * S)), max(1, int(h * S))), Image.LANCZOS)
        self.im.alpha_composite(image, (int(x * S), int(y * S)))

    def tile_x(self, image, x, y, w, h):
        # TexCoords right > 1: the texture repeats across the width.
        tile = image.resize((int(256 * S), int(h * S)), Image.LANCZOS)
        strip = Image.new('RGBA', (int(w * S), int(h * S)), (0, 0, 0, 0))
        for ox in range(0, int(w * S), tile.width):
            strip.alpha_composite(tile, (ox, 0))
        self.im.alpha_composite(strip, (int(x * S), int(y * S)))

    def text(self, s, x, y, h, colour, anchor='mm', spacing=1):
        f = font(h)
        if '\n' in s:
            self.d.multiline_text((x * S + 1, y * S + 1), s, font=f, fill=(0, 0, 0, 255), anchor=anchor, align='center', spacing=spacing * S)
            self.d.multiline_text((x * S, y * S), s, font=f, fill=colour, anchor=anchor, align='center', spacing=spacing * S)
        else:
            self.d.text((x * S + 1, y * S + 1), s, font=f, fill=(0, 0, 0, 255), anchor=anchor)
            self.d.text((x * S, y * S), s, font=f, fill=colour, anchor=anchor)


def scoreboard(columns, widened, borrowed_last=True):
    """columns: list of (header, value_fn); the first sits where Killing Blows does."""
    # WorldStateScoreFrame_Resize, unrated arena: padding + Name + Class + 43 + 4 column spacings.
    W = 35 + 175 + 16 + 43 + 4 * 76 + (76 if widened else 0)
    H = 512
    c = Canvas(W, H)

    c.tile_x(art('WorldStateFinalScoreFrame-TopBackground'), 13, 20, W - 129, 64)
    c.blit(art('WorldStateFinalScoreFrame-TopLeft'), 8, 16, 128, 256)
    c.blit(art('WorldStateFinalScoreFrame-TopRight'), W - 256, 16, 256, 256)
    c.blit(art('WorldStateFinalScoreFrame-Top'), 136, 16, W - 256 - 136, 256)
    c.blit(art('WorldStateFinalScoreFrame-BotLeft'), 8, 272, 128, 256)
    c.blit(art('WorldStateFinalScoreFrame-BotRight'), W - 256, 272, 256, 256)
    c.blit(art('WorldStateFinalScoreFrame-Bot'), 136, 272, W - 256 - 136, 256)

    # Winner banner (WorldStateScoreWinnerFrame: 17..W-141, y 20..36), coloured by CenturionSquadFrames.
    hl = art('WorldStateFinalScore-Highlight')
    band = SQUAD[0]['band']
    c.blit(tint(hl, band), 17, 20, 256, 16)
    c.blit(tint(hl, band), 273, 20, (W - 141) - 273, 16, flip=True)
    c.text('Gold Team Wins', (17 + W - 141) / 2, 28, 12, SQUAD[0]['name'])

    # Header row: Class (18,48) 16x16; Name text 20 in from the Name button; columns centred at y 56.
    c.text('Name', 38, 56, 10, WHITE, anchor='lm')
    centres = []
    left = 18 + 175 + 4                      # KB: LEFT to Name RIGHT, +4
    for i, (header, _) in enumerate(columns):
        if i == 3 and borrowed_last:         # the borrowed column: LEFT to Healing RIGHT, +5
            left = left + 10
        centres.append(left + 75 / 2)
        c.text(header, left + 75 / 2, 56, 10, WHITE)
        left = left + 75 - 5                 # next column: LEFT to this RIGHT, -5

    # Rows: WorldStateScoreButton1 at (17,78), 16 high, width frame - 137.
    rw = W - 137
    icons = art('Icons-Classes')
    for n, row in enumerate(ROWS):
        name, cls, squad, kb, dmg, heal, wins = row
        y = 78 + n * 16
        b = SQUAD[squad]['band']
        c.blit(tint(hl, b), 17, y, 256, 16)
        c.blit(tint(hl, b), 273, y, 17 + rw - 273, 16, flip=True)
        c.blit(icons, 17, y, 16, 16, crop=CLASS[cls])
        c.text(name, 37, y + 8 + 1, 12, SQUAD[squad]['name'], anchor='lm')
        for centre, (_, fn) in zip(centres, columns):
            c.text(str(fn(row)), centre, 56 + 28 + n * 16, 10, GOLD_TEXT)

    last = 78 + len(ROWS) * 16
    c.text('3 Alliance Players / 3 Horde Players', 32, last + 6, 10, GOLD_TEXT, anchor='lt')
    c.text('Time Elapsed: 7 Min', 17 + rw - 20, last + 7, 10, GOLD_TEXT, anchor='rt')

    c.text('Arena closing in', 20, H - 77, 10, WHITE, anchor='lb')
    c.text('1 Min 52 Sec', 20 + 92, H - 77, 10, GOLD_TEXT, anchor='lb')
    btn = art('UI-Panel-Button-Up')
    bx = W / 2 - 30 - 153 / 2
    c.blit(btn, bx, H - 76 - 21, 153, 21, crop=(0, 0.625, 0, 0.6875))
    c.text('Leave Arena', bx + 153 / 2, H - 76 - 21 + 10.5, 12, GOLD_TEXT)

    # Crop to the frame's visible art (HitRectInsets right 30, bottom 70).
    return c.im.crop((0, 0, (W - 20) * S, (H - 60) * S))


KB = lambda r: r[3]
DMG = lambda r: r[4]
HEAL = lambda r: r[5]
WINS = lambda r: r[6]

if len(sys.argv) > 3 and sys.argv[3] == 'final':
    panels = [
        ('BEFORE (live)', scoreboard([('Killing\nBlows', KB), ('Damage\nDone', DMG), ('Healing\nDone', HEAL), ('Wins', WINS)], True)),
        ('AFTER (this change)', scoreboard([('Rounds\nWon', WINS), ('Killing\nBlows', KB), ('Damage\nDone', DMG), ('Healing\nDone', HEAL)], True, borrowed_last=False)),
    ]
else:
    panels = [
        ('NOW (live, patch-enUS-A 1.00253)', scoreboard([('Killing\nBlows', KB), ('Damage\nDone', DMG), ('Healing\nDone', HEAL), ('Wins', WINS)], True)),
        ('OPTION 1: Rounds Won first, Killing Blows kept', scoreboard([('Rounds\nWon', WINS), ('Damage\nDone', DMG), ('Healing\nDone', HEAL), ('Killing\nBlows', KB)], True)),
        ('OPTION 2: Rounds Won replaces Killing Blows', scoreboard([('Rounds\nWon', WINS), ('Damage\nDone', DMG), ('Healing\nDone', HEAL)], False)),
    ]

pad, cap = 24 * S, 34 * S
width = sum(p.width for _, p in panels) + pad * (len(panels) + 1)
height = max(p.height for _, p in panels) + cap + pad
sheet = Image.new('RGBA', (width, height), (24, 24, 28, 255))
d = ImageDraw.Draw(sheet)
x = pad
for title, p in panels:
    d.text((x, pad // 2 + 4 * S), title, font=font(13), fill=(235, 235, 235))
    sheet.alpha_composite(p, (x, cap))
    x += p.width + pad
sheet.convert('RGB').save(OUT, quality=92)
print('wrote', OUT, sheet.size)
