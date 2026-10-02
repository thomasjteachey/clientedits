"""Minimal MPQ v1 reader: hash-table presence checks + zlib/bzip2 extraction."""
import bz2
import struct
import zlib

# standard MPQ crypt table
CRYPT = []
def _init_crypt():
    seed = 0x00100001
    tbl = [0] * (0x500)
    for i in range(0x100):
        idx = i
        for j in range(5):
            seed = (seed * 125 + 3) % 0x2AAAAB
            t1 = (seed & 0xFFFF) << 0x10
            seed = (seed * 125 + 3) % 0x2AAAAB
            t2 = seed & 0xFFFF
            tbl[idx] = t1 | t2
            idx += 0x100
    CRYPT.extend(tbl)
_init_crypt()

def hash_string(s, htype):
    seed1, seed2 = 0x7FED7FED, 0xEEEEEEEE
    for ch in s.upper().replace("/", "\\"):
        c = ord(ch)
        seed1 = (CRYPT[(htype << 8) + c] ^ (seed1 + seed2)) & 0xFFFFFFFF
        seed2 = (c + seed1 + seed2 + (seed2 << 5) + 3) & 0xFFFFFFFF
    return seed1

def decrypt(data, key):
    out = bytearray()
    seed2 = 0xEEEEEEEE
    for i in range(0, len(data) - len(data) % 4, 4):
        seed2 = (seed2 + CRYPT[0x400 + (key & 0xFF)]) & 0xFFFFFFFF
        v = struct.unpack_from("<I", data, i)[0]
        v = v ^ ((key + seed2) & 0xFFFFFFFF)
        out += struct.pack("<I", v)
        key = (((~key << 0x15) + 0x11111111) | (key >> 0x0B)) & 0xFFFFFFFF
        seed2 = (v + seed2 + (seed2 << 5) + 3) & 0xFFFFFFFF
    return bytes(out)

class MPQ:
    def __init__(self, path):
        self.f = open(path, "rb")
        data = self.f.read(0x200)
        ofs = 0
        while data[:4] != b"MPQ\x1a":
            ofs += 0x200
            self.f.seek(ofs)
            data = self.f.read(0x200)
            if not data:
                raise ValueError("no MPQ header")
        self.base = ofs
        (self.hdr_size, self.arch_size, self.fmt, self.block_shift,
         self.ht_pos, self.bt_pos, self.ht_size, self.bt_size) = struct.unpack_from("<IIHHIIII", data, 4)
        self.sector_size = 512 << self.block_shift
        self.f.seek(self.base + self.ht_pos)
        ht_raw = self.f.read(self.ht_size * 16)
        self.ht = decrypt(ht_raw, hash_string("(hash table)", 3))
        self.f.seek(self.base + self.bt_pos)
        bt_raw = self.f.read(self.bt_size * 16)
        self.bt = decrypt(bt_raw, hash_string("(block table)", 3))

    def find(self, name):
        h_ofs = hash_string(name, 0) & (self.ht_size - 1)
        h_a, h_b = hash_string(name, 1), hash_string(name, 2)
        i = h_ofs
        while True:
            a, b, locale_platform, block = struct.unpack_from("<IIII", self.ht, i * 16)
            if block == 0xFFFFFFFF:
                return None
            if a == h_a and b == h_b and block != 0xFFFFFFFE:
                return block
            i = (i + 1) & (self.ht_size - 1)
            if i == h_ofs:
                return None

    def block(self, idx):
        ofs, csize, fsize, flags = struct.unpack_from("<IIII", self.bt, idx * 16)
        return ofs, csize, fsize, flags

    def n_files(self):
        n = 0
        for i in range(self.bt_size):
            if self.block(i)[3] & 0x80000000:
                n += 1
        return n

    def extract(self, name):
        idx = self.find(name)
        if idx is None:
            return None
        ofs, csize, fsize, flags = self.block(idx)
        assert flags & 0x80000000, "file deleted"
        assert not flags & 0x10000, "encrypted (unsupported here): " + name
        self.f.seek(self.base + ofs)
        raw = self.f.read(csize)
        def decomp(chunk, expected):
            if len(chunk) == expected:
                return chunk
            method = chunk[0]
            body = chunk[1:]
            if method == 0x02:
                return zlib.decompress(body)
            if method == 0x10:
                return bz2.decompress(body)
            raise ValueError("compression 0x%02X unsupported" % method)
        if not flags & 0x300:  # uncompressed
            return raw[:fsize]
        if flags & 0x1000000:  # single unit
            return decomp(raw, fsize)
        n_sect = (fsize + self.sector_size - 1) // self.sector_size
        offs = struct.unpack_from("<%dI" % (n_sect + 1), raw, 0)
        out = bytearray()
        for s in range(n_sect):
            expected = min(self.sector_size, fsize - s * self.sector_size)
            out += decomp(raw[offs[s]:offs[s + 1]], expected)
        return bytes(out[:fsize])

if __name__ == "__main__":
    import sys
    m = MPQ(sys.argv[1])
    print("hash table %d entries, block table %d, %d live files, sector %d" %
          (m.ht_size, m.bt_size, m.n_files(), m.sector_size))
    for name in sys.argv[2:]:
        idx = m.find(name)
        if idx is None:
            print("  MISSING %s" % name)
        else:
            ofs, csize, fsize, flags = m.block(idx)
            print("  FOUND   %-45s fsize=%-9d flags=0x%08X" % (name, fsize, flags))
