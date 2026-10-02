"""Minimal BLP2 reader/writer for WoW 3.3.5 armour textures.

Two encodings matter here:

  compression 1 - PALETTIZED. 256-entry BGRA palette followed by one byte of
                  palette index per pixel (and, if alphaDepth is 8, a separate
                  alpha plane). This is what WoW uses for the character body
                  texture components - the bulk of any armour set.

                  Recolouring these is lossless and trivial: rewrite the 256
                  palette entries and leave the index data untouched. No
                  requantisation, no generation loss, mips stay valid because
                  they are indices into the same palette.

  compression 2 - DXT1/3/5 block compression. Used by the shoulder and helm
                  model textures under Item\\ObjectComponents. These have to be
                  decoded, recoloured and re-encoded, which does cost a little
                  quality. Pillow handles both directions via DDS.

Header layout (BLP2):
    char     magic[4]     "BLP2"
    uint32   version      1
    uint8    compression  1 palettized | 2 DXT | 3 BGRA
    uint8    alphaDepth   0 | 1 | 4 | 8
    uint8    alphaType    0 DXT1 | 1 DXT3 | 7 DXT5   (only when compression==2)
    uint8    hasMips
    uint32   width, height
    uint32   mipOffsets[16]
    uint32   mipSizes[16]
    -> 148 bytes, then a 1024-byte BGRA palette when compression==1
"""
import io
import struct

from PIL import Image

HEADER = 148
PALETTE = 1024

COMP_PAL, COMP_DXT, COMP_BGRA = 1, 2, 3
DXT1, DXT3, DXT5 = 0, 1, 7
# preferredFormat values share the field with the DXT flavours above, so a
# palettized file needs its own: 8 = uncompressed/palettized.
PREF_PAL = 8


class Blp:
    def __init__(self, data):
        if data[:4] != b"BLP2":
            raise ValueError("not a BLP2 file")
        (self.version, self.compression, self.alphaDepth, self.alphaType,
         self.hasMips, self.width, self.height) = struct.unpack_from("<IBBBBII", data, 4)
        self.mipOffsets = list(struct.unpack_from("<16I", data, 20))
        self.mipSizes = list(struct.unpack_from("<16I", data, 84))
        self.data = data

    def __repr__(self):
        kind = {1: "palettized", 2: "DXT%d" % {0: 1, 1: 3, 7: 5}.get(self.alphaType, 0),
                3: "bgra"}.get(self.compression, self.compression)
        return "<BLP %dx%d %s alpha=%d mips=%d>" % (
            self.width, self.height, kind, self.alphaDepth, self.hasMips)

    # -- palette -------------------------------------------------------------

    def palette(self):
        """256 (r,g,b,a) tuples. Stored BGRA; the 4th byte is padding, not alpha."""
        raw = self.data[HEADER:HEADER + PALETTE]
        return [(raw[i * 4 + 2], raw[i * 4 + 1], raw[i * 4 + 0], raw[i * 4 + 3])
                for i in range(256)]

    def with_palette(self, pal):
        """Copy of this file with a new palette; pixel data untouched (lossless)."""
        if self.compression != COMP_PAL:
            raise ValueError("not a palettized BLP")
        out = bytearray(self.data)
        for i, (r, g, b, x) in enumerate(pal):
            out[HEADER + i * 4 + 0] = b & 0xFF
            out[HEADER + i * 4 + 1] = g & 0xFF
            out[HEADER + i * 4 + 2] = r & 0xFF
            out[HEADER + i * 4 + 3] = x & 0xFF
        return bytes(out)

    # -- decode --------------------------------------------------------------

    def image(self, mip=0):
        """Mip level as an RGBA PIL image."""
        off, size = self.mipOffsets[mip], self.mipSizes[mip]
        w, h = max(1, self.width >> mip), max(1, self.height >> mip)
        chunk = self.data[off:off + size]

        if self.compression == COMP_PAL:
            pal = self.palette()
            idx = chunk[:w * h]
            px = bytearray(w * h * 4)
            for i, v in enumerate(idx):
                r, g, b, _ = pal[v]
                px[i * 4:i * 4 + 4] = bytes((r, g, b, 255))
            if self.alphaDepth == 8:
                alpha = chunk[w * h:w * h * 2]
                for i, a in enumerate(alpha):
                    px[i * 4 + 3] = a
            elif self.alphaDepth == 1:
                bits = chunk[w * h:]
                for i in range(w * h):
                    px[i * 4 + 3] = 255 if (bits[i >> 3] >> (i & 7)) & 1 else 0
            return Image.frombytes("RGBA", (w, h), bytes(px))

        if self.compression == COMP_DXT:
            fourcc = {DXT1: b"DXT1", DXT3: b"DXT3", DXT5: b"DXT5"}[self.alphaType]
            return Image.open(io.BytesIO(_dds(chunk, w, h, fourcc))).convert("RGBA")

        if self.compression == COMP_BGRA:
            im = Image.frombytes("RGBA", (w, h), chunk[:w * h * 4])
            b, g, r, a = im.split()
            return Image.merge("RGBA", (r, g, b, a))

        raise ValueError("unsupported compression %d" % self.compression)


