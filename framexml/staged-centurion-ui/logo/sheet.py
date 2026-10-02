"""Contact sheet: each logo up close, then at real size in the character-create corner."""
import sys

from PIL import Image, ImageDraw, ImageFont

sys.path.insert(0, r'..\render')
from blp import Blp  # noqa: E402

BG = (r'C:\Users\broki\AppData\Local\Temp\claude\C--Projects-Gamedev-wow-servers-tc-lplus'
      r'\a5a77ab1-b5bc-46d9-9f27-6a2987d0480a\scratchpad\glue\Interface\Glues\LoadingScreens\LoadScreenNorthrendWide.blp')

names = sys.argv[1:]
bg = Blp(open(BG, 'rb').read()).image().convert('RGBA').resize((1366, 683)).crop((171, 0, 1195, 683)).resize((1024, 768))
font = ImageFont.truetype(r'..\render\FRIZQT__.TTF', 22)
out = Image.new('RGBA', (len(names) * 540, 460), (16, 16, 16, 255))
d = ImageDraw.Draw(out)
for k, v in enumerate(names):
    im = Image.open('centurion_logo_%s.png' % v)
    t = Image.new('RGBA', (512, 256), (40, 44, 56, 255))
    t.alpha_composite(im)
    out.paste(t, (k * 540 + 14, 40))
    small = bg.crop((1024 - 300, 0, 1024, 140)).copy()
    small.alpha_composite(im.resize((256, 128), Image.LANCZOS), (300 - 9 - 256, 0))
    out.paste(small, (k * 540 + 120, 310))
    d.text((k * 540 + 14, 8), v, font=font, fill='white')
out.save('variants_megaman.png')
