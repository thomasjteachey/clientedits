"""The owner's logo (owner_logo_source.webp, art on flat black) -> 512x256 RGBA.

Black outside the artwork becomes transparent, keyed by brightness so glows and
antialiased edges fade out instead of leaving a dark fringe. Black that the artwork
ENCLOSES (the inside of the banner behind the letters) stays opaque: that is found by
flood-filling the background from the border - whatever the fill cannot reach is
part of the design.
"""
import numpy as np
from PIL import Image, ImageFilter

src = Image.open('owner_logo_source.webp').convert('RGB')
rgb = np.asarray(src).astype(np.float32)
bright = rgb.max(axis=2)

# solid artwork, closed a little so hairline gaps in the frame don't leak the fill
art = Image.fromarray(((bright > 40) * 255).astype(np.uint8)).filter(ImageFilter.MaxFilter(15))
outside = Image.new('L', art.size, 0)
fill = art.copy()
from PIL import ImageDraw  # noqa: E402
ImageDraw.floodfill(fill, (0, 0), 128)
outside = np.asarray(fill) == 128                     # background reachable from the border
# shrink back by what the closing grew, or a black halo rings the whole logo
enclosed = np.asarray(Image.fromarray((~outside * 255).astype(np.uint8)).filter(ImageFilter.MinFilter(15))) > 0

alpha = np.where(enclosed, 255.0, np.clip(bright * 255.0 / 200.0, 0, 255))
a = np.maximum(alpha, 1)[..., None] / 255.0
colour = np.where(enclosed[..., None], rgb, np.clip(rgb / a, 0, 255))   # un-premultiply the fringe
out = Image.fromarray(np.dstack([colour, alpha]).astype(np.uint8), 'RGBA')

bb = out.getchannel('A').point(lambda v: 255 if v > 8 else 0).getbbox()
out = out.crop(bb)
k = min(504 / out.width, 248 / out.height)
out = out.resize((round(out.width * k), round(out.height * k)), Image.LANCZOS)
canvas = Image.new('RGBA', (512, 256), (0, 0, 0, 0))
canvas.alpha_composite(out, ((512 - out.width) // 2, (256 - out.height) // 2))
canvas.save('centurion_logo_final.png')
print('crop', bb, '->', out.size)