def _dds(block_data, w, h, fourcc):
    """Wrap raw DXT blocks in a DDS container so Pillow can decode them."""
    hdr = bytearray(128)
    hdr[0:4] = b"DDS "
    struct.pack_into("<I", hdr, 4, 124)
    struct.pack_into("<I", hdr, 8, 0x1 | 0x2 | 0x4 | 0x1000 | 0x80000)  # caps|h|w|pixelformat|linearsize
    struct.pack_into("<I", hdr, 12, h)
    struct.pack_into("<I", hdr, 16, w)
    struct.pack_into("<I", hdr, 20, len(block_data))
    struct.pack_into("<I", hdr, 76, 32)      # pixelformat size
    struct.pack_into("<I", hdr, 80, 0x4)     # DDPF_FOURCC
    hdr[84:88] = fourcc
    struct.pack_into("<I", hdr, 108, 0x1000)  # DDSCAPS_TEXTURE
    return bytes(hdr) + block_data


# -- encode ------------------------------------------------------------------

def _mips(im):
    out, w, h = [im], im.width, im.height
    while w > 1 or h > 1:
        w, h = max(1, w // 2), max(1, h // 2)
        out.append(out[-1].resize((w, h), Image.LANCZOS))
    return out


def write_palettized(im, keep_alpha=True):
    """Encode an RGBA image as a palettized BLP2 with a full mip chain."""
    im = im.convert("RGBA")
    alpha = im.getchannel("A")
    has_alpha = keep_alpha and alpha.getextrema()[0] < 255

    quant = im.convert("RGB").quantize(colors=256, method=Image.MEDIANCUT)
    pal = quant.getpalette()[:768]
    pal += [0] * (768 - len(pal))

    levels = _mips(im)
    body, offsets, sizes = b"", [0] * 16, [0] * 16
    base = HEADER + PALETTE
    for i, lv in enumerate(levels[:16]):
        q = lv.convert("RGB").quantize(palette=quant, dither=Image.NONE)
        chunk = q.tobytes()
        if has_alpha:
            chunk += lv.getchannel("A").resize(lv.size).tobytes()
        offsets[i] = base + len(body)
        sizes[i] = len(chunk)
        body += chunk

    hdr = bytearray(HEADER)
    hdr[0:4] = b"BLP2"
    # preferredFormat MUST be 8 (uncompressed/palettized). It used to be 0,
    # which means DXT1: the client read compression=1, then decoded the palette
    # indices as DXT blocks and drew the mesh missing-texture green. Object
    # components are where this showed - shoulders are palettized, helms are
    # DXT1 (where 0 is correct), so helms rendered and pauldrons went green.
    struct.pack_into("<IBBBBII", hdr, 4, 1, COMP_PAL, 8 if has_alpha else 0,
                     PREF_PAL, 1 if len(levels) > 1 else 0,
                     im.width, im.height)
    struct.pack_into("<16I", hdr, 20, *offsets)
    struct.pack_into("<16I", hdr, 84, *sizes)

    palbytes = bytearray(PALETTE)
    for i in range(256):
        r, g, b = pal[i * 3], pal[i * 3 + 1], pal[i * 3 + 2]
        palbytes[i * 4 + 0] = b
        palbytes[i * 4 + 1] = g
        palbytes[i * 4 + 2] = r
        palbytes[i * 4 + 3] = 0
    return bytes(hdr) + bytes(palbytes) + body


def write_dxt(im, fmt=DXT5):
    """Encode an RGBA image as a DXT BLP2 with a full mip chain."""
    im = im.convert("RGBA")
    name = {DXT1: "DXT1", DXT3: "DXT3", DXT5: "DXT5"}[fmt]
    levels = _mips(im)
    body, offsets, sizes = b"", [0] * 16, [0] * 16
    for i, lv in enumerate(levels[:16]):
        if lv.width % 4 or lv.height % 4:      # DXT needs 4x4 blocks
            break
        buf = io.BytesIO()
        lv.save(buf, format="DDS", pixel_format=name)
        chunk = buf.getvalue()[128:]           # strip the DDS header
        offsets[i] = HEADER + len(body)
        sizes[i] = len(chunk)
        body += chunk

    hdr = bytearray(HEADER)
    hdr[0:4] = b"BLP2"
    struct.pack_into("<IBBBBII", hdr, 4, 1, COMP_DXT,
                     0 if fmt == DXT1 else 8, fmt, 1, im.width, im.height)
    struct.pack_into("<16I", hdr, 20, *offsets)
    struct.pack_into("<16I", hdr, 84, *sizes)
    return bytes(hdr) + body
