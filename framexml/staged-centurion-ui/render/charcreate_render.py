#!/usr/bin/env python3
"""Render the Centurion character create screen at a given resolution from the
client's own art, so the layout can be judged before a patch push.

Glue screens are always 768 UI units tall; the width follows the aspect ratio
(1920x1080 -> 1365.33 x 768). Coordinates here are UI units, y growing DOWN
from the screen's top-left. Geometry follows CharacterCreate.xml/.lua as
published in patch-enUS-6, with the layout dict able to override the
Centurion pieces (name boxes, tournament box, challenges panel). The
'live' layout is 1.00017; 'old_1.00016' is the one that ran into the class
box at 16:9 and narrower.

usage: charcreate_render.py [width height [layout ...]]
       -> cc_<layout>_<w>x<h>.png here, plus the overlaps it found.
Check at least 1920x1080 and 1920x1200 (16:10 is the tightest common shape).

The 3D scene cannot be drawn; a dark backdrop and a model silhouette stand in.
Art: render/art (pulled with mpqread.py + blp.py); fonts next to this file.
"""
import os
import sys
from PIL import Image, ImageDraw, ImageFont, ImageFilter

HERE = os.path.dirname(os.path.abspath(__file__))
ART = os.path.join(HERE, 'art')
FRIZ = os.path.join(HERE, 'FRIZQT__.TTF')
ARIALN = os.path.join(HERE, 'ARIALN.TTF')
MORPH = os.path.join(HERE, 'MORPHEUS.ttf')

GOLD = (255, 199, 0)
WHITE = (255, 255, 255)
GREY = (128, 128, 128)

_art = {}


def art(name):
    if name not in _art:
        _art[name] = Image.open(os.path.join(ART, name + '.png')).convert('RGBA')
    return _art[name]


