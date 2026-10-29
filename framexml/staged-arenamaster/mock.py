"""Arena Master mockups, drawn 1:1 from the client's own textures and fonts.

The frame is the bounty board's recipe (CENTURION_BountyBoard.lua): the
achievement frame's wood border, carved header and plate, the category list on
the left and a cork board on the right. Coordinates are UI units, y down from
the frame's top-left, so they port straight into the addon."""
import os

from PIL import Image, ImageChops, ImageDraw, ImageFont, ImageOps

HERE = os.path.dirname(os.path.abspath(__file__))
ART = os.path.join(HERE, 'art')
OUT = os.path.join(HERE, 'out')
S = 2

GOLD = (255, 209, 0)
WHITE = (255, 255, 255)
GREY = (128, 128, 128)
INK = (40, 24, 6)
BLOOD = (110, 12, 8)
WIN = (16, 84, 8)
FRIZ = 'FRIZQT__.TTF'
MORPH = 'MORPHEUS.TTF'

CLASS_TC = {1: (0, 0.25, 0, 0.25), 8: (0.25, 0.49609375, 0, 0.25), 4: (0.49609375, 0.7421875, 0, 0.25),
            11: (0.7421875, 0.98828125, 0, 0.25), 3: (0, 0.25, 0.25, 0.5), 7: (0.25, 0.49609375, 0.25, 0.5),
            5: (0.49609375, 0.7421875, 0.25, 0.5), 9: (0.7421875, 0.98828125, 0.25, 0.5),
            2: (0, 0.25, 0.5, 0.75), 6: (0.25, 0.49609375, 0.5, 0.75)}
CLASS_NAME = {1: 'Warrior', 2: 'Paladin', 3: 'Hunter', 4: 'Rogue', 5: 'Priest', 7: 'Shaman', 8: 'Mage',
              9: 'Warlock', 11: 'Druid'}

FW, FH = 768, 500
OX, OY = 20, 92                 # the frame inside the canvas; the header rises above it
CW, CH = FW + 2 * OX, FH + OY + 20

_art, _fonts = {}, {}


def art(name):
    if name not in _art:
        _art[name] = Image.open(os.path.join(ART, name + '.png')).convert('RGBA')
    return _art[name]


def font(face, size):
    key = (face, size)
    if key not in _fonts:
        _fonts[key] = ImageFont.truetype(os.path.join(ART, face), int(round(size * S)))
    return _fonts[key]


def desaturate(im):
    g = ImageOps.grayscale(im.convert('RGB')).convert('RGBA')
    g.putalpha(im.split()[3])
    return g


def loadscreen(name, w, h, cy=0.5, zoom=1.0):
    """A loading screen shown at its real 4:3 shape, cut to w x h around cy."""
    im = art(name).resize((1365, 1024), Image.LANCZOS)
    cw = 1365 * zoom
    ch = cw * h / w
    if ch > 1024:
        ch = 1024
        cw = ch * w / h
    x0 = (1365 - cw) / 2
    y0 = min(max(0, cy * 1024 - ch / 2), 1024 - ch)
    return im.crop((int(x0), int(y0), int(x0 + cw), int(y0 + ch)))


