#!/usr/bin/env python3
"""2x2 sheet of the bottom-right of the create screen, one layout per quarter,
from the PNGs charcreate_render.py wrote.

usage: charcreate_sheet.py [WxH [out.png [layout layout layout layout]]]
"""
import os
import sys
from PIL import Image, ImageDraw, ImageFont

HERE = os.path.dirname(os.path.abspath(__file__))
res = sys.argv[1] if len(sys.argv) > 1 else '1920x1080'
out = sys.argv[2] if len(sys.argv) > 2 else os.path.join(HERE, 'cc_sheet_%s.png' % res)
keys = sys.argv[3:7] or ['old_1.00016', 'live', 'B_pinned', 'C_onepanel']
w, h = (int(v) for v in res.split('x'))
# crop: from the name boxes to the right edge, lower half
cx0 = int(w / 2 - 260 * h / 768)
cy0 = int(h * 0.50)
f = ImageFont.truetype(os.path.join(HERE, 'FRIZQT__.TTF'), 34)
crops = []
for key in keys:
    im = Image.open(os.path.join(HERE, 'cc_%s_%s.png' % (key, res))).crop((cx0, cy0, w, h))
    d = ImageDraw.Draw(im)
    d.rectangle([0, 0, im.width, 52], fill=(0, 0, 0))
    d.text((14, 8), key, font=f, fill=(255, 255, 255))
    crops.append(im)
cw, ch = crops[0].size
sheet = Image.new('RGB', (cw * 2 + 12, ch * 2 + 12), (255, 255, 255))
for i, im in enumerate(crops):
    sheet.paste(im, ((i % 2) * (cw + 12), (i // 2) * (ch + 12)))
sheet.save(out)
print(out, sheet.size)