class Screen:
    def __init__(self, px_w, px_h):
        self.S = px_h / 768.0
        self.W = px_w / self.S
        self.H = 768.0
        self.im = Image.new('RGBA', (px_w, px_h), (0, 0, 0, 255))
        self.boxes = []          # (name, x0, y0, x1, y1) for the overlap report
        self._bg()

    # -- primitives ----------------------------------------------------------
    def _bg(self):
        w, h = self.im.size
        g = Image.new('RGBA', (w, h))
        d = ImageDraw.Draw(g)
        for y in range(h):
            t = y / h
            d.line([(0, y), (w, y)], fill=(int(30 + 25 * t), int(34 + 20 * t), int(48 + 10 * t), 255))
        self.im.alpha_composite(g)
        # model silhouette, centred, roughly where the 3D character stands
        sil = Image.new('RGBA', (w, h), (0, 0, 0, 0))
        d = ImageDraw.Draw(sil)
        cx = w / 2
        s = self.S
        d.ellipse([cx - 40 * s, 230 * s, cx + 40 * s, 310 * s], fill=(90, 90, 100, 150))
        d.rounded_rectangle([cx - 75 * s, 300 * s, cx + 75 * s, 520 * s], 30 * s, fill=(90, 90, 100, 150))
        d.rectangle([cx - 60 * s, 510 * s, cx - 15 * s, 650 * s], fill=(90, 90, 100, 150))
        d.rectangle([cx + 15 * s, 510 * s, cx + 60 * s, 650 * s], fill=(90, 90, 100, 150))
        sil = sil.filter(ImageFilter.GaussianBlur(4))
        self.im.alpha_composite(sil)

    def P(self, v):
        return int(round(v * self.S))

    def blit(self, img, x, y, w, h, crop=None, tint=None, flipv=False, alpha=1.0):
        if crop:
            l, t, r, b = crop
            if t > b:
                t, b = b, t
                flipv = not flipv
            img = img.crop((int(round(l * img.width)), int(round(t * img.height)),
                            int(round(r * img.width)), int(round(b * img.height))))
        if flipv:
            img = img.transpose(Image.FLIP_TOP_BOTTOM)
        pw, ph = max(1, self.P(x + w) - self.P(x)), max(1, self.P(y + h) - self.P(y))
        img = img.resize((pw, ph), Image.LANCZOS)
        if tint or alpha != 1.0:
            r, g, b, a = img.split()
            if tint:
                r = r.point(lambda v: int(v * tint[0]))
                g = g.point(lambda v: int(v * tint[1]))
                b = b.point(lambda v: int(v * tint[2]))
            if alpha != 1.0:
                a = a.point(lambda v: int(v * alpha))
            img = Image.merge('RGBA', (r, g, b, a))
        self.im.alpha_composite(img, (self.P(x), self.P(y)))

    def font(self, path, height):
        return ImageFont.truetype(path, max(1, int(round(height * self.S))))

    def text_width(self, s, height, path=FRIZ):
        d = ImageDraw.Draw(self.im)
        return d.textlength(s, font=self.font(path, height)) / self.S

    def text(self, s, x, y, height, colour, anchor='lm', path=FRIZ, outline=True, shadow=1):
        d = ImageDraw.Draw(self.im)
        f = self.font(path, height)
        if shadow:
            d.text((self.P(x) + shadow * 2, self.P(y) + shadow * 2), s, font=f, fill=(0, 0, 0, 255), anchor=anchor,
                   stroke_width=1 if outline else 0, stroke_fill=(0, 0, 0, 255))
        d.text((self.P(x), self.P(y)), s, font=f, fill=colour, anchor=anchor,
               stroke_width=1 if outline else 0, stroke_fill=(0, 0, 0, 255))

    def wrap(self, s, x, y, width, height, colour, spacing=2, path=FRIZ, max_lines=99):
        words = s.split(' ')
        line = ''
        lines = []
        for w in words:
            t = (line + ' ' + w).strip()
            if self.text_width(t, height, path) > width and line:
                lines.append(line)
                line = w
            else:
                line = t
        if line:
            lines.append(line)
        for i, l in enumerate(lines[:max_lines]):
            self.text(l, x, y + i * (height + spacing) + height / 2, height, colour, anchor='lm', path=path)
        return len(lines[:max_lines]) * (height + spacing)

    def backdrop(self, x, y, w, h, edge, edge_size, bg, insets, bg_colour, border_colour=(1, 1, 1), bg_alpha=1.0):
        il, ir, it, ib = insets
        self.blit(art(bg), x + il, y + it, w - il - ir, h - it - ib, tint=bg_colour, alpha=bg_alpha)
        e = art(edge)
        pw = e.width // 8
        pieces = [e.crop((i * pw, 0, (i + 1) * pw, e.height)) for i in range(8)]
        es = edge_size
        left, right = pieces[0], pieces[1]
        top = pieces[2].rotate(-90, expand=True)
        bottom = pieces[3].rotate(-90, expand=True)
        self.blit(left, x, y + es, es, h - 2 * es, tint=border_colour)
        self.blit(right, x + w - es, y + es, es, h - 2 * es, tint=border_colour)
        self.blit(top, x + es, y, w - 2 * es, es, tint=border_colour)
        self.blit(bottom, x + es, y + h - es, w - 2 * es, es, tint=border_colour)
        self.blit(pieces[4], x, y, es, es, tint=border_colour)
        self.blit(pieces[5], x + w - es, y, es, es, tint=border_colour)
        self.blit(pieces[6], x, y + h - es, es, es, tint=border_colour)
        self.blit(pieces[7], x + w - es, y + h - es, es, es, tint=border_colour)

    def box(self, name, x, y, w, h):
        self.boxes.append((name, x, y, x + w, y + h))

    # -- widgets -------------------------------------------------------------
    def glue_button(self, label, cx, top, w, h, small=True, name=None):
        x = cx - w / 2
        self.blit(art('Glue-Panel-Button-Up'), x, top, w, h, crop=(0, 0, 0.578125, 0.75))
        self.text(label, cx, top + h / 2 - 3, 12 if small else 15, GOLD, anchor='mm')
        self.box(name or label, x, top, w, h)

    def checkbox(self, label, x, y, size, checked=False, enabled=True, text_h=12, name=None, hit_right=0):
        self.blit(art('UI-CheckBox-Up'), x, y, size, size)
        if checked:
            self.blit(art('UI-CheckBox-Check'), x, y, size, size)
        self.text(label, x + size, y + size / 2 - 1, text_h, GOLD if enabled else GREY, anchor='lm')
        tw = self.text_width(label, text_h)
        self.box(name or label, x, y, size + tw, size)
        return tw

    def edit_box(self, label, x, y, w, h, value='', name=None):
        self.backdrop(x, y, w, h, 'Glue-Tooltip-Border', 16, 'UI-Tooltip-Background', (10, 5, 4, 9),
                      (0.09, 0.09, 0.19), (0.5, 0.5, 0.5))
        # label: 256x64 box, BOTTOM at the edit box TOP y=-23 -> centre 32 above that bottom
        self.text(label, x + w / 2, y + 23 - 32, 18, GOLD, anchor='mm')
        if value:
            self.text(value, x + 15, y + h / 2 - 2, 16, WHITE, anchor='lm', path=ARIALN, outline=False, shadow=0)
        self.box(name or label, x, y, w, h)
        self.box((name or label) + ' label', x + w / 2 - self.text_width(label, 18) / 2, y + 23 - 32 - 9,
                 self.text_width(label, 18), 18)