class Canvas:
    def __init__(self, w, h):
        self.im = Image.new('RGBA', (w * S, h * S), (0, 0, 0, 0))
        self.d = ImageDraw.Draw(self.im)

    def blit(self, image, x, y, w, h, crop=None, alpha=1.0, flip_x=False, flip_y=False, add=False,
             tint=None, desat=False):
        if isinstance(image, str):
            image = art(image)
        if crop:
            l, r, t, b = crop
            image = image.crop((int(round(l * image.width)), int(round(t * image.height)),
                                int(round(r * image.width)), int(round(b * image.height))))
        image = image.resize((max(1, int(round(w * S))), max(1, int(round(h * S)))), Image.LANCZOS)
        if flip_x:
            image = image.transpose(Image.FLIP_LEFT_RIGHT)
        if flip_y:
            image = image.transpose(Image.FLIP_TOP_BOTTOM)
        if desat:
            image = desaturate(image)
        if tint:
            ch = image.split()
            image = Image.merge('RGBA', tuple(ch[i].point(lambda v, k=tint[i]: v * k) for i in range(3)) + (ch[3],))
        if alpha < 1.0:
            image.putalpha(image.split()[3].point(lambda v: v * alpha))
        px, py = int(round(x * S)), int(round(y * S))
        if add:
            base = self.im.crop((px, py, px + image.width, py + image.height))
            black = Image.new('RGBA', image.size, (0, 0, 0, 0))
            premul = Image.composite(image, black, image.split()[3])
            summed = ImageChops.add(base.convert('RGB'), premul.convert('RGB')).convert('RGBA')
            summed.putalpha(base.split()[3])
            self.im.paste(summed, (px, py))
            return
        self.im.alpha_composite(image, (px, py))

    def tile(self, image, x, y, w, h, ts):
        t = art(image).resize((int(ts * S), int(ts * S)), Image.LANCZOS)
        layer = Image.new('RGBA', (int(w * S), int(h * S)), (0, 0, 0, 0))
        for yy in range(0, layer.height, t.height):
            for xx in range(0, layer.width, t.width):
                layer.alpha_composite(t, (xx, yy))
        self.im.alpha_composite(layer, (int(x * S), int(y * S)))

    def fill(self, x, y, w, h, rgba):
        layer = Image.new('RGBA', (max(1, int(w * S)), max(1, int(h * S))), rgba)
        self.im.alpha_composite(layer, (int(x * S), int(y * S)))

    def gradient(self, x, y, w, h, vertical, a1, a2):
        """Black, alpha a1 at the top/left to a2 at the bottom/right."""
        W, H = max(1, int(w * S)), max(1, int(h * S))
        mask = Image.new('L', (W, H))
        md = mask.load()
        for yy in range(H):
            for xx in range(W):
                f = yy / max(1, H - 1) if vertical else xx / max(1, W - 1)
                md[xx, yy] = int(255 * (a1 + (a2 - a1) * f))
        layer = Image.new('RGBA', (W, H), (0, 0, 0, 255))
        layer.putalpha(mask)
        self.im.alpha_composite(layer, (int(x * S), int(y * S)))

    def backdrop(self, x, y, w, h, bg, ts, edge, e, inset):
        self.tile(bg, x + inset, y + inset, w - 2 * inset, h - 2 * inset, ts)
        im = art(edge)
        sl = im.width / 8.0

        def piece(i):
            return im.crop((int(i * sl), 0, int((i + 1) * sl), im.height))
        yy = y + e
        while yy < y + h - e - 0.01:
            hh = min(e, y + h - e - yy)
            for idx, xx in ((0, x), (1, x + w - e)):
                p = piece(idx)
                p = p.crop((0, 0, p.width, max(1, int(p.height * hh / e))))
                self.blit(p, xx, yy, e, hh)
            yy += e
        xx = x + e
        while xx < x + w - e - 0.01:
            ww = min(e, x + w - e - xx)
            for idx, yv in ((2, y), (3, y + h - e)):
                p = piece(idx).rotate(90, expand=True)
                p = p.crop((0, 0, max(1, int(p.width * ww / e)), p.height))
                self.blit(p, xx, yv, ww, e)
            xx += e
        self.blit(piece(4), x, y, e, e)
        self.blit(piece(5), x + w - e, y, e, e)
        self.blit(piece(6), x, y + h - e, e, e)
        self.blit(piece(7), x + w - e, y + h - e, e, e)

    # ---- text
    def text(self, s, x, y, size=12, colour=WHITE, face=FRIZ, shadow=(0, 0, 0), anchor='lm', alpha=255):
        f = font(face, size)
        if shadow is not None:
            self.d.text(((x + 1) * S, (y + 1) * S), s, font=f, fill=tuple(shadow) + (alpha,), anchor=anchor)
        self.d.text((x * S, y * S), s, font=f, fill=tuple(colour) + (alpha,), anchor=anchor)

    def width(self, s, size=12, face=FRIZ):
        return self.d.textlength(s, font=font(face, size)) / S

    def para(self, s, x, y, maxw, size=12, colour=INK, face=FRIZ, shadow=None, leading=None, anchor='la'):
        leading = leading or size + 4
        lines = []
        for p in s.split('\n'):
            cur = ''
            for wd in p.split(' '):
                t = (cur + ' ' + wd).strip()
                if self.width(t, size, face) <= maxw or not cur:
                    cur = t
                else:
                    lines.append(cur)
                    cur = wd
            lines.append(cur)
        for i, line in enumerate(lines):
            lx = x if anchor[0] == 'l' else x + maxw / 2
            self.text(line, lx, y + i * leading, size, colour, face, shadow, anchor=anchor)
        return y + len(lines) * leading

    # ---- widgets
    def panel_button(self, label, x, y, w, h=22, disabled=False, size=12):
        btn = 'UI-Panel-Button-Disabled' if disabled else 'UI-Panel-Button-Up'
        self.blit(btn, x, y, 12, h, crop=(0, 0.09375, 0, 0.6875))
        self.blit(btn, x + 12, y, w - 24, h, crop=(0.09375, 0.53125, 0, 0.6875))
        self.blit(btn, x + w - 12, y, 12, h, crop=(0.53125, 0.625, 0, 0.6875))
        self.text(label, x + w / 2, y + h / 2, size, GREY if disabled else GOLD, anchor='mm')

    def icon_ring(self, icon, x, y, ring, crop=None, desat=False, alpha=1.0):
        """An icon in the achievement frame's round ring (the bounty card's portrait)."""
        inner = ring * 0.75
        off = (ring - inner) / 2
        self.blit(icon, x + off, y + off, inner, inner, crop=crop or (0.07, 0.93, 0.07, 0.93), desat=desat,
                  alpha=alpha)
        self.blit('UI-Achievement-IconFrame', x, y, ring, ring, crop=(0, 0.5625, 0, 0.5625), alpha=alpha)

    def class_circle(self, cls, x, y, size, desat=False, alpha=1.0):
        self.blit('UI-Classes-Circles', x, y, size, size, crop=CLASS_TC[cls], desat=desat, alpha=alpha)

    def save(self, path, bg=(26, 26, 32)):
        out = Image.new('RGBA', self.im.size, bg + (255,))
        out.alpha_composite(self.im)
        out.convert('RGB').save(path)


