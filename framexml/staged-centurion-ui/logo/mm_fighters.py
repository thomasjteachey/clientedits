"""WoW classes as NES Mega Man sprites.

Drawn to the rules of the Mega Man 2 sprite sheet: ~24px tall, a helmeted head a
dozen pixels wide, two tones of one hue per character (light for the highlight
strips, dark for the mass), black for the outline AND the inner lines, flat skin,
3x3 white eyes with a 2px pupil, a wide stance on big flat boots.

Letters: K black, c light, B dark, S skin, W white,
         X/x accent light/dark (steel, gold), F/f/R fire.
Grids face right and are assembled from parts; '.' is transparent.
"""
from PIL import Image

COMMON = {
    'K': (0, 0, 0), 'S': (252, 228, 160), 'W': (252, 252, 252),
    'M': (188, 188, 188), 'm': (116, 116, 116),
    'F': (252, 116, 0), 'f': (252, 216, 120), 'R': (216, 40, 0),
}

HEAD = [                      # 13 wide, faces right; ear stud W on the left
    ".....KKKK....",
    "...KKccBBKK..",
    "..KccBBBBBBK.",
    ".KcBBBBBBBBBK",
    ".KBBBBBBBBBBK",
    ".KBBKKKKKKKBK",
    ".KWKSSWWWSSBK",
    ".KWKSSWWKKSBK",
    ".KWKSSWWKKSK.",
    ".KBKSSSSSSSK.",
    "..KBKSSKKKSK.",
    "...KKSSSSSK..",
]

BODY_STAND = [                # 20 wide; head sits at column 3
    ".....KcKKKKKKKKcK...",
    "....KccBKSSSSSKcBK..",
    "...KcBBKKKKKKKKKBBK.",
    "..KcBBKcccccccBKBBK.",
    "..KBBBKcBBBBBBBKBBK.",
    "..KSSSKKKKKKKKKKSSK.",
    "..KSSSKxxxxxxxxKSSK.",
    "...KKKKcBBBBBBBKKK..",
    "......KcBBKKcBBK....",
    ".....KcBBK..KcBBK...",
    "...KKBBBBK..KBBBBKK.",
    "..KBBBBBBK..KBBBBBBK",
    "..KKKKKKKK..KKKKKKKK",
]

BODY_SHOOT = [                # front arm thrust straight out, like the buster pose
    ".....KcKKKKKKKKK.......",
    "....KccBKSSSSSKccccKKK.",
    "...KcBBKKKKKKKKcccccccK",
    "..KcBBKcccccccKKBBBBSSK",
    "..KBBBKcBBBBBBBKKKKKSSK",
    "..KSSSKKKKKKKKKK...KKK.",
    "..KSSSKxxxxxxxxK.......",
    "...KKKKcBBBBBBBK.......",
    "......KcBBKKcBBK.......",
    ".....KcBBK..KcBBK......",
    "...KKBBBBK..KBBBBKK....",
    "..KBBBBBBK..KBBBBBBK...",
    "..KKKKKKKK..KKKKKKKK...",
]

BODY_LEAP = [                 # mid-jump: knees up, one boot kicked back
    ".....KcKKKKKKKKcK...",
    "....KccBKSSSSSKcBK..",
    "...KcBBKKKKKKKKKBBK.",
    "..KcBBKcccccccBKBBK.",
    "..KBBBKcBBBBBBBKBBK.",
    "..KSSSKKKKKKKKKKSSK.",
    "..KSSSKxxxxxxxxKSSK.",
    "...KKKKcBBBBBBBKKK..",
    "..KKKKKcBBKKcBBBK...",
    ".KBBBBBBBK.KcBBBBK..",
    ".KBBBBBBK...KBBBBBK.",
    "..KKKKKK.....KKKKKK.",
]


class Sprite:
    def __init__(self, w, h, light, dark, acc=((200, 200, 200), (120, 120, 120))):
        self.w, self.h = w, h
        self.pal = dict(COMMON, c=light, B=dark, X=acc[0], x=acc[1])
        self.px = [['.'] * w for _ in range(h)]

    def put(self, grid, ox, oy):
        for y, row in enumerate(grid):
            for x, ch in enumerate(row):
                if ch != '.' and 0 <= ox + x < self.w and 0 <= oy + y < self.h:
                    self.px[oy + y][ox + x] = ch
        return self

    def image(self, flip=False):
        im = Image.new('RGBA', (self.w, self.h), (0, 0, 0, 0))
        for y in range(self.h):
            for x in range(self.w):
                ch = self.px[y][x]
                if ch != '.':
                    im.putpixel((x, y), self.pal[ch] + (255,))
        im = im.crop(im.getchannel('A').getbbox())
        return im.transpose(Image.FLIP_LEFT_RIGHT) if flip else im