def draw(layout, px_w=1920, px_h=1080):
    sc = Screen(px_w, px_h)
    W, H = sc.W, sc.H

    # ---------------- left: configuration frame (TOPLEFT 9,-5, 256x758) ----------------
    cx0, cy0 = 9, 5
    ccx = cx0 + 128
    sc.blit(art('UI-Tooltip-Background'), cx0 + 10, cy0 + 8, 236, 758 - 16 - 2, tint=(0.09, 0.09, 0.19))
    sc.blit(art('UI-CharacterCreate-OuterBorder'), cx0, cy0, 256, 256, crop=(0, 1, 1, 0))
    sc.blit(art('UI-CharacterCreate-OuterBorder'), cx0, cy0 + 256, 256, 246, crop=(0, 0, 1, 0.9609375))
    sc.blit(art('UI-CharacterCreate-OuterBorder'), cx0, cy0 + 502, 256, 256)
    sc.box('config frame', cx0, cy0, 256, 758)

    races = [('Human', 0), ('Dwarf', 1), ('Night Elf', 3), ('Gnome', 2),
             ('Orc', 'o'), ('Undead', 's'), ('Tauren', 't'), ('Troll', 'r')]
    tc = {0: (0, 0.125, 0, 0.25), 1: (0.125, 0.25, 0, 0.25), 2: (0.25, 0.375, 0, 0.25), 3: (0.375, 0.5, 0, 0.25),
          't': (0, 0.125, 0.25, 0.5), 's': (0.125, 0.25, 0.25, 0.5), 'r': (0.25, 0.375, 0.25, 0.5), 'o': (0.375, 0.5, 0.25, 0.5)}
    for i, (rname, key) in enumerate(races):
        col = 0 if i < 4 else 1
        row = i % 4
        bx = ccx + (-50 if col == 0 else 50) - 19
        by = cy0 + 61 + row * 59
        sc.blit(art('UI-CharacterCreate-IconShadow'), bx - 10, by - 10, 58, 58, alpha=0.85)
        l, r, t, b = tc[key]
        sc.blit(art('UI-CharacterCreate-Races'), bx, by, 38, 38, crop=(l, t, r, b))
        sc.blit(art('UI-CharacterCreate-Highlights'), bx, by, 38, 38, crop=(0, 0.5, 0.5, 1.0))
        if i == 2:
            sc.blit(art('UI-CharacterCreate-Highlights'), bx, by, 38, 38, crop=(0.5, 0, 1, 0.5))
            sc.text(rname, bx + 19, by - 2 - 6, 12, GOLD, anchor='mm')

    # gender
    sc.text('Male', ccx, cy0 + 388 - 8, 15, GOLD, anchor='mm')
    for j, dx in enumerate((-20, 20)):
        bx = ccx + dx - 19
        by = cy0 + 392
        sc.blit(art('UI-CharacterCreate-IconShadow'), bx - 10, by - 10, 58, 58, alpha=0.85)
        sc.blit(art('UI-CharacterCreate-Gender'), bx, by, 38, 38, crop=(0.5 * j, 0, 0.5 * j + 0.5, 1))
        sc.blit(art('UI-CharacterCreate-Highlights'), bx, by, 38, 38, crop=(0, 0.5, 0.5, 1.0))
    # class
    sc.text('Warrior', ccx, cy0 + 456 - 8, 15, GOLD, anchor='mm')
    ctc = [(0, 0.25, 0, 0.25), (0, 0.25, 0.5, 0.75), (0, 0.25, 0.25, 0.5), (0.49609375, 0.7421875, 0, 0.25),
           (0.49609375, 0.7421875, 0.25, 0.5), None, (0.25, 0.49609375, 0.25, 0.5), (0.25, 0.49609375, 0, 0.25),
           (0.7421875, 0.98828125, 0.25, 0.5), (0.7421875, 0.98828125, 0, 0.25)]
    for i in range(10):
        if ctc[i] is None:
            continue
        col, row = i % 5, i // 5
        bx = ccx - 90 - 19 + col * 44
        by = cy0 + 463 + row * 44
        sc.blit(art('UI-CharacterCreate-IconShadow'), bx - 10, by - 10, 58, 58, alpha=0.85)
        l, r, t, b = ctc[i]
        sc.blit(art('UI-CharacterCreate-Classes'), bx, by, 38, 38, crop=(l, t, r, b))
        sc.blit(art('UI-CharacterCreate-Highlights'), bx, by, 38, 38, crop=(0, 0.5, 0.5, 1.0))

    # randomize (BOTTOM 0,10) + customization frames stacked above it
    rw = sc.text_width('Randomize', 12) + 50
    rtop = cy0 + 758 - 10 - 30
    sc.glue_button('Randomize', ccx, rtop, rw, 30, name='appearance Randomize')
    labels = ['Skin Color', 'Face', 'Hair Style', 'Hair Color', 'Facial Hair']
    for k in range(5):
        fx = ccx - 6 - 115
        fy = rtop - 3 - 32 * (5 - k)
        sc.blit(art('CharacterCreate-LabelFrame'), fx - 5, fy - 16, 25, 64, crop=(0, 0, 0.1953125, 1))
        sc.blit(art('CharacterCreate-LabelFrame'), fx + 20, fy - 16, 230 - 44 - 25 - 20 + 5 - 5, 64,
                crop=(0.1953125, 0, 0.8046875, 1))
        sc.blit(art('CharacterCreate-LabelFrame'), fx + 230 - 44 - 25, fy - 16, 25, 64, crop=(0.8046875, 0, 1, 1))
        sc.text(labels[k], fx + (230 - 44) / 2, fy + 16 - 2, 12, WHITE, anchor='mm')
        sc.blit(art('Glue-RightArrow-Button-Up'), fx + 230 - 32, fy, 32, 32)
        sc.blit(art('Glue-LeftArrow-Button-Up'), fx + 230 - 32 - 32 + 3, fy, 32, 32)

    # ---------------- right column ----------------
    logo_x = W - 9 - 256
    sc.blit(art('Glues-WoW-WotLKLogo'), logo_x, 0, 256, 128)
    sc.box('logo', logo_x, 0, 256, 128)

    def info(title, x, y, icon, icon_crop, lines):
        sc.blit(art('UI-Tooltip-Background'), x + 12, y + 20, 267 - 15, 287 - 26, tint=(0.09, 0.09, 0.19))
        sc.blit(art(icon), x + 4, y + 4, 48, 48, crop=icon_crop)
        sc.blit(art('UI-CharacterCreate-InfoBox'), x, y, 267, 287,
                crop=(0.005859375, 0.005859375, 0.52734375, 0.56640625))
        sc.text(title, x + 133.5 + 40 - 110, y + 10 + 11, 18, WHITE, anchor='lm', path=MORPH)
        ty = y + 55
        for l, c in lines:
            ty += sc.wrap(l, x + 20, ty, 215, 12, c) + 10

    rx = logo_x + 128 - 5 - 133.5
    ry = 128 - 5
    info('Night Elf', rx, ry, 'UI-CharacterCreate-RacesRound', (0.375, 0, 0.5, 0.25), [
        ('Shadowmeld: Activate to slip into the shadows, reducing the chance for enemies to detect your presence.', GOLD),
        ('Quickness: Increases your chance to dodge melee and ranged attacks by 2%.', GOLD),
        ('Wisp Spirit: Transform into a wisp upon death, increasing movement speed.', GOLD)])
    sc.box('race info', rx + 7, ry, 267 - 7, 287)
    cxp = rx + 133.5 - 5 - 133.5
    cyp = ry + 287 + 5
    info('Warrior', cxp, cyp, 'UI-Classes-Circles', (0, 0.25, 0, 0.25), [
        ('Role: Tank, Damage', GOLD),
        ('Warriors are the most powerful melee fighters, able to wear heavy armor and take a beating.', WHITE),
        ('Uses rage as a resource.', WHITE)])
    sc.box('class info', cxp + 7, cyp, 267 - 7, 287)
    ok_cx = cxp + 133.5 + 4
    sc.glue_button('Accept', ok_cx, cyp + 287 + 3, 160, 35, small=False)
    sc.glue_button('Back', ok_cx + 4, cyp + 287 + 3 + 35 - 3, 120, 30)

    # ---------------- bottom centre: Centurion pieces ----------------
    L = layout
    mid = W / 2
    name_x = mid + L['name_dx'] - 78
    name_y = H - L['name_y'] - 40
    sc.edit_box('First Name' if L['surname'] else 'Name', name_x, name_y, 156, 40, 'Elgrom', name='first name box')
    rw = sc.text_width('Randomize', 12) + 50
    sc.glue_button('Randomize', name_x + 78, name_y + 40 - 7, rw, 30, name='first name Randomize')
    if L['surname']:
        sx = name_x + 156 + 8
        sc.edit_box('Last Name', sx, name_y, 156, 40, 'Fernbloom', name='last name box')
        sc.glue_button('Randomize', sx + 78, name_y + 40 - 7, rw, 30, name='last name Randomize')

    # rotate arrows: BOTTOM (+-16, -5), 50x50
    for dx, flip in ((-16, True), (16, False)):
        img = art('UI-RotationRight-Big-Up')
        if flip:
            img = img.transpose(Image.FLIP_LEFT_RIGHT)
        sc.blit(img, mid + dx - 25, H + 5 - 50, 50, 50)
        sc.box('rotate ' + ('left' if flip else 'right'), mid + dx - 25 + 13, H + 5 - 50 + 10, 24, 27)

    L['options'](sc, name_x, name_y)

    return sc


