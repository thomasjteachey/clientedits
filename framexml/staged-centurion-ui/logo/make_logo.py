"""Centurion logo, orange theme, in the style of the WoW glue logo.

Rendered at 4x (2048x1024) and downsampled to 512x256, the size and aspect of
Glues-WoW-WotLKLogo.blp, which every glue screen draws at 256x128.
"""
import math
import sys

import numpy as np
from PIL import Image, ImageChops, ImageDraw, ImageFilter, ImageFont

S = 4
W, H = 512 * S, 256 * S
FONT = r'C:\Users\broki\AppData\Local\Microsoft\Windows\Fonts\LifeCraft_Font.ttf'


def L(size=(W, H)):
    return Image.new('L', size, 0)


def arr(im):
    return np.asarray(im, dtype=np.float32) / 255.0


def grad_v(stops, h=H, w=W):
    """Vertical colour gradient; stops = [(t, (r,g,b)), ...]."""
    ys = np.linspace(0, 1, h)
    out = np.zeros((h, 3), np.float32)
    for c in range(3):
        out[:, c] = np.interp(ys, [s[0] for s in stops], [s[1][c] for s in stops])
    return np.repeat(out[:, None, :], w, axis=1)


def bevel(mask, radius, strength, angle=-2.3):
    """Signed light term from a blurred height field: + lit, - shaded."""
    h = arr(mask.filter(ImageFilter.GaussianBlur(radius)))
    gy, gx = np.gradient(h)
    lx, ly = math.cos(angle), math.sin(angle)
    return np.clip(-(gx * lx + gy * ly) * strength, -1, 1)


def shade(rgb, light, hi=(255, 246, 214), lo=(40, 12, 0)):
    lit = np.clip(light, 0, 1)[..., None]
    dark = np.clip(-light, 0, 1)[..., None]
    rgb = rgb * (1 - lit) + np.array(hi, np.float32) * lit
    return rgb * (1 - dark) + np.array(lo, np.float32) * dark


def over(dst_rgb, dst_a, src_rgb, src_a):
    src_a = src_a[..., None]
    out_a = src_a + dst_a[..., None] * (1 - src_a)
    safe = np.where(out_a == 0, 1, out_a)
    out_rgb = (src_rgb * src_a + dst_rgb * dst_a[..., None] * (1 - src_a)) / safe
    return out_rgb, out_a[..., 0]


def dilate(mask, px):
    k = px * 2 + 1
    while k > 0:
        step = min(k, 25) | 1
        mask = mask.filter(ImageFilter.MaxFilter(step))
        k -= step - 1
        if step <= 1:
            break
    return mask


