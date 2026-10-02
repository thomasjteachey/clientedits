"""Battle logo: chibi WoW classes fighting on top of the pixel CENTURION wordmark.

Laid out on a logical pixel grid, scaled by a whole number to the 256x128 screen
area, then doubled to the 512x256 texture.
"""
import sys

from PIL import Image

import fighters as F
from make_logo3 import word_image

SW, SH = 256, 128


def crop(im):
    return im.crop(im.getchannel('A').getbbox())


def scene(scale):
    lw, lh = SW // scale, SH // scale
    word = word_image('CENTURION', 1)
    war, rog = crop(F.warrior().image()), crop(F.rogue().image(True))
    mag, pal = crop(F.mage().image()), crop(F.paladin().image(True))
    fire, spark = crop(F.fireball().image()), crop(F.spark().image())

    out = Image.new('RGBA', (lw, lh), (0, 0, 0, 0))
    wx = (lw - word.width) // 2
    wy = lh - word.height - 1
    floor = wy + 1                                    # feet rest on the letters' top edge

    def stand(im, x):
        out.alpha_composite(im, (x, floor - im.height + 1))

    # left bout: warrior and rogue, blades crossing over the spark
    stand(war, 3)
    stand(rog, 24)
    out.alpha_composite(spark, (19, floor - 16))
    # right bout: mage throws, paladin blocks
    stand(mag, 42)
    out.alpha_composite(fire, (57, floor - 13))
    stand(pal, lw - pal.width)                        # shield in front: the fireball is blocked
    out.alpha_composite(word, (wx, wy))
    return out


def build(scale=3):
    s = scene(scale)
    s = s.resize((s.width * scale, s.height * scale), Image.NEAREST)
    screen = Image.new('RGBA', (SW, SH), (0, 0, 0, 0))
    screen.alpha_composite(s, ((SW - s.width) // 2, (SH - s.height) // 2))
    return screen.resize((SW * 2, SH * 2), Image.NEAREST)


if __name__ == '__main__':
    scale = int(sys.argv[1]) if len(sys.argv) > 1 else 3
    build(scale).save('centurion_logo_H.png')
