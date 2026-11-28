# Minimal read-only MPQ (v1/v2) reader: pulls DBC files out of the 3.3.5a
# client archives. Opens archives read-only; never writes to them.
#   python3 -I mpq.py <client dir> <out dir> Map.dbc Light.dbc ...
import struct, zlib, bz2, sys, os


def _init():
    seed = 0x00100001
    table = [0] * 0x500
    for i in range(0x100):
        idx = i
        for _ in range(5):
            seed = (seed * 125 + 3) % 0x2AAAAB
            t1 = (seed & 0xFFFF) << 16
            seed = (seed * 125 + 3) % 0x2AAAAB
            t2 = seed & 0xFFFF
            table[idx] = t1 | t2
            idx += 0x100
    return table


CRYPT = _init()


def hash_string(s, t):
    s1, s2 = 0x7FED7FED, 0xEEEEEEEE
    for ch in s.upper().replace('/', '\\'):
        c = ord(ch)
        s1 = CRYPT[(t << 8) + c] ^ ((s1 + s2) & 0xFFFFFFFF)
        s2 = (c + s1 + s2 + (s2 << 5) + 3) & 0xFFFFFFFF
    return s1


def decrypt(data, key):
    out = bytearray()
    seed = 0xEEEEEEEE
    for i in range(len(data) // 4):
        seed = (seed + CRYPT[0x400 + (key & 0xFF)]) & 0xFFFFFFFF
        v = struct.unpack_from('<I', data, i * 4)[0]
        v = (v ^ (key + seed)) & 0xFFFFFFFF
        key = ((((~key) << 0x15) + 0x11111111) | (key >> 0x0B)) & 0xFFFFFFFF
        seed = (v + seed + (seed << 5) + 3) & 0xFFFFFFFF
        out += struct.pack('<I', v)
    return bytes(out)


def encrypt(data, key):
    out = bytearray()
    seed = 0xEEEEEEEE
    for i in range(len(data) // 4):
        seed = (seed + CRYPT[0x400 + (key & 0xFF)]) & 0xFFFFFFFF
        v = struct.unpack_from('<I', data, i * 4)[0]
        out += struct.pack('<I', (v ^ (key + seed)) & 0xFFFFFFFF)
        key = ((((~key) << 0x15) + 0x11111111) | (key >> 0x0B)) & 0xFFFFFFFF
        seed = (v + seed + (seed << 5) + 3) & 0xFFFFFFFF
    return bytes(out)


class MPQ:
    def __init__(self, path):
        self.f = open(path, 'rb')
        data = self.f.read(64)
        off = 0
        while data[:4] != b'MPQ\x1a':
            off += 512
            self.f.seek(off)
            data = self.f.read(64)
            if not data:
                raise ValueError('not an MPQ: ' + path)
        self.base = off
        (_, hsize, _, ver, shift, hpos, bpos, hcount, bcount) = struct.unpack_from('<4sIIHHIIII', data)
        self.sector = 512 << shift
        hi_h = hi_b = 0
        if ver >= 1:
            _, hi_h, hi_b = struct.unpack_from('<QHH', data, 32)
        self.f.seek(self.base + (hi_h << 32) + hpos)
        ht = decrypt(self.f.read(hcount * 16), hash_string('(hash table)', 3))
        self.hashes = [struct.unpack_from('<IIHHI', ht, i * 16) for i in range(hcount)]
        self.f.seek(self.base + (hi_b << 32) + bpos)
        bt = decrypt(self.f.read(bcount * 16), hash_string('(block table)', 3))
        self.blocks = [struct.unpack_from('<IIII', bt, i * 16) for i in range(bcount)]

    def find(self, name):
        n = len(self.hashes)
        i = hash_string(name, 0) % n
        a, b = hash_string(name, 1), hash_string(name, 2)
        for _ in range(n):
            h1, h2, loc, plat, blk = self.hashes[i]
            if blk == 0xFFFFFFFF:
                return None
            if h1 == a and h2 == b and blk != 0xFFFFFFFE:
                return self.blocks[blk]
            i = (i + 1) % n
        return None

    def read(self, name):
        blk = self.find(name)
        if not blk:
            return None
        pos, packed, size, flags = blk
        if not flags & 0x80000000:
            return None
        if flags & 0x10000:
            raise ValueError('encrypted file not supported: ' + name)
        self.f.seek(self.base + pos)
        raw = self.f.read(packed)
        if flags & 0x01000000:
            return self._unpack(raw, size, flags)
        if not flags & 0x0000FF00:
            return raw[:size]
        nsec = (size + self.sector - 1) // self.sector
        offs = struct.unpack_from('<%dI' % (nsec + 1), raw)
        out = bytearray()
        for i in range(nsec):
            chunk = raw[offs[i]:offs[i + 1]]
            want = min(self.sector, size - i * self.sector)
            out += self._unpack(chunk, want, flags)
        return bytes(out)

    def _unpack(self, chunk, want, flags):
        if len(chunk) >= want:
            return chunk[:want]
        if flags & 0x200:
            mask, body = chunk[0], chunk[1:]
            if mask == 0x02:
                return zlib.decompress(body)
            if mask == 0x10:
                return bz2.decompress(body)
            raise ValueError('compression %#x not supported' % mask)
        raise ValueError('imploded data not supported')


# The 3.3.5a client's load order, lowest priority first (later archives win).
# Custom patches (patch-X with a letter) load after the numbered ones.
ORDER = ['common.MPQ', 'common-2.MPQ', 'expansion.MPQ', 'lichking.MPQ',
         'enUS/locale-enUS.MPQ', 'enUS/expansion-locale-enUS.MPQ', 'enUS/lichking-locale-enUS.MPQ',
         'patch.MPQ', 'patch-2.MPQ', 'patch-3.MPQ', 'enUS/patch-enUS.MPQ', 'enUS/patch-enUS-2.MPQ',
         'enUS/patch-enUS-3.MPQ']


def extract(client, names, out, order=ORDER):
    data = os.path.join(client, 'Data')
    found = {}
    for arch in order:
        p = os.path.join(data, arch)
        if not os.path.exists(p):
            continue
        m = MPQ(p)
        for n in names:
            d = m.read('DBFilesClient\\' + n)
            if d:
                found[n] = (arch, d)
    os.makedirs(out, exist_ok=True)
    for n, (arch, d) in found.items():
        open(os.path.join(out, n), 'wb').write(d)
        print(n, len(d), 'from', arch)
    return found


if __name__ == '__main__':
    extract(sys.argv[1], sys.argv[3:], sys.argv[2])