def noise(seed, blur):
    rng = np.random.default_rng(seed)
    n = Image.fromarray((rng.random((H // 4, W // 4)) * 255).astype(np.uint8)).resize((W, H), Image.BICUBIC)
    return arr(n.filter(ImageFilter.GaussianBlur(blur))) - 0.5


# ---------------------------------------------------------------- geometry
CX = W // 2
TEXT_Y = 150 * S          # baseline-ish centre of the wordmark
PLATE_TOP, PLATE_BOT = 104 * S, 198 * S


def plate_shape(inset=0):
    """Banner behind the wordmark: long hexagon with swallow-tail points."""
    i = inset
    x0, x1 = 22 * S + i, 490 * S - i
    t, b = PLATE_TOP + i, PLATE_BOT - i
    m = (t + b) / 2
    notch = 14 * S
    return [
        (x0 + 34 * S, t), (CX - 70 * S, t), (CX, t - 16 * S + i * 0.6), (CX + 70 * S, t),
        (x1 - 34 * S, t), (x1, m - 6 * S), (x1 - notch, m), (x1, m + 6 * S), (x1 - 34 * S, b),
        (CX + 70 * S, b), (CX, b + 14 * S - i * 0.6), (CX - 70 * S, b),
        (x0 + 34 * S, b), (x0, m + 6 * S), (x0 + notch, m), (x0, m - 6 * S),
    ]


def helmet_mask():
    """Roman galea with a transverse centurion crest, sitting on the banner."""
    m = L()
    d = ImageDraw.Draw(m)
    hx, hy = CX, 72 * S            # centre of the dome
    r = 30 * S
    # dome
    d.ellipse([hx - r, hy - r, hx + r, hy + r], fill=255)
    # cheek guards + neck flare, down to the banner
    d.polygon([(hx - r, hy), (hx - r - 6 * S, hy + 30 * S), (hx - 14 * S, hy + 38 * S), (hx - 10 * S, hy + 8 * S),
               (hx + 10 * S, hy + 8 * S), (hx + 14 * S, hy + 38 * S), (hx + r + 6 * S, hy + 30 * S), (hx + r, hy)],
              fill=255)
    # face opening (T-shaped visor) punched out
    d.polygon([(hx - 16 * S, hy - 2 * S), (hx + 16 * S, hy - 2 * S), (hx + 9 * S, hy + 8 * S), (hx + 6 * S, hy + 34 * S),
               (hx - 6 * S, hy + 34 * S), (hx - 9 * S, hy + 8 * S)], fill=0)
    # brow ridge
    d.rectangle([hx - r - 2 * S, hy - 6 * S, hx + r + 2 * S, hy - 1 * S], fill=255)
    return m


def crest_mask():
    """The fan of bristles: an arch of wedges over the dome."""
    m = L()
    d = ImageDraw.Draw(m)
    hx, hy = CX, 72 * S
    r_in, r_out = 26 * S, 62 * S
    n = 23
    a0, a1 = math.radians(200), math.radians(340)
    for k in range(n):
        a = a0 + (a1 - a0) * (k + 0.5) / n
        wa = (a1 - a0) / n * 0.62
        ro = r_out * (0.92 + 0.08 * math.sin(math.pi * (k + 0.5) / n))
        pts = [(hx + r_in * math.cos(a - wa * 1.6), hy + r_in * math.sin(a - wa * 1.6)),
               (hx + ro * math.cos(a - wa * 0.55), hy + ro * math.sin(a - wa * 0.55)),
               (hx + (ro + 5 * S) * math.cos(a), hy + (ro + 5 * S) * math.sin(a)),
               (hx + ro * math.cos(a + wa * 0.55), hy + ro * math.sin(a + wa * 0.55)),
               (hx + r_in * math.cos(a + wa * 1.6), hy + r_in * math.sin(a + wa * 1.6))]
        d.polygon(pts, fill=255)
    return m


def laurel_mask(side):
    """A laurel branch curling from the banner tip up towards the helmet."""
    m = L()
    d = ImageDraw.Draw(m)
    sgn = -1 if side == 'L' else 1
    # stem: quadratic curve from near the helm outward and down to the banner top
    p0 = (CX + sgn * 44 * S, 96 * S)
    p1 = (CX + sgn * 110 * S, 58 * S)
    p2 = (CX + sgn * 176 * S, 104 * S)
    pts = []
    for i in range(41):
        t = i / 40
        x = (1 - t) ** 2 * p0[0] + 2 * (1 - t) * t * p1[0] + t * t * p2[0]
        y = (1 - t) ** 2 * p0[1] + 2 * (1 - t) * t * p1[1] + t * t * p2[1]
        pts.append((x, y))
    d.line(pts, fill=255, width=4 * S)
    for i in range(2, 40, 4):
        (x, y), (x2, y2) = pts[i], pts[i + 1]
        ang = math.atan2(y2 - y, x2 - x)
        for flip in (-1, 1):
            la = ang + flip * 0.7
            ln = 19 * S * (1 - i / 70)
            cxl, cyl = x + math.cos(la) * ln * 0.55, y + math.sin(la) * ln * 0.55
            leaf = []
            for j in range(16):
                u = j / 15 * 2 * math.pi
                ex, ey = math.cos(u) * ln * 0.55, math.sin(u) * ln * 0.26
                leaf.append((cxl + ex * math.cos(la) - ey * math.sin(la), cyl + ex * math.sin(la) + ey * math.cos(la)))
            d.polygon(leaf, fill=255)
    return m


def text_mask():
    """CENTURION with the first and last letters oversized, WARCRAFT-style."""
    big = ImageFont.truetype(FONT, 104 * S)
    small = ImageFont.truetype(FONT, 84 * S)
    word = 'CENTURION'
    glyphs = [(ch, big if i in (0, len(word) - 1) else small) for i, ch in enumerate(word)]
    track = -1 * S
    widths = [f.getbbox(ch)[2] - f.getbbox(ch)[0] for ch, f in glyphs]
    total = sum(widths) + track * (len(glyphs) - 1)
    m = L()
    d = ImageDraw.Draw(m)
    x = CX - total / 2
    for (ch, f), w in zip(glyphs, widths):
        bb = f.getbbox(ch)
        # centre every glyph on TEXT_Y by its own ink box
        y = TEXT_Y - (bb[1] + bb[3]) / 2
        d.text((x - bb[0], y), ch, font=f, fill=255)
        x += w + track
    return m


# ---------------------------------------------------------------- build
def build():
    rgb = np.zeros((H, W, 3), np.float32)
    a = np.zeros((H, W), np.float32)

    plate = L(); ImageDraw.Draw(plate).polygon(plate_shape(), fill=255)
    plate_in = L(); ImageDraw.Draw(plate_in).polygon(plate_shape(7 * S), fill=255)
    text = text_mask()
    helm = helmet_mask()
    crest = crest_mask()
    laurels = ImageChops.lighter(laurel_mask('L'), laurel_mask('R'))

    emblem = ImageChops.lighter(helm, crest)
    everything = ImageChops.lighter(ImageChops.lighter(plate, emblem), ImageChops.lighter(text, laurels))

    # soft ember glow + drop shadow around the whole lockup
    glow = arr(dilate(everything, 3 * S).filter(ImageFilter.GaussianBlur(12 * S))) * 0.85
    rgb, a = over(rgb, a, np.broadcast_to(np.array([255, 110, 10], np.float32), rgb.shape), glow * 0.55)
    shadow = arr(everything.filter(ImageFilter.GaussianBlur(5 * S)).transform((W, H), Image.AFFINE, (1, 0, 0, 0, 1, -4 * S)))
    rgb, a = over(rgb, a, np.zeros_like(rgb), shadow * 0.8)

    # banner: bronze rim, then a dark iron field
    rim = shade(grad_v([(0.40, (255, 196, 92)), (0.60, (214, 110, 20)), (0.78, (120, 50, 6))]),
                bevel(plate, 3 * S, 140))
    rgb, a = over(rgb, a, rim, arr(plate))
    field = grad_v([(0.42, (70, 40, 26)), (0.62, (34, 18, 12)), (0.78, (16, 8, 6))])
    field = field * (1 + noise(3, 2) [..., None] * 0.35)
    field = shade(field, bevel(plate_in, 4 * S, -90), hi=(120, 70, 40), lo=(0, 0, 0))
    rgb, a = over(rgb, a, field, arr(plate_in))

    # laurels, bronze
    lau_rgb = shade(grad_v([(0.15, (255, 200, 100)), (0.45, (196, 104, 24))]), bevel(laurels, 2 * S, 160))
    rgb, a = over(rgb, a, np.zeros_like(rgb), arr(dilate(laurels, S)) * 0.9)
    rgb, a = over(rgb, a, lau_rgb, arr(laurels))

    # crest: flame orange bristles
    crest_rgb = grad_v([(0.03, (255, 226, 120)), (0.08, (255, 140, 20)), (0.12, (214, 58, 4))])
    crest_rgb = crest_rgb * (1 + noise(7, 1) [..., None] * 0.5)
    crest_rgb = shade(crest_rgb, bevel(crest, 2 * S, 120), hi=(255, 236, 170), lo=(60, 10, 0))
    rgb, a = over(rgb, a, np.zeros_like(rgb), arr(dilate(crest, 2 * S)))
    rgb, a = over(rgb, a, crest_rgb, arr(crest))

    # helmet: polished bronze
    helm_rgb = shade(grad_v([(0.05, (255, 222, 150)), (0.12, (224, 140, 48)), (0.19, (122, 56, 12))]),
                     bevel(helm, 3 * S, 170))
    rgb, a = over(rgb, a, np.zeros_like(rgb), arr(dilate(helm, 2 * S)))
    rgb, a = over(rgb, a, helm_rgb, arr(helm))

    # wordmark: dark keyline, bright gold keyline, then the molten fill
    outer = dilate(text, 5 * S)
    gold = dilate(text, 3 * S)
    rgb, a = over(rgb, a, np.full_like(rgb, 12), arr(outer))
    gold_rgb = shade(grad_v([(0.47, (255, 240, 190)), (0.58, (240, 168, 60)), (0.72, (150, 70, 10))]),
                     bevel(gold, 2 * S, 150))
    rgb, a = over(rgb, a, gold_rgb, arr(gold))
    rgb, a = over(rgb, a, np.full_like(rgb, 20), arr(dilate(text, S)))
    fill = grad_v([(0.44, (255, 214, 110)), (0.52, (255, 150, 24)), (0.60, (246, 96, 4)),
                   (0.67, (196, 52, 0)), (0.73, (120, 26, 0))])
    fill = shade(fill, bevel(text, 3 * S, 110) * 0.8, hi=(255, 232, 160), lo=(60, 12, 0))
    # a thin specular band across the upper third of the letters
    band = grad_v([(0.49, (0, 0, 0)), (0.515, (255, 255, 255)), (0.54, (0, 0, 0))])[..., 0:1] / 255.0
    fill = fill * (1 - band * 0.22) + np.array([255, 236, 180], np.float32) * band * 0.22
    rgb, a = over(rgb, a, fill, arr(text))

    out = np.dstack([np.clip(rgb, 0, 255), np.clip(a * 255, 0, 255)]).astype(np.uint8)
    img = Image.fromarray(out, 'RGBA')
    # fit the ink to the 2:1 canvas the glue XML expects, with a small margin for the glow
    bb = img.getchannel('A').point(lambda v: 255 if v > 6 else 0).getbbox()
    img = img.crop(bb)
    k = min((W - 6 * S) / img.width, (H - 6 * S) / img.height)
    img = img.resize((int(img.width * k), int(img.height * k)), Image.LANCZOS)
    canvas = Image.new('RGBA', (W, H), (0, 0, 0, 0))
    canvas.paste(img, ((W - img.width) // 2, (H - img.height) // 2))
    return canvas.resize((512, 256), Image.LANCZOS)


if __name__ == '__main__':
    out = sys.argv[1] if len(sys.argv) > 1 else 'centurion_logo.png'
    im = build()
    im.save(out)
    # previews on the three backgrounds the logo actually sits on
    prev = Image.new('RGBA', (512 * 2, 256 * 2), (0, 0, 0, 255))
    for k, bg in enumerate([(20, 24, 40), (200, 200, 200), (60, 40, 20), (0, 0, 0)]):
        tile = Image.new('RGBA', (512, 256), bg + (255,))
        tile.alpha_composite(im)
        prev.paste(tile, ((k % 2) * 512, (k // 2) * 256))
    prev.save(out.replace('.png', '_preview.png'))
