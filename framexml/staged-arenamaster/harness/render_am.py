"""Lay out a frame-tree dump from run.py the way the client anchors regions, and draw it
from the client's own textures and fonts. usage: render_dump.py <snapshot> <out.png> [hover_id]"""
import json
import os
import sys

from PIL import Image, ImageChops, ImageDraw, ImageFont, ImageOps

HERE = os.path.dirname(os.path.abspath(__file__))
BB = r'C:\Projects\Gamedev\wow\clientedits\bountyboard'
sys.path.insert(0, BB)
import artlib  # noqa: E402

S = 2
ART = os.path.join(BB, 'art')
ICONS = os.path.join(BB, 'icons')
EXTRA = os.path.join(HERE, 'tex')

snap = json.load(open(os.path.join(HERE, 'dumps.json')))[sys.argv[1]]
out_path = sys.argv[2]
hover = int(sys.argv[3]) if len(sys.argv) > 3 else None
O = {int(k): v for k, v in snap['objects'].items()}
UIP = snap['uiparent']
if hover is None:
    hover = snap.get('hover')
UIW, UIH = 1100, 720

FRAC = {'TOPLEFT': (0, 1), 'TOP': (.5, 1), 'TOPRIGHT': (1, 1), 'LEFT': (0, .5), 'CENTER': (.5, .5),
        'RIGHT': (1, .5), 'BOTTOMLEFT': (0, 0), 'BOTTOM': (.5, 0), 'BOTTOMRIGHT': (1, 0)}

# ------------------------------------------------------------------ fonts and text
_fonts = {}


def pil_font(file, size):
    name = os.path.basename(file.replace('\\', '/')).upper()
    key = (name, size)
    if key not in _fonts:
        _fonts[key] = ImageFont.truetype(os.path.join(ART, name), int(round(size * S)))
    return _fonts[key]


_measure = ImageDraw.Draw(Image.new('RGBA', (8, 8)))


def text_width(text, f):
    return _measure.textlength(text, font=f) / S


def wrap(text, f, maxw):
    if maxw is None:
        return [text]
    lines, cur = [], ''
    for word in text.split(' '):
        t = (cur + ' ' + word).strip()
        if text_width(t, f) <= maxw or not cur:
            cur = t
        else:
            lines.append(cur)
            cur = word
    lines.append(cur)
    return lines


def font_of(o):
    f = o.get('font') or {'file': 'Fonts\\FRIZQT__.TTF', 'size': 12, 'colour': [1, 1, 1], 'shadow': [0, 0, 0]}
    return f


# ------------------------------------------------------------------ layout
_rects = {}


def natural_size(o):
    w, h = o.get('_w'), o.get('_h')
    if o['_kind'] == 'FontString':
        f = font_of(o)
        pf = pil_font(f['file'], f['size'])
        text = o.get('_text') or ''
        lines = wrap(text, pf, w) if w else [text]
        if w is None:
            w = max([text_width(l, pf) for l in lines] + [0])
        if h is None:
            h = len(lines) * (f['size'] + 2)
    return w, h


def rect(oid):
    if oid in _rects:
        return _rects[oid]
    o = O[oid]
    if oid == UIP:
        r = (0, 0, UIW, UIH)
        _rects[oid] = r
        return r
    w, h = natural_size(o)
    L = R = T = B = CX = CY = None
    for p in o['points']:
        rl, rb, rr, rt = rect(p['rel'])
        fx, fy = FRAC[p['relPoint']]
        ax = rl + (rr - rl) * fx + p['x']
        ay = rb + (rt - rb) * fy + p['y']
        px, py = FRAC[p['point']]
        if px == 0:
            L = ax
        elif px == 1:
            R = ax
        else:
            CX = ax
        if py == 0:
            B = ay
        elif py == 1:
            T = ay
        else:
            CY = ay
    if L is not None and R is not None:
        pass
    elif L is not None:
        R = L + (w or 0)
    elif R is not None:
        L = R - (w or 0)
    elif CX is not None:
        L, R = CX - (w or 0) / 2, CX + (w or 0) / 2
    if B is not None and T is not None:
        pass
    elif B is not None:
        T = B + (h or 0)
    elif T is not None:
        B = T - (h or 0)
    elif CY is not None:
        B, T = CY - (h or 0) / 2, CY + (h or 0) / 2
    r = (L or 0, B or 0, R or 0, T or 0)
    _rects[oid] = r
    return r


# ------------------------------------------------------------------ textures
_tex = {}


def texture(path):
    if path is None:
        return None
    key = path.lower()
    if key in _tex:
        return _tex[key]
    base = os.path.basename(path.replace('\\', '/'))
    if base.upper() == 'WHITE8X8':
        im = Image.new('RGBA', (8, 8), (255, 255, 255, 255))
    else:
        im = None
        for d in (ART, ICONS, EXTRA):
            for cand in (base + '.png',):
                for f in os.listdir(d) if os.path.isdir(d) else []:
                    if f.lower() == cand.lower():
                        im = Image.open(os.path.join(d, f)).convert('RGBA')
                        break
                if im:
                    break
            if im:
                break
        if im is None:
            full = path if path.lower().endswith('.blp') else path + '.blp'
            r = artlib.png(full, EXTRA)
            if r:
                im = Image.open(r[0]).convert('RGBA')
            else:
                print('missing texture', path)
                im = Image.new('RGBA', (8, 8), (255, 0, 255, 255))
    _tex[key] = im
    return im