CHALLENGES = ['Hardcore', 'Semi-Hardcore', 'Self Crafted', 'Iron Man', 'Poor/Normal Gear Only',
              'Slow XP', 'Very Slow XP', 'Quest XP Only']


def old_options(sc, name_x, name_y):
    """patch-enUS-6 1.00016: tournament box right of the last name box, challenges
    panel BOTTOMLEFT at the box's TOPLEFT +64 - touched the class box at 16:9."""
    tx = name_x + 156 + 8 + 156 - 2
    ty = name_y + 20 + 2 - 13
    sc.checkbox('Tournament character', tx, ty, 26, name='tournament checkbox')
    px, py = tx + 64, ty - 212
    sc.backdrop(px, py, 190, 212, 'UI-Tooltip-Border', 16, 'UI-Tooltip-Background', (4, 4, 4, 4),
                (0.09, 0.09, 0.19), (0.6, 0.6, 0.6), bg_alpha=0.85)
    sc.text('Challenges', px + 10, py + 8 + 8, 15, GOLD, anchor='lm')
    for i, c in enumerate(CHALLENGES):
        sc.checkbox(c, px + 6, py + 26 + i * 22, 24, checked=(i == 0), enabled=(i != 1), name='challenge ' + c)
    sc.box('challenges panel', px, py, 190, 212)