# ------------------------------------------------------------------ the frame
def frame(c, plate_text):
    x, y = OX, OY
    c.backdrop(x, y, FW, FH, 'TutorialFrameBackground', 32, 'UI-Achievement-WoodBorder', 64, 16)
    corner = 'UI-Achievement-WoodBorder-Corner'
    c.blit(corner, x + 4, y + 2, 64, 64)
    c.blit(corner, x + FW - 4 - 64, y + 2, 64, 64, flip_x=True)
    c.blit(corner, x + 4, y + FH - 3 - 64, 64, 64, flip_y=True)
    c.blit(corner, x + FW - 4 - 64, y + FH - 3 - 64, 64, 64, flip_x=True, flip_y=True)

    # the carved header and its plate, at AchievementFrameHeader's own offsets
    c.blit('UI-Achievement-Header', x + 26, y + 39 - 106, 512, 106, crop=(0, 1, 0, 0.4140625))
    c.blit('UI-Achievement-Header', x + 26 + 512, y + 45 - 100, 215, 100, crop=(0, 0.419921875, 0.4140625, 0.8046875))
    px = x + 26 + 363 + 20
    c.blit('UI-Achievement-Header', px - 96.5, y + 23 - 39, 193, 39,
           crop=(0.419921875, 0.6796875, 0.4140625, 0.56640625))
    c.text('Arena Master', px, y - 22, 12, GOLD, anchor='mm')
    c.text(plate_text, px, y + 3, 12, WHITE, anchor='mm')


def finish(c, path):
    """UIPanelCloseButton sits above the board (frame level + 5)."""
    c.blit('UI-Panel-MinimizeButton-Up', OX + FW - 8 - 32, OY + 8, 32, 32)
    c.save(path)


AX, AY, AW, AH = 196, 19, 553, 461       # the board, beside the list


def board(c):
    x, y = OX + AX, OY + AY
    c.blit('UI-Achievement-AchievementBackground', x, y, AW, AH, tint=(0.41, 0.31, 0.19))
    c.gradient(x, y, AW, 10, True, 0.5, 0)
    c.gradient(x, y, 10, AH, False, 0.5, 0)


def dropdown(c, mode_icon, label, open_items=None):
    """UIDropDownMenuTemplate, its own art, with the stock left icon slot used."""
    x, y = OX + 4, OY + 40
    mw = 136
    tex = 'CharacterCreate-LabelFrame'
    c.text('Arena type', OX + 24, OY + 30, 10, GOLD, anchor='lm')
    c.blit(tex, x, y - 17, 25, 64, crop=(0, 0.1953125, 0, 1))
    c.blit(tex, x + 25, y - 17, mw, 64, crop=(0.1953125, 0.8046875, 0, 1))
    c.blit(tex, x + 25 + mw, y - 17, 25, 64, crop=(0.8046875, 1, 0, 1))
    c.blit(mode_icon, x + 26, y + 6, 15, 15, crop=(0.07, 0.93, 0.07, 0.93))
    c.text(label, x + 46, y + 14, 12, WHITE, anchor='lm')
    c.blit('UI-ChatIcon-ScrollDown-Up', x + 50 + mw - 40, y + 1, 24, 24)
    return x, y, mw