canvas = Image.new('RGBA', (UIW * S, UIH * S), (0, 0, 0, 0))


def to_px(l, b, r, t):
    return int(round(l * S)), int(round((UIH - t) * S)), int(round(r * S)), int(round((UIH - b) * S))


def paste(im, l, b, r, t, add=False):
    x0, y0, x1, y1 = to_px(l, b, r, t)
    if x1 <= x0 or y1 <= y0:
        return
    im = im.resize((x1 - x0, y1 - y0), Image.LANCZOS)
    if add:
        base = canvas.crop((x0, y0, x1, y1))
        black = Image.new('RGBA', im.size, (0, 0, 0, 0))
        premul = Image.composite(im, black, im.split()[3])
        summed = ImageChops.add(base.convert('RGB'), premul.convert('RGB')).convert('RGBA')
        summed.putalpha(base.split()[3])
        canvas.paste(summed, (x0, y0))
    else:
        canvas.alpha_composite(im, (x0, y0))


def draw_texture(o, alpha):
    l, b, r, t = rect(o['_id'])
    if o.get('_file') is None and o.get('_colour') is None:
        return
    if o.get('_colour') is not None:
        c = o['_colour']
        im = Image.new('RGBA', (8, 8), tuple(int(v * 255) for v in c[:3]) + (int(c[3] * 255),))
    else:
        im = texture(o['_file']).copy()
    tc = o.get('_tc')
    if tc:
        lft, rgt, top, bot = tc
        fx, fy = lft > rgt, top > bot
        lft, rgt = min(lft, rgt), max(lft, rgt)
        top, bot = min(top, bot), max(top, bot)
        im = im.crop((int(round(lft * im.width)), int(round(top * im.height)),
                      max(int(round(lft * im.width)) + 1, int(round(rgt * im.width))),
                      max(int(round(top * im.height)) + 1, int(round(bot * im.height)))))
        if fx:
            im = ImageOps.mirror(im)
        if fy:
            im = ImageOps.flip(im)
    if o.get('_desat'):
        g = ImageOps.grayscale(im.convert('RGB')).convert('RGBA')
        g.putalpha(im.split()[3])
        im = g
    vc = o.get('_vc')
    if vc:
        ch = im.split()
        im = Image.merge('RGBA', (ch[0].point(lambda v: v * vc[0]), ch[1].point(lambda v: v * vc[1]),
                                  ch[2].point(lambda v: v * vc[2]), ch[3].point(lambda v: v * vc[3])))
    a = alpha * o.get('_alpha', 1)
    if a < 1:
        im.putalpha(im.split()[3].point(lambda v: v * a))
    grad = o.get('_gradient')
    if grad:
        orient, a1, a2 = grad[:3]
        # the gradient's colour multiplies the texture, as SetGradientAlpha does
        ch = im.split()
        im = Image.merge('RGBA', (ch[0].point(lambda v: v * grad[3]), ch[1].point(lambda v: v * grad[4]),
                                  ch[2].point(lambda v: v * grad[5]), ch[3]))
        x0, y0, x1, y1 = to_px(l, b, r, t)
        w, h = max(1, x1 - x0), max(1, y1 - y0)
        im = im.resize((w, h))
        mask = Image.new('L', (w, h))
        md = mask.load()
        for yy in range(h):
            for xx in range(w):
                f = (xx / max(1, w - 1)) if orient == 'HORIZONTAL' else (1 - yy / max(1, h - 1))
                md[xx, yy] = int(255 * (a1 + (a2 - a1) * f))
        im.putalpha(ImageChops.multiply(im.split()[3], mask))
    paste(im, l, b, r, t, add=o.get('_blend') == 'ADD')


def draw_text(o, alpha):
    text = o.get('_text')
    if not text:
        return
    f = font_of(o)
    pf = pil_font(f['file'], f['size'])
    l, b, r, t = rect(o['_id'])
    colour = o.get('_textColour') or f.get('colour') or [1, 1, 1]
    col = tuple(int(c * 255) for c in colour[:3]) + (int(255 * alpha),)
    lines = wrap(text, pf, o.get('_w'))
    lh = f['size'] + 2
    total = len(lines) * lh
    top = (t + b) / 2 + total / 2
    d = ImageDraw.Draw(canvas)
    for i, line in enumerate(lines):
        lw = text_width(line, pf)
        j = o.get('_justifyH', 'CENTER')
        x = l if j == 'LEFT' else (r - lw if j == 'RIGHT' else (l + r) / 2 - lw / 2)
        cy = top - i * lh - lh / 2
        px, py = x * S, (UIH - cy) * S
        if f.get('shadow'):
            sc = tuple(int(c * 255) for c in f['shadow'][:3]) + (int(255 * alpha),)
            d.text((px + S, py + S), line, font=pf, fill=sc, anchor='lm')
        d.text((px, py), line, font=pf, fill=col, anchor='lm')