OLD = dict(surname=True, name_dx=-82, name_y=55, options=old_options)


def challenges_panel(sc, px, py, tournament_inside=False):
    """The panel at its top-left. Returns its height."""
    h = 212
    top_pad = 0
    if tournament_inside:
        top_pad = 36
        h += top_pad
    sc.backdrop(px, py, 190, h, 'UI-Tooltip-Border', 16, 'UI-Tooltip-Background', (4, 4, 4, 4),
                (0.09, 0.09, 0.19), (0.6, 0.6, 0.6), bg_alpha=0.85)
    if tournament_inside:
        sc.checkbox('Tournament character', px + 6, py + 6, 24, name='tournament checkbox')
        d = ImageDraw.Draw(sc.im)
        d.line([(sc.P(px + 10), sc.P(py + 36)), (sc.P(px + 180), sc.P(py + 36))], fill=(150, 150, 150, 160),
               width=max(1, sc.P(1)))
    sc.text('Challenges', px + 10, py + top_pad + 8 + 8, 15, GOLD, anchor='lm')
    for i, c in enumerate(CHALLENGES):
        sc.checkbox(c, px + 6, py + top_pad + 26 + i * 22, 24, checked=(i == 0), enabled=(i != 1),
                    name='challenge ' + c)
    sc.box('challenges panel', px, py, 190, h)
    return h


