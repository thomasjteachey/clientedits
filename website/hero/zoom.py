import sys
from PIL import Image, ImageDraw
x0, y0, x1, y1, s = map(int, sys.argv[1:6]); out = sys.argv[6]
im = Image.open('lowres.png').crop((x0, y0, x1, y1)).resize(((x1-x0)*s, (y1-y0)*s), Image.NEAREST).convert('RGB')
d = ImageDraw.Draw(im)
for x in range(x0, x1):
    if x % 5 == 0: d.line([((x-x0)*s, 0), ((x-x0)*s, im.height)], fill=(255,255,255) if x % 10 == 0 else (120,120,120), width=1)
for y in range(y0, y1):
    if y % 5 == 0: d.line([(0, (y-y0)*s), (im.width, (y-y0)*s)], fill=(255,255,255) if y % 10 == 0 else (120,120,120), width=1)
for x in range(x0, x1):
    if x % 10 == 0: d.text(((x-x0)*s+2, 2), str(x), fill=(255,255,0))
for y in range(y0, y1):
    if y % 10 == 0: d.text((2, (y-y0)*s+2), str(y), fill=(0,255,255))
im.save(out)
