#!/usr/bin/env python3
"""Render CenturionSquadFrames 1:1 from the client's own art, to judge it before a patch push.

Geometry is the module's (which is ArenaEnemyFrameTemplate's, from Blizzard_ArenaUI.xml in
patch-enUS-2); textures are the stock BLPs and FRIZQT__.TTF. UI units, y growing downward.

usage: squadframes_render.py <dir of extracted BLPs> <out.png> [numbers] [fives]
       the BLPs: UI-ArenaTargetingFrame.blp, UI-Classes-Circles.blp, UI-StatusBar.blp
       "numbers" draws the bar texts as with the party status-text option on
       "fives" draws a 5v5v5v5 (fifteen frames, at the module's 80%)
"""
import os
import sys
from PIL import Image, ImageDraw, ImageFont

HERE = os.path.dirname(os.path.abspath(__file__))
RENDER = os.path.join(HERE, "..", "..", "staged-centurion-ui", "render")
sys.path.insert(0, RENDER)
from blp import Blp

S = 2
FRAME_W, FRAME_H, FRAME_GAP, HEADER_H, COLUMN_GAP = 102, 32, 8, 16, 18
SCALE_BY_SQUAD_SIZE = {4: 0.9, 5: 0.8}
GOLD = (255, 209, 0)
WHITE = (255, 255, 255)
SQUADS = {0: ("Gold Team", (255, 199, 0)), 1: ("Green Team", (51, 224, 77)),
          2: ("Blue Team", (46, 168, 255)), 3: ("Purple Team", (184, 77, 255))}
CLASS_TCOORDS = {
    "WARRIOR": (0, 0.25, 0, 0.25), "MAGE": (0.25, 0.49609375, 0, 0.25),
    "ROGUE": (0.49609375, 0.7421875, 0, 0.25), "DRUID": (0.7421875, 0.98828125, 0, 0.25),
    "HUNTER": (0, 0.25, 0.25, 0.5), "SHAMAN": (0.25, 0.49609375, 0.25, 0.5),
    "PRIEST": (0.49609375, 0.7421875, 0.25, 0.5), "WARLOCK": (0.7421875, 0.98828125, 0.25, 0.5),
    "PALADIN": (0, 0.25, 0.5, 0.75), "DEATHKNIGHT": (0.25, 0.49609375, 0.5, 0.75),
}
POWER = {"mana": (0, 0, 255), "rage": (255, 0, 0), "energy": (255, 255, 0)}


def font(height):
    return ImageFont.truetype(os.path.join(RENDER, "FRIZQT__.TTF"), int(round(height * S)))