def class_frame_left(sc):
    logo_x = sc.W - 9 - 256
    return logo_x + 128 - 5 - 133.5 - 5, 128 - 5 + 287 + 5   # frame left, frame top


def nudge_options(sc, name_x, name_y):
    """A, live since 1.00017: same place, left edge lined up with the tournament
    box, 4 above it (BOTTOMLEFT at its TOPLEFT, 0, 4). Clear at 16:9; still
    touches the class box at 16:10 (~7 units) and covers it at 5:4."""
    tx = name_x + 156 + 8 + 156 - 2
    ty = name_y + 20 + 2 - 13
    sc.checkbox('Tournament character', tx, ty, 26, name='tournament checkbox')
    challenges_panel(sc, tx, ty - 4 - 212)


def pinned_options(sc, name_x, name_y):
    """B: challenges panel pinned to the left of the class box, tops level;
    tournament box stays right of the last name box."""
    tx = name_x + 156 + 8 + 156 - 2
    ty = name_y + 20 + 2 - 13
    sc.checkbox('Tournament character', tx, ty, 26, name='tournament checkbox')
    fl, ft = class_frame_left(sc)
    challenges_panel(sc, fl - 4 - 190, ft + 8)


def onepanel_options(sc, name_x, name_y):
    """C: one panel - tournament box on top, challenges under it - pinned left
    of the class box, tops level."""
    fl, ft = class_frame_left(sc)
    challenges_panel(sc, fl - 4 - 190, ft + 8, tournament_inside=True)


LIVE = dict(surname=True, name_dx=-82, name_y=55, options=nudge_options)

CANDIDATES = {
    'live': LIVE,
    'old_1.00016': OLD,
    'A_nudge': LIVE,
    'B_pinned': dict(surname=True, name_dx=-82, name_y=55, options=pinned_options),
    'C_onepanel': dict(surname=True, name_dx=-82, name_y=55, options=onepanel_options),
}


def overlaps(sc, ignore=()):
    out = []
    b = sc.boxes
    for i in range(len(b)):
        for j in range(i + 1, len(b)):
            n1, a0, a1, a2, a3 = b[i]
            n2, c0, c1, c2, c3 = b[j]
            if (n1, n2) in ignore or (n2, n1) in ignore:
                continue
            ix = min(a2, c2) - max(a0, c0)
            iy = min(a3, c3) - max(a1, c1)
            if ix > 0 and iy > 0:
                out.append((n1, n2, round(ix, 1), round(iy, 1)))
    return out


NOISE = ('challenge ', 'config frame', 'logo', 'Accept', 'Randomize')


def report(sc):
    res = []
    for o in overlaps(sc):
        a, b = o[0], o[1]
        if a.startswith('challenge ') and (b.startswith('challenge') or b == 'challenges panel'):
            continue
        if b.startswith('challenge ') and a == 'challenges panel':
            continue
        if 'Randomize' in a or 'Randomize' in b:
            if o[2] < 5 or o[3] < 8:
                continue
        if a in ('config frame', 'logo', 'Accept'):
            continue
        if b == 'tournament checkbox' and a == 'challenges panel':
            continue
        if a == 'last name box' and b == 'tournament checkbox' and o[2] <= 2:
            continue
        res.append(o)
    return res


if __name__ == '__main__':
    w, h = (int(v) for v in (sys.argv[1:3] if len(sys.argv) > 2 else (1920, 1080)))
    names = sys.argv[3:] or ['live']
    for n in names:
        sc = draw(CANDIDATES[n], w, h)
        sc.im.convert('RGB').save(os.path.join(HERE, 'cc_%s_%dx%d.png' % (n, w, h)))
        print(n, w, h, report(sc))