def warrior():
    """Rust-red plate, horned helmet, leaping in with a greatsword."""
    s = Sprite(28, 30, (228, 120, 72), (148, 48, 16))
    s.put(BODY_LEAP, 2, 17)
    s.put(HEAD, 5, 6)
    s.put(["KK.........KK",
           "KWK.......KWK",
           ".KWK.....KWK.",
           "..KWKKKKKWK..",
           "...KK...KK..."], 5, 3)                     # horns rising off the helmet
    s.put(["..............KK",
           ".............KWXK",
           "............KWXK.",
           "...........KWXK..",
           "..........KWXK...",
           ".........KWXK....",
           "........KWXK.....",
           ".....KKKWXK......",
           ".....KxxxxK......",
           "......KKSSK......",
           ".......KSSK......"], 10, 12)                # greatsword raised over the head
    return s


def rogue():
    """Purple hood with a mask across the mouth, dagger thrust forward."""
    s = Sprite(28, 26, (168, 120, 216), (96, 56, 144), acc=((252, 245, 105), (200, 160, 0)))
    s.put(BODY_SHOOT, 1, 12)
    s.put(HEAD, 4, 1)
    s.put(["......KKKK....",
           "....KKcBBBKK..",
           "..KKccBBBBBBK.",
           ".KcBBBBBBBBBBK",
           "KcBBBBBBBBBBBK",
           "KBBBKKKKKKKBBK",
           "KBBK..........",
           "KBBK..........",
           ".KBK.........."], 3, 0)                    # hood drawn over the helmet shape
    s.put(["KBBBBBBK", "KKKKKKKK"], 8, 10)             # mask over the mouth
    s.put(["KKKKKK", "WWWWXK", "KKKKKK"], 23, 14)      # dagger from the outstretched fist
    return s


def mage():
    """Tall pointed hat, firing a fireball buster-style."""
    s = Sprite(30, 32, (140, 220, 252), (40, 112, 216), acc=((252, 216, 0), (200, 128, 0)))
    s.put(BODY_SHOOT, 1, 19)
    s.put(HEAD, 4, 8)
    s.put([".......KK.....",
           "......KcBK....",
           ".....KccBK....",
           ".....KcBBBK...",
           "....KcBBBBK...",
           "....KcBXBBBK..",
           "...KcBBBBBBBK.",
           "..KcBBBBBBBBBK",
           "KKKKKKKKKKKKKKKK",
           ".KccccccccccccK.",
           "..KKKKKKKKKKKK.."], 2, 0)                  # hat and brim
    s.put(["..fK", ".fFFK", "fFRFFK", ".fFFK", "..fK"], 24, 20)   # fire leaving the palm
    return s


def paladin():
    """Gold-crested helm, pink tabard, shield braced and hammer cocked back."""
    s = Sprite(30, 28, (252, 176, 208), (216, 88, 152), acc=((252, 216, 0), (200, 128, 0)))
    s.put(BODY_STAND, 4, 15)
    s.put(HEAD, 7, 4)
    s.put(["....KKKKK....",
           "...KXXXXXK...",
           "..KXxXXXXxK..",
           "...KKKKKKK..."], 7, 1)                     # gold crest on the helm
    s.put(["KKKKKKK",
           "KMMMMMK",
           "KMmmmMK",
           "KMMMMMK",
           "KKKKKKK",
           "..KxK..",
           "..KxK..",
           "..KxK..",
           "..KxK..",
           "..KxK..",
           ".KSSSK.",
           ".KSSSK."], 0, 8)                             # hammer behind, fist at the hip
    s.put(["..KKKKKK..",
           ".KMMMMMMK.",
           "KMMMXXMMmK",
           "KMXXXXXXmK",
           "KMMMXXMMmK",
           "KMMMXXMMmK",
           ".KMMXXMmK.",
           "..KMMmmK..",
           "...KKKK..."], 19, 12)                      # shield with a gold cross
    return s


def fireball():
    s = Sprite(14, 9, (0, 0, 0), (0, 0, 0))
    s.put(["......KKKK....",
           "....KKffFFKK..",
           "..KKFfffFFFFK.",
           "KRFFfWWffFFFFK",
           "KRRFfWWffFFFFK",
           "..KKFfffFFFFK.",
           "....KKffFFKK..",
           "......KKKK...."], 0, 0)
    return s


def pellet():
    """Mega Buster shot: the tiny lemon, here as the clash between blades."""
    s = Sprite(9, 9, (0, 0, 0), (0, 0, 0))
    s.put(["....W....",
           "....W....",
           "..W.f.W..",
           "...fFf...",
           "WWfFWFfWW",
           "...fFf...",
           "..W.f.W..",
           "....W....",
           "....W...."], 0, 0)
    return s


if __name__ == '__main__':
    parts = [warrior().image(), rogue().image(True), mage().image(), fireball().image(),
             paladin().image(True), pellet().image()]
    w = sum(p.width for p in parts) + 6 * len(parts)
    h = max(p.height for p in parts)
    sheet = Image.new('RGBA', (w, h), (40, 44, 56, 255))
    x = 0
    for p in parts:
        sheet.alpha_composite(p, (x, h - p.height))
        x += p.width + 6
    sheet.resize((w * 7, h * 7), Image.NEAREST).save('mm_sheet.png')
