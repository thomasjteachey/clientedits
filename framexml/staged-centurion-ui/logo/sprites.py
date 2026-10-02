"""Hand-placed pixel art for the Mega Man-style Centurion logos.

Every grid here is drawn by hand, one character per pixel. Palette is NES-ish,
with orange standing in for Mega Man's blue.
"""
from PIL import Image

PAL = {
    'K': (0, 0, 0),          # outline
    'D': (168, 16, 0),       # helmet shadow
    'O': (228, 92, 16),      # helmet
    'L': (252, 160, 68),     # helmet highlight
    'H': (252, 224, 168),    # glint
    'R': (216, 40, 0),       # crest
    'r': (252, 116, 96),     # crest highlight
    'S': (252, 188, 148),    # skin
    's': (228, 132, 96),     # skin shadow
    'W': (252, 252, 252),    # eye white
    'B': (0, 88, 248),       # iris (the one nod to Mega Man's blue)
}

# 24 x 25: galea with a transverse bristle crest, ear discs, Mega Man's face.
HELMET = [
    "........KK..KK..KK......",
    "......KKrrKKrrKKrK......",
    "...KrrRRrrRRrrRRrrRRK...",
    ".KRRrrRRrrRRrrRRrrRRrrK.",
    "KrRRrrRRrrRRrrRRrrRRrrRK",
    "KrRRrrRRrrRRrrRRrrRRrrRK",
    "KRDDRRDDRRDDRRDDRRDDRRDK",
    ".KKKKKKKKKKKKKKKKKKKKKK.",
    "...KHLOOOOOLLOOOOODDK...",
    "..KHLOOOOOOLLOOOOOODDK..",
    ".KLOOOOOOOOLLOOOOOOODDK.",
    ".KKKKKKKKKKKKKKKKKKKKKK.",
    "KOOKSSSSSSSSSSSSSSSSKOOK",
    "KOLKSSKKKSSSSSSKKKSSKLOK",
    "KLHKSKWWWKSSSSKWWWKSKHLK",
    "KLHKSKWWBKSSSSKBWWKSKHLK",
    "KLHKSKWWBKSSSSKBWWKSKHLK",
    "KOLKSSKKKSSSSSSKKKSSKLOK",
    "KOOKSSSSSSSSSSSSSSSSKOOK",
    "KDOKsSSSSSKKKKSSSSSsKODK",
    ".KDOOKsSSSSSSSSSSsKOODK.",
    "..KDOOKKsSSSSSSsKKOODK..",
    "...KDDOOKKKKKKKKOODDK...",
    "....KKDDK......KDDKK....",
    "......KK........KK......",
]

# 7 x 7 bold capitals, the Mega Man HUD font's weight.
FONT = {
    'C': [".#####.", "##...##", "##.....", "##.....", "##.....", "##...##", ".#####."],
    'E': ["#######", "##.....", "##.....", "######.", "##.....", "##.....", "#######"],
    'N': ["##...##", "###..##", "####.##", "##.####", "##..###", "##...##", "##...##"],
    'T': ["#######", "..###..", "..###..", "..###..", "..###..", "..###..", "..###.."],
    'U': ["##...##", "##...##", "##...##", "##...##", "##...##", "##...##", ".#####."],
    'R': ["######.", "##...##", "##...##", "######.", "##.##..", "##..##.", "##...##"],
    'I': ["######", "..##..", "..##..", "..##..", "..##..", "..##..", "######"],
    'O': [".#####.", "##...##", "##...##", "##...##", "##...##", "##...##", ".#####."],
}


def sprite(grid, pal=PAL):
    w, h = len(grid[0]), len(grid)
    for row in grid:
        assert len(row) == w, row
    im = Image.new('RGBA', (w, h), (0, 0, 0, 0))
    for y, row in enumerate(grid):
        for x, c in enumerate(row):
            if c != '.':
                im.putpixel((x, y), pal[c] + (255,))
    return im


if __name__ == '__main__':
    sprite(HELMET).resize((24 * 12, len(HELMET) * 12), Image.NEAREST).save('helmet_sprite.png')