def drop_list(c, items, checked, hover):
    """DropDownList1: the dialog box's dark backdrop and border, 16-high buttons,
    the check on the left and the icon on the right as 3.3.5 draws them."""
    x, y = OX + 20, OY + 70
    w = 196
    rows = len(items)
    h = 15 * 2 + rows * 16
    c.backdrop(x, y, w, h, 'UI-DialogBox-Background-Dark', 32, 'UI-DialogBox-Border', 32, 11)
    for i, it in enumerate(items):
        ry = y + 15 + i * 16
        if it is None:
            continue
        name, icon, title = it
        if title:
            c.text(name, x + 16, ry + 8, 10, GOLD, anchor='lm')
            continue
        if name == hover:
            c.blit('UI-QuestTitleHighlight', x + 12, ry, w - 24, 16, add=True, alpha=0.9)
        if name == checked:
            c.blit('UI-CheckBox-Check', x + 10, ry - 4, 24, 24)
        c.text(name, x + 32, ry + 8, 11, WHITE, anchor='lm')
        if icon:
            c.blit(icon, x + w - 34, ry, 16, 16, crop=(0.07, 0.93, 0.07, 0.93))


def category(c, i, label, count=None, selected=False, top=78):
    x, y = OX + 21, OY + top + (i - 1) * 25
    c.blit('UI-Achievement-Category-Background', x, y, 158, 32, crop=(0, 0.6640625, 0, 1))
    if selected:
        c.blit('UI-Achievement-Category-Background', x, y, 158, 32, crop=(0, 0.6640625, 0, 1), add=True, alpha=0.6)
    c.text(label, x + 14, y + 13, 12, WHITE if selected else GOLD, anchor='lm')
    if count is not None:
        c.text(str(count), x + 158 - 14, y + 13, 10, WHITE, anchor='rm')


def poster(c, banner, banner_cy, icon, title, subtitle):
    """The mode's sheet: a parchment with the arena's own art across the top."""
    x, y = OX + AX + 12, OY + AY + 12
    w, h = AW - 24, AH - 12 - 52
    c.fill(x + 4, y + 5, w, h, (0, 0, 0, 110))
    c.blit('UI-Achievement-Parchment', x, y, w, h, crop=(0.03, 0.97, 0.02, 0.98))
    bx, by, bw, bh = x + 12, y + 12, w - 24, 118
    c.fill(bx - 1, by - 1, bw + 2, bh + 2, (20, 12, 4, 255))
    c.blit(loadscreen(banner, bw, bh, banner_cy), bx, by, bw, bh)
    c.gradient(bx, by + bh - 40, bw, 40, True, 0, 0.55)
    c.icon_ring(icon, x + 14, y + 98, 72)
    c.text(title, x + 94, y + 150, 24, BLOOD, MORPH, shadow=None)
    c.text(subtitle, x + 95, y + 172, 11, INK, shadow=None)
    c.blit('UI-HorizontalBreak', x + 14, y + 186, w - 28, 14, alpha=0.8)
    return x, y, w, h


def bottom_bar(c, note, buttons):
    y = OY + AY + AH - 34
    c.text(note, OX + AX + 16, y + 11, 10, GOLD, anchor='lm')
    bx = OX + AX + AW - 14
    for label, w, disabled in reversed(buttons):
        bx -= w
        c.panel_button(label, bx, y, w, 22, disabled)
        bx -= 6


def facts(c, x, y, w, items):
    """Three headed facts across the sheet."""
    cw = w / len(items)
    for i, (head, val) in enumerate(items):
        cx = x + i * cw + cw / 2
        c.text(head, cx, y, 14, (0, 0, 0), MORPH, shadow=None, anchor='mm')
        c.text(val, cx, y + 18, 11, INK, shadow=None, anchor='mm')


