"""Hand-pixelled chibi WoW class sprites (Mega Man proportions) for the battle logo.

Grids hold colour letters only; `outline()` adds the black keyline afterwards, the
way NES sprites wrap every shape. '.' is transparent, 'K' is drawn black inline.
All sprites are drawn facing right; `flip` turns the right-hand fighters round.
"""
from PIL import Image

K = (0, 0, 0)
BASE = {
    'K': K,
    'S': (252, 188, 148), 's': (228, 132, 96),         # skin
    'W': (252, 252, 252), 'E': (0, 0, 0),              # eye white, pupil
    'M': (188, 188, 188), 'm': (116, 116, 116),        # steel
    'G': (252, 216, 0), 'g': (200, 128, 0),            # gold
    'F': (252, 116, 0), 'f': (252, 216, 120), 'R': (216, 40, 0),   # fire
}

# body shared by everyone: head gear H/h, tunic B/b, legs L/l, boots D
BODY = [
    "...HHHHH....",
    "..HHHHHHH...",
    "..HHHSSSSS..",
    "..HHSSWESS..",
    "..HHSSWESS..",
    "...HSSSSSS..",
    "....SSSSS...",
    "...BBBBBB...",
    "..BBBBBBBB..",
    "..bBBBBBBb..",
    "...BBBBBB...",
    "...LL..LL...",
    "..LLl..LLl..",
    "..DD...DDD..",
]


class Sprite:
    def __init__(self, w, h, pal):
        self.w, self.h = w, h
        self.pal = dict(BASE, **pal)
        self.px = [['.'] * w for _ in range(h)]

    def put(self, grid, ox, oy):
        for y, row in enumerate(grid):
            for x, c in enumerate(row):
                if c != '.' and 0 <= ox + x < self.w and 0 <= oy + y < self.h:
                    self.px[oy + y][ox + x] = c
        return self

    def dot(self, pts, c):
        for x, y in pts:
            self.px[y][x] = c
        return self

    def image(self, flip=False):
        im = Image.new('RGBA', (self.w + 2, self.h + 2), (0, 0, 0, 0))
        filled = {(x, y) for y in range(self.h) for x in range(self.w) if self.px[y][x] != '.'}
        for x, y in filled:
            for dx, dy in ((1, 0), (-1, 0), (0, 1), (0, -1)):
                if (x + dx, y + dy) not in filled:
                    im.putpixel((x + dx + 1, y + dy + 1), K + (255,))
        for x, y in filled:
            im.putpixel((x + 1, y + 1), self.pal[self.px[y][x]] + (255,))
        return im.transpose(Image.FLIP_LEFT_RIGHT) if flip else im


def warrior():
    """Horned steel helm, tan plate, greatsword raised over the shoulder."""
    s = Sprite(20, 24, {'H': (188, 188, 188), 'h': (116, 116, 116), 'B': (199, 156, 110),
                        'b': (140, 96, 56), 'L': (116, 116, 116), 'l': (80, 80, 80), 'D': (96, 56, 24)})
    s.put(BODY, 2, 7)
    s.put(["W.......W",
           "WW.....WW",
           ".WHHHHHW."], 3, 5)                               # horns
    s.put(["hhhhhhh"], 4, 9)                                   # helm rim across the brow
    # sword: blade up and forward from the hand
    s.put(["..........W",
           ".........WM",
           "........WM.",
           ".......WM..",
           "......WM...",
           ".....WM....",
           "...gGg.....",
           "...SS......",
           "..g........"], 7, 8)
    return s


def rogue():
    """Purple hood and mask, twin daggers, crouched forward."""
    s = Sprite(18, 20, {'H': (88, 56, 120), 'h': (56, 32, 80), 'B': (64, 48, 72),
                        'b': (40, 28, 48), 'L': (64, 48, 72), 'l': (40, 28, 48), 'D': (24, 16, 24)})
    s.put(BODY, 2, 6)
    s.put(["..HHHHHH....",
           ".HHHHHHHH...",
           "HHHHHHHHHH.."], 1, 4)                               # hood peak, back of cowl
    s.put(["hhhhhh"], 6, 11)                                   # mask over mouth
    s.put(["H", "H", "H"], 3, 10)
    # yellow sash: the rogue class colour
    s.put(["GGGGGG"], 5, 16)
    # front dagger thrust forward, back dagger held high
    s.put(["SSmMMMW"], 10, 14)
    s.put([".W", "M.", "S."], 1, 10)
    return s


def mage():
    """Cyan pointy hat and robe, fire gathering in the outstretched hand."""
    s = Sprite(18, 24, {'H': (105, 204, 240), 'h': (40, 120, 200), 'B': (105, 204, 240),
                        'b': (40, 120, 200), 'L': (40, 120, 200), 'l': (24, 72, 152), 'D': (72, 40, 16)})
    s.put(BODY, 2, 10)
    s.put(["....hh..",
           "...HHh..",
           "..HHHh..",
           "..HHHHh.",
           ".HHHHHHh",
           "HHHHHHHHHh",
           "hhhhhhhhhhh"], 1, 3)                               # hat, brim over the eyes
    s.put(["GG"], 5, 7)                                        # hat band stars
    # robe skirt down to the boots instead of legs
    s.put(["..BBBBBBB..",
           ".BBBBBBBBB.",
           ".bBBBBBBBb."], 2, 20)
    s.put(["SS"], 11, 17)                                      # casting hand
    s.put(["..f.",
           ".fFf",
           "fFRF",
           ".fF."], 13, 15)                                    # fire in the palm
    return s


def paladin():
    """Gold winged helm, pink tabard over steel, shield up and hammer raised."""
    s = Sprite(20, 22, {'H': (252, 216, 0), 'h': (200, 128, 0), 'B': (245, 140, 186),
                        'b': (196, 80, 140), 'L': (188, 188, 188), 'l': (116, 116, 116), 'D': (116, 116, 116)})
    s.put(BODY, 4, 7)
    s.put(["WW.........",
           ".WWHHHHH...",
           "..WHHHHHHH."], 3, 5)                                # wing on the helm
    s.put(["hhhhhhh"], 6, 9)
    # hammer raised behind the head
    s.put(["MMM", "MmM", ".g.", ".g.", ".g.", ".S."], 0, 4)
    # shield held forward: steel with a gold cross
    s.put(["MMMMM",
           "MMGMM",
           "MGGGM",
           "MMGMM",
           "MMGMM",
           ".mMm.",
           "..m.."], 13, 12)
    return s


def fireball():
    s = Sprite(12, 7, {})
    s.put(["......ffF...",
           "..F.ffFFFf..",
           "RFFfFFfFFFf.",
           ".RFFFFFfWfFf",
           "RFFfFFfFFFf.",
           "..F.ffFFFf..",
           "......ffF..."], 0, 0)
    return s


def spark():
    s = Sprite(7, 7, {})
    s.put(["...W...",
           ".f.W.f.",
           "..fWf..",
           "WWWWWWW",
           "..fWf..",
           ".f.W.f.",
           "...W..."], 0, 0)
    return s


if __name__ == '__main__':
    parts = [warrior().image(), rogue().image(True), mage().image(), fireball().image(), paladin().image(True), spark().image()]
    w = sum(p.width for p in parts) + 4 * len(parts)
    h = max(p.height for p in parts)
    sheet = Image.new('RGBA', (w, h), (40, 44, 56, 255))
    x = 0
    for p in parts:
        sheet.alpha_composite(p, (x, h - p.height))
        x += p.width + 4
    sheet.resize((w * 8, h * 8), Image.NEAREST).save('fighters_sheet.png')