def draw_backdrop(o):
    bd = o['backdrop']
    l, b, r, t = rect(o['_id'])
    il, ir, it, ib = bd['insets']
    if bd.get('bgFile'):
        tile = texture(bd['bgFile'])
        if bd.get('colour'):
            # SetBackdropColor multiplies the tile, alpha included
            c = bd['colour']
            ch = tile.split()
            tile = Image.merge('RGBA', tuple(ch[i].point(lambda v, k=c[i]: v * k) for i in range(4)))
        ts = bd.get('tileSize') or 32
        x0, y0, x1, y1 = to_px(l + il, b + ib, r - ir, t - it)
        tt = tile.resize((int(ts * S), int(ts * S)))
        layer = Image.new('RGBA', (x1 - x0, y1 - y0))
        for yy in range(0, layer.height, tt.height):
            for xx in range(0, layer.width, tt.width):
                layer.alpha_composite(tt, (xx, yy))
        canvas.alpha_composite(layer, (x0, y0))
    if bd.get('edgeFile'):
        im = texture(bd['edgeFile'])
        e = bd['edgeSize']
        sl = im.width / 8.0

        def piece(i):
            return im.crop((int(i * sl), 0, int((i + 1) * sl), im.height))
        # left and right edges, tiled downwards between the corners
        y = t - e
        while y > b + e + 0.01:
            hh = min(e, y - (b + e))
            for idx, xx in ((0, l), (1, r - e)):
                p = piece(idx)
                p = p.crop((0, 0, p.width, max(1, int(p.height * hh / e))))
                paste(p, xx, y - hh, xx + e, y)
            y -= e
        x = l + e
        while x < r - e - 0.01:
            ww = min(e, (r - e) - x)
            for idx, yy in ((2, t - e), (3, b)):
                p = piece(idx).rotate(90, expand=True)
                p = p.crop((0, 0, max(1, int(p.width * ww / e)), p.height))
                paste(p, x, yy, x + ww, yy + e)
            x += e
        paste(piece(4), l, t - e, l + e, t)
        paste(piece(5), r - e, t - e, r, t)
        paste(piece(6), l, b, l + e, b + e)
        paste(piece(7), r - e, b, r, b + e)


LAYERS = ['BACKGROUND', 'BORDER', 'ARTWORK', 'OVERLAY', 'HIGHLIGHT']


def visible_region(o, frame):
    if not o.get('_shown', True):
        return False
    st = o.get('_buttonState')
    if st == '_pushed':
        return False
    if st == '_highlight':
        return hover == frame['_id']
    if st == '_disabledTex':
        return bool(frame.get('_disabled'))
    if st == '_normal' and frame.get('_disabled') and any(
            O[rid].get('_buttonState') == '_disabledTex' for rid in frame['regions']):
        return False
    if o.get('_layer') == 'HIGHLIGHT':
        return hover == frame['_id']
    return True


def draw_panel_button(o):
    l, b, r, t = rect(o['_id'])
    btn = texture('Interface\\Buttons\\UI-Panel-Button-Disabled' if o.get('_disabled') else 'Interface\\Buttons\\UI-Panel-Button-Up')
    for (cl, cr), (x0, x1) in (((0, 0.09375), (l, l + 12)), ((0.09375, 0.53125), (l + 12, r - 12)),
                               ((0.53125, 0.625), (r - 12, r))):
        p = btn.crop((int(cl * btn.width), 0, int(cr * btn.width), int(0.6875 * btn.height)))
        paste(p, x0, b, x1, t)


def draw_frame(fid, alpha=1.0):
    f = O[fid]
    if not f.get('_shown', True):
        return
    a = alpha * f.get('_alpha', 1)
    if f.get('backdrop'):
        draw_backdrop(f)
    if f.get('_panelButton'):
        draw_panel_button(f)
    regions = [O[r] for r in f['regions']]
    for layer in LAYERS:
        for o in regions:
            if o.get('_layer', 'ARTWORK') != layer or not visible_region(o, f):
                continue
            if o['_kind'] == 'Texture':
                draw_texture(o, a)
            else:
                if f.get('_panelButton') and f.get('_disabled'):
                    o = dict(o, _textColour=[0.5, 0.5, 0.5])
                draw_text(o, a)
    kids = sorted(f['children'], key=lambda c: (O[c].get('_level', 0), c))
    for c in kids:
        draw_frame(c, a)


root = snap['root']
draw_frame(root)
for extra in snap.get('extra') or []:
    draw_frame(extra)
l, b, r, t = rect(root)
x0, y0, x1, y1 = to_px(l - 12, b - 12, r + 12, t + 80)
out = Image.new('RGBA', (x1 - x0, y1 - y0), (26, 26, 32, 255))
out.alpha_composite(canvas.crop((x0, y0, x1, y1)))
out.convert('RGB').save(out_path)
print(out_path)