def lineup(c, x, y, w, people, ring=44):
    step = w / len(people)
    for i, (cls, name, sub, own) in enumerate(people):
        cx = x + i * step + step / 2
        inner = ring * 0.75
        c.class_circle(cls, cx - inner / 2, y + (ring - inner) / 2, inner, desat=not own, alpha=1 if own else 0.75)
        c.blit('UI-Achievement-IconFrame', cx - ring / 2, y, ring, ring, crop=(0, 0.5625, 0, 0.5625))
        c.text(name, cx, y + ring + 6, 10, INK if own else (95, 80, 60), shadow=None, anchor='mm')
        c.text(sub, cx, y + ring + 18, 9, INK if own else (110, 95, 75), shadow=None, anchor='mm')


ICON = {
    'Rated': 'Achievement_Arena_3v3_7',
    'Skirmish': 'Achievement_Arena_2v2_1',
    'Multi-team': 'Achievement_Arena_5v5_7',
    'Buffle Shuffle': 'INV_Misc_Dice_01',
    'All-Star': 'Achievement_FeatsOfStrength_Gladiator_07',
    'Replays': 'INV_Misc_Spyglass_02',
}
MODES = ['Rated', 'Skirmish', 'Multi-team', 'Buffle Shuffle', 'All-Star', 'Replays']


# ------------------------------------------------------------------ the four sheets
def sheet_skirmish(path):
    c = Canvas(CW, CH)
    frame(c, '3v3 Skirmish')
    board(c)
    dropdown(c, ICON['Skirmish'], 'Skirmish')
    for i, b in enumerate(['2v2', '3v3', '4v4', '5v5'], 1):
        category(c, i, b + ' Skirmish', selected=(b == '3v3'))
    x, y, w, h = poster(c, 'LoadScreenNagrandArenaBattlegrounds', 0.42, ICON['Skirmish'], '3v3 Skirmish',
                        'Practice match  -  no rating at stake')
    c.para('Fight for honor, not rating. Queue alone or bring up to two friends; the queue fills any '
           'empty places on your team. A loss costs you nothing.', x + 24, y + 210, w - 48, 12, INK, leading=17)
    facts(c, x + 10, y + 262, w - 20, [('Team', '3 players'), ('Arena', 'Random'), ('Reward', 'Honor')])
    c.text('Arenas in rotation', x + w / 2, y + 306, 14, (0, 0, 0), MORPH, shadow=None, anchor='mm')
    arenas = [('LoadScreenNagrandArenaBattlegrounds', 'Nagrand Arena'), ('LoadScreenBladesEdgeArena', "Blade's Edge"),
              ('LoadScreenRuinsofLordaeronBattlegrounds', 'Lordaeron'), ('LoadScreenDalaranSewersArena', 'Dalaran Sewers'),
              ('LoadScreenOrgrimmarArena', 'Ring of Valor')]
    tw, th, gap = 76, 50, 18
    tx = x + (w - (5 * tw + 4 * gap)) / 2
    for k, (ls, name) in enumerate(arenas):
        ax = tx + k * (tw + gap)
        c.fill(ax - 1, y + 318, tw + 2, th + 2, (20, 12, 4, 255))
        c.blit(loadscreen(ls, tw, th, 0.5, 0.8), ax, y + 319, tw, th)
        c.text(name, ax + tw / 2, y + 381, 10, INK, shadow=None, anchor='mm')
    bottom_bar(c, 'You are not in a group.', [('Join as Group', 120, True), ('Join Battle', 120, False)])
    finish(c, path)


def sheet_rated_dropdown(path):
    c = Canvas(CW, CH)
    frame(c, '3v3 Rated')
    board(c)
    for i, (b, r) in enumerate([('2v2', 1820), ('3v3', 1650), ('5v5', '-')], 1):
        category(c, i, b + ' Team', r, selected=(b == '3v3'))
    x, y, w, h = poster(c, 'LoadScreenBladesEdgeArena', 0.45, ICON['Rated'], 'The Drunken Monks',
                        '3v3 arena team  -  rating 1650  -  14 played this week')
    c.para('Your team\'s rating rises and falls with every match. Queue with your team: any three of '
           'its members can fight.', x + 24, y + 210, w - 48, 12, INK, leading=17)
    c.text('Roster', x + w / 2, y + 268, 15, (0, 0, 0), MORPH, shadow=None, anchor='mm')
    lineup(c, x + 60, y + 284, w - 120, [(11, 'Drunks', '1702', True), (7, 'Claude', '1688', True),
                                          (1, 'Elgrom', '1650', True), (4, 'Tijuana', '1611', True)])
    bottom_bar(c, 'Your group: Drunks, Claude, Elgrom', [('Join Battle', 120, False)])
    # the menu, open, over everything
    dropdown(c, ICON['Rated'], 'Rated')
    items = [('Arena type', None, True)] + [(m, ICON[m], False) for m in MODES[:5]] + [None, ('Replays', ICON['Replays'], False)]
    drop_list(c, items, 'Rated', 'All-Star')
    finish(c, path)