class Canvas:
    def __init__(self, w, h, art):
        self.im = Image.new("RGBA", (w * S, h * S), (34, 38, 33, 255))
        self.d = ImageDraw.Draw(self.im)
        self.art = art

    def blit(self, image, x, y, w, h, crop=None, tint=None, alpha=1.0, grey=False):
        if crop:
            l, r, t, b = crop
            image = image.crop((int(l * image.width), int(t * image.height),
                                int(r * image.width), int(b * image.height)))
        image = image.resize((max(1, int(w * S)), max(1, int(h * S))), Image.LANCZOS)
        if grey:
            g = image.convert("LA").convert("RGBA")
            g.putalpha(image.getchannel("A"))
            image = g
        if tint:
            r, gch, b, a = image.split()
            r = r.point(lambda v: v * tint[0] // 255)
            gch = gch.point(lambda v: v * tint[1] // 255)
            b = b.point(lambda v: v * tint[2] // 255)
            image = Image.merge("RGBA", (r, gch, b, a))
        if alpha < 1.0:
            a = image.getchannel("A").point(lambda v: int(v * alpha))
            image.putalpha(a)
        self.im.alpha_composite(image, (int(x * S), int(y * S)))

    def rect(self, x, y, w, h, rgba):
        layer = Image.new("RGBA", self.im.size, (0, 0, 0, 0))
        ImageDraw.Draw(layer).rectangle((x * S, y * S, (x + w) * S - 1, (y + h) * S - 1), fill=rgba)
        self.im.alpha_composite(layer)

    def text(self, s, x, y, height, colour, anchor="lt", outline=False):
        f = font(height)
        if outline:
            for dx, dy in ((-1, 0), (1, 0), (0, -1), (0, 1)):
                self.d.text((x * S + dx, y * S + dy), s, font=f, fill=(0, 0, 0, 255), anchor=anchor)
        else:
            self.d.text((x * S + 1, y * S + 1), s, font=f, fill=(0, 0, 0, 255), anchor=anchor)
        self.d.text((x * S, y * S), s, font=f, fill=colour, anchor=anchor)


def health_colour(pct):
    v = pct / 100.0
    if v > 0.5:
        return (int((1 - v) * 2 * 255), 255, 0)
    return (255, int(v * 2 * 255), 0)


def member(c, x, y, m, numbers):
    name, cls, state, hp, ptype, pw = m
    art = c.art
    c.blit(art["circles"], x + FRAME_W - 1 - 30, y + 4, 30, 30, crop=CLASS_TCOORDS[cls], grey=state in ("dead",))
    c.rect(x + 2, y + 10, 72, 17, (0, 0, 0, 128))
    if state == "alive":
        hcol, pcol = health_colour(hp), POWER[ptype]
    else:
        hcol = pcol = (128, 128, 128)
    if state == "dead":
        hp = pw = 0
    if hp:
        c.blit(art["bar"], x + 2, y + 12, 70 * hp / 100.0, 8, tint=hcol)
    if pw:
        c.blit(art["bar"], x + 2, y + 20, 70 * pw / 100.0, 8, tint=pcol)
    c.blit(art["frame"], x, y + 2, FRAME_W, FRAME_H, crop=(0, 0.796, 0, 0.5))
    c.text(name, x + 3, y + FRAME_H - 24, 10, GOLD, anchor="lb")
    label = {"dead": "Dead", "stealthed": "Stealthed"}.get(state)
    if label:
        c.text(label, x + 31, y + 16 - 3, 9, WHITE, anchor="mm", outline=True)
    elif numbers:
        c.text("%d%%" % hp, x + 31, y + 16 - 3, 9, WHITE, anchor="mm", outline=True)
        c.text("%d%%" % pw, x + 31, y + 16 + 6, 9, WHITE, anchor="mm", outline=True)


FIVES = [
    (0, [("Kaelthor", "WARRIOR", "alive", 87, "rage", 45), ("Mirelle", "MAGE", "dead", 0, "mana", 0),
         ("Thane", "ROGUE", "stealthed", 62, "energy", 50), ("Aldric", "PALADIN", "alive", 55, "mana", 62),
         ("Senna", "PRIEST", "alive", 92, "mana", 71)]),
    (2, [("Bryndis", "PRIEST", "alive", 64, "mana", 80), ("Oskar", "HUNTER", "alive", 100, "mana", 100),
         ("Vex", "WARLOCK", "alive", 41, "mana", 35), ("Rurik", "DEATHKNIGHT", "alive", 70, "rage", 20),
         ("Tamsin", "SHAMAN", "dead", 0, "mana", 0)]),
    (3, [("Sorrin", "DRUID", "alive", 23, "energy", 70), ("Halvard", "PALADIN", "dead", 0, "mana", 0),
         ("Ingrid", "SHAMAN", "alive", 76, "mana", 58), ("Corvin", "ROGUE", "alive", 100, "energy", 100),
         ("Ysolde", "MAGE", "alive", 33, "mana", 12)]),
]


def render(art, out, numbers, fives=False):
    # As a Green player sees a 3v3v3v3: Gold, Blue and Purple, right to left.
    squads = FIVES if fives else [
        (0, [("Kaelthor", "WARRIOR", "alive", 87, "rage", 45),
             ("Mirelle", "MAGE", "dead", 0, "mana", 0),
             ("Thane", "ROGUE", "stealthed", 62, "energy", 50)]),
        (2, [("Bryndis", "PRIEST", "alive", 64, "mana", 80),
             ("Oskar", "HUNTER", "alive", 100, "mana", 100),
             ("Vex", "WARLOCK", "alive", 41, "mana", 35)]),
        (3, [("Sorrin", "DRUID", "alive", 23, "energy", 70),
             ("Halvard", "PALADIN", "dead", 0, "mana", 0),
             ("Ingrid", "SHAMAN", "alive", 76, "mana", 58)]),
    ]
    margin = 14
    cols = len(squads)
    rows = max(len(m) for _, m in squads)
    w = cols * (FRAME_W + COLUMN_GAP) - COLUMN_GAP + 2 * margin
    h = HEADER_H + rows * (FRAME_H + FRAME_GAP) - FRAME_GAP + 2 * margin
    c = Canvas(w, h, art)
    right = w - margin
    for column, (squad, members) in enumerate(squads):
        gx = right - column * (FRAME_W + COLUMN_GAP) - FRAME_W
        gy = margin
        title, colour = SQUADS[squad]
        standing = sum(1 for m in members if m[2] != "dead")
        c.text(title, gx + 3, gy, 10, colour)
        c.text("%d/%d" % (standing, len(members)), gx + FRAME_W - 3, gy, 10, colour, anchor="rt")
        for i, m in enumerate(members):
            member(c, gx, gy + HEADER_H + i * (FRAME_H + FRAME_GAP), m, numbers)
    # The module scales the whole block by the largest squad; so does the picture.
    scale = SCALE_BY_SQUAD_SIZE.get(rows, 1.0)
    im = c.im if scale == 1.0 else c.im.resize((int(c.im.width * scale), int(c.im.height * scale)), Image.LANCZOS)
    im.save(out)
    print(out, im.size, "scale", scale, "UI units", (round(w * scale), round(h * scale)))


if __name__ == "__main__":
    src, out = sys.argv[1], sys.argv[2]
    art = {}
    for key, name in (("frame", "UI-ArenaTargetingFrame.blp"), ("circles", "UI-Classes-Circles.blp"),
                      ("bar", "UI-StatusBar.blp")):
        hit = [f for f in os.listdir(src) if f.lower().endswith(name.lower())]
        art[key] = Blp(open(os.path.join(src, sorted(hit)[-1]), "rb").read()).image().convert("RGBA")
    render(art, out, "numbers" in sys.argv[3:], "fives" in sys.argv[3:])