def sheet_allstar(path):
    c = Canvas(CW, CH)
    frame(c, '4v4 All-Star')
    board(c)
    dropdown(c, ICON['All-Star'], 'All-Star')
    for i in range(1, 6):
        category(c, i, '%dv%d All-Star' % (i, i), selected=(i == 4))
    x, y, w, h = poster(c, 'LoadScreenOrgrimmarArena', 0.5, ICON['All-Star'], '4v4 All-Star',
                        'Nine lives, one class each')
    c.para('Each death brings you back where you fell as your next character: their gear, talents, '
           'keybindings and macros. A class you have no character for comes in its standard kit - '
           'the tournament spec and gear. The last team with lives left wins.', x + 24, y + 206, w - 48, 12,
           INK, leading=16)
    c.text('Your lineup', x + w / 2, y + 268, 15, (0, 0, 0), MORPH, shadow=None, anchor='mm')
    c.text('shuffled every match', x + w / 2, y + 283, 9, INK, shadow=None, anchor='mm')
    people = [(1, 'Elgrom', 'Warrior', True), (7, 'Claude', 'Shaman', True), (11, 'Drunks', 'Druid', True),
              (4, 'Tijuana', 'Rogue', True), (8, 'Mage', 'Standard', True), (5, 'Priest', 'Standard', True),
              (9, 'Warlock', 'Standard', True), (3, 'Hunter', 'Standard', True),
              (2, 'Paladin', 'Standard', True)]
    lineup(c, x + 8, y + 293, w - 16, people, ring=42)
    bottom_bar(c, 'Your group: you and Claude', [('Join as Group', 120, False), ('Join Battle', 120, False)])
    finish(c, path)


REPLAYS = [
    ('LoadScreenNagrandArenaBattlegrounds', 'Nagrand Arena', '3v3 Rated  -  Match 4821', '2 hours ago  -  watched 12 times', True,
     ('Drunken Monks', [11, 7, 1], '1820  +14'), ('Kill Command', [3, 9, 4], '1795  -14')),
    ('LoadScreenDalaranSewersArena', 'Dalaran Sewers', '2v2 Skirmish  -  Match 4820', '3 hours ago  -  watched 2 times', False,
     ('Gold Team', [4, 8], ''), ('Green Team', [2, 5], '')),
    ('LoadScreenOrgrimmarArena', 'Ring of Valor', '5v5 Rated  -  Match 4817', 'yesterday  -  watched 31 times', True,
     ('Old Gods', [11, 4, 9, 3, 1], '1702  +11'), ('Sunwell Rejects', [1, 2, 5, 7, 8], '1690  -11')),
    ('LoadScreenBladesEdgeArena', "Blade's Edge Arena", '3v3 Skirmish  -  Match 4815', 'yesterday  -  watched 4 times', False,
     ('Green Team', [9, 5, 1], ''), ('Gold Team', [4, 4, 11], '')),
    ('LoadScreenRuinsofLordaeronBattlegrounds', 'Ruins of Lordaeron', '2v2 Rated  -  Match 4811', '2 days ago  -  watched 7 times', False,
     ('Two Dots', [4, 5], '1544  +16'), ('Bubble Trouble', [2, 8], '1530  -16')),
    ('LoadScreenNagrandArenaBattlegrounds', 'Nagrand Arena', '3v3 Rated  -  Match 4809', '2 days ago  -  watched 9 times', False,
     ('Kill Command', [3, 9, 4], '1809  +12'), ('Drunken Monks', [11, 7, 1], '1806  -12')),
]


def replay_card(c, x, y, w, h, r, selected):
    banner, arena, line1, line2, fav, won, lost = r
    c.fill(x + 4, y + 5, w, h, (0, 0, 0, 110))
    c.blit('UI-Achievement-Parchment-Horizontal', x, y, w, h, crop=(0.02, 0.98, 0.04, 0.96))
    if selected:
        c.blit('UI-Achievement-Parchment-Highlight', x, y, w, h, add=True, alpha=0.9)
    c.fill(x + 9, y + 9, 78, 59, (20, 12, 4, 255))
    c.blit(loadscreen(banner, 76, 57, 0.5, 0.8), x + 10, y + 10, 76, 57)
    c.text(arena, x + 95, y + 20, 15, (0, 0, 0), MORPH, shadow=None)
    c.text(line1, x + 95, y + 38, 10, INK, shadow=None)
    c.text(line2, x + 95, y + 52, 9, INK, shadow=None)
    c.blit('UI-RaidTargetingIcon_1', x + w - 25, y + 8, 16, 16, desat=not fav, alpha=1 if fav else 0.35)
    c.fill(x + 10, y + 76, w - 20, 1, INK + (70,))
    for k, (team, classes, rating) in enumerate((won, lost)):
        ry = y + 91 + k * 21
        if k == 0:
            c.blit('ReadyCheck-Ready', x + 9, ry - 7, 14, 14)
        for j, cls in enumerate(classes):
            c.class_circle(cls, x + 26 + j * 16, ry - 7.5, 15)
        c.text(team, x + 26 + len(classes) * 16 + 5, ry, 11, INK, shadow=None)
        if rating:
            c.text(rating, x + w - 10, ry, 10, WIN if k == 0 else BLOOD, shadow=None, anchor='rm')


def sheet_replays(path):
    c = Canvas(CW, CH)
    frame(c, 'Replays')
    board(c)
    dropdown(c, ICON['Replays'], 'Replays')
    category(c, 1, 'Recent', 10, selected=True)
    category(c, 2, 'Favorites', 3)
    # watch a match by its number
    lx, ly = OX + 24, OY + 150
    c.text('Match number', lx, ly, 10, GOLD, anchor='lm')
    ib = 'Common-Input-Border'
    c.blit(ib, lx, ly + 10, 8, 20, crop=(0, 0.0625, 0, 0.625))
    c.blit(ib, lx + 8, ly + 10, 72, 20, crop=(0.0625, 0.9375, 0, 0.625))
    c.blit(ib, lx + 80, ly + 10, 8, 20, crop=(0.9375, 1, 0, 0.625))
    c.text('4807', lx + 8, ly + 20, 12, WHITE, anchor='lm')
    c.panel_button('Watch', lx + 94, ly + 9, 62, 22)
    # the rule, where the list ends
    c.fill(OX + 30, OY + 400, 140, 1, (255, 209, 0, 60))
    c.blit('UI-Dialog-Icon-AlertNew', OX + 24, OY + 410, 18, 18)
    c.para('Replays are only recorded when both teams are fully human.', OX + 46, OY + 410, 128, 10, GOLD,
           shadow=(0, 0, 0), leading=13)
    c.para('Bot-filled matches are never saved.', OX + 46, OY + 450, 128, 10, (200, 200, 200),
           shadow=(0, 0, 0), leading=13)
    # the cards
    cw, chh = 262, 128
    for i, r in enumerate(REPLAYS):
        x = OX + AX + 12 + (i % 2) * (cw + 5)
        y = OY + AY + 12 + (i // 2) * (chh + 7)
        replay_card(c, x, y, cw, chh, r, selected=(i == 0))
    # paging and the selected match's buttons
    y = OY + AY + AH - 34
    c.blit('UI-SpellbookIcon-PrevPage-Up', OX + AX + 12, y - 4, 30, 30)
    c.text('Page 1 of 2', OX + AX + 80, y + 11, 12, GOLD, anchor='mm')
    c.blit('UI-SpellbookIcon-NextPage-Up', OX + AX + 118, y - 4, 30, 30)
    bx = OX + AX + AW - 14
    c.panel_button('Watch', bx - 110, y, 110, 22)
    c.panel_button('Unfavorite', bx - 110 - 6 - 110, y, 110, 22)
    finish(c, path)


if __name__ == '__main__':
    os.makedirs(OUT, exist_ok=True)
    sheet_skirmish(os.path.join(OUT, '1_skirmish.png'))
    sheet_rated_dropdown(os.path.join(OUT, '2_rated_menu_open.png'))
    sheet_allstar(os.path.join(OUT, '3_allstar.png'))
    sheet_replays(os.path.join(OUT, '4_replays.png'))
    print('ok')
