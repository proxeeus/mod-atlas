"""Minimal MPQ reader (v1/v2) and writer (v1) for the 3.3.5a client.

Reading opens archives read-only. Writing makes a format-0 (v1) archive: a 32-byte header, the files stored
uncompressed (flag MPQ_FILE_EXISTS only, so no sector offset table), then the encrypted hash and block tables, plus
a `(listfile)` so archive tools can show the names. Neutral locale, platform 0.
"""
import fnmatch
import os
import struct
import bz2
import zlib

MPQ_FILE_EXISTS = 0x80000000
MPQ_FILE_SINGLE_UNIT = 0x01000000
MPQ_FILE_ENCRYPTED = 0x00010000
MPQ_FILE_COMPRESS = 0x00000200
MPQ_FILE_COMPRESS_MASK = 0x0000FF00

HASH_FREE = 0xFFFFFFFF
HASH_DELETED = 0xFFFFFFFE


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
        self.path = path
        with open(path, 'rb') as f:
            self._open(f)

    def _open(self, f):
        data = f.read(64)
        off = 0
        while data[:4] != b'MPQ\x1a':
            off += 512
            f.seek(off)
            data = f.read(64)
            if len(data) < 32:
                raise ValueError('not an MPQ: ' + self.path)
        self.base = off
        (_, hsize, _, ver, shift, hpos, bpos, hcount, bcount) = struct.unpack_from('<4sIIHHIIII', data)
        self.version = ver
        self.sector = 512 << shift
        hi_h = hi_b = 0
        if ver >= 1:
            _, hi_h, hi_b = struct.unpack_from('<QHH', data, 32)
        f.seek(self.base + (hi_h << 32) + hpos)
        ht = decrypt(f.read(hcount * 16), hash_string('(hash table)', 3))
        self.hashes = [struct.unpack_from('<IIHHI', ht, i * 16) for i in range(hcount)]
        f.seek(self.base + (hi_b << 32) + bpos)
        bt = decrypt(f.read(bcount * 16), hash_string('(block table)', 3))
        self.blocks = [struct.unpack_from('<IIII', bt, i * 16) for i in range(bcount)]

    def find(self, name):
        n = len(self.hashes)
        i = hash_string(name, 0) % n
        a, b = hash_string(name, 1), hash_string(name, 2)
        for _ in range(n):
            h1, h2, loc, plat, blk = self.hashes[i]
            if blk == HASH_FREE:
                return None
            if h1 == a and h2 == b and blk != HASH_DELETED:
                return self.blocks[blk]
            i = (i + 1) % n
        return None

    def read(self, name):
        blk = self.find(name)
        if not blk:
            return None
        pos, packed, size, flags = blk
        if not flags & MPQ_FILE_EXISTS:
            return None
        if flags & MPQ_FILE_ENCRYPTED:
            raise ValueError('encrypted file not supported: ' + name)
        with open(self.path, 'rb') as f:
            f.seek(self.base + pos)
            raw = f.read(packed)
        if flags & MPQ_FILE_SINGLE_UNIT:
            return self._unpack(raw, size, flags)
        if not flags & MPQ_FILE_COMPRESS_MASK:
            return raw[:size]
        nsec = (size + self.sector - 1) // self.sector
        offs = struct.unpack_from('<%dI' % (nsec + 1), raw)
        out = bytearray()
        for i in range(nsec):
            chunk = raw[offs[i]:offs[i + 1]]
            want = min(self.sector, size - i * self.sector)
            out += self._unpack(chunk, want, flags)
        return bytes(out)

    def listfile(self):
        data = self.read('(listfile)')
        if not data:
            return []
        return [n for n in data.decode('utf8', 'replace').replace('\r', '\n').split('\n') if n.strip()]

    @staticmethod
    def _unpack(chunk, want, flags):
        if len(chunk) >= want:
            return chunk[:want]
        if flags & MPQ_FILE_COMPRESS:
            mask, body = chunk[0], chunk[1:]
            if mask == 0x02:
                return zlib.decompress(body)
            if mask == 0x10:
                return bz2.decompress(body)
            raise ValueError('compression %#x not supported' % mask)
        raise ValueError('imploded data not supported')


def write(path, files, listfile=True):
    """Write a v1 MPQ holding `files` ({archive name: bytes}) uncompressed. Returns the archive bytes."""
    names = list(files)
    if listfile:
        names.append('(listfile)')
        files = dict(files)
        files['(listfile)'] = ''.join(n + '\r\n' for n in names[:-1]).encode('utf8')

    hcount = 16
    while hcount < len(names) * 2:
        hcount *= 2

    body = bytearray()
    blocks = []
    pos = 32
    for n in names:
        data = files[n]
        blocks.append((pos + len(body), len(data), len(data), MPQ_FILE_EXISTS))
        body += data

    hashes = [(HASH_FREE, HASH_FREE, 0xFFFF, 0xFFFF, HASH_FREE)] * hcount
    for idx, n in enumerate(names):
        i = hash_string(n, 0) % hcount
        while hashes[i][4] != HASH_FREE:
            i = (i + 1) % hcount
        hashes[i] = (hash_string(n, 1), hash_string(n, 2), 0, 0, idx)

    ht = encrypt(b''.join(struct.pack('<IIHHI', *h) for h in hashes), hash_string('(hash table)', 3))
    bt = encrypt(b''.join(struct.pack('<IIII', *b) for b in blocks), hash_string('(block table)', 3))
    hpos = 32 + len(body)
    bpos = hpos + len(ht)
    size = bpos + len(bt)
    header = struct.pack('<4sIIHHIIII', b'MPQ\x1a', 32, size, 0, 3, hpos, bpos, hcount, len(blocks))
    out = header + bytes(body) + ht + bt
    with open(path, 'wb') as f:
        f.write(out)
    return out


# Base archives, lowest priority first (the client opens them before any patch).
BASE_ORDER = ['common.MPQ', 'common-2.MPQ', 'expansion.MPQ', 'lichking.MPQ',
              '{loc}/locale-{loc}.MPQ', '{loc}/expansion-locale-{loc}.MPQ', '{loc}/lichking-locale-{loc}.MPQ']


def _listdir_ci(folder):
    try:
        return os.listdir(folder)
    except OSError:
        return []


def client_order(client, locale='enUS', exclude=()):
    """The client's archives that exist, lowest priority first (the last one wins).

    The client lists Data/patch-?.MPQ and Data/<loc>/patch-<loc>-?.MPQ, sorts them descending by path (case
    insensitive), appends Data/patch.MPQ and Data/<loc>/patch-<loc>.MPQ, and gives the first entry the highest
    priority (Wowdecomp2 WowServices/PatchFiles.cpp, Client.cpp). Names in `exclude` (case insensitive, relative to
    Data/) are left out, e.g. the patch being rebuilt.
    """
    data = os.path.join(client, 'Data')
    skip = {e.lower().replace('\\', '/') for e in exclude}
    patches = []
    for n in _listdir_ci(data):
        if fnmatch.fnmatch(n.lower(), 'patch-?.mpq'):
            patches.append(n)
    for n in _listdir_ci(os.path.join(data, locale)):
        if fnmatch.fnmatch(n.lower(), ('patch-%s-?.mpq' % locale).lower()):
            patches.append(locale + '/' + n)
    patches.sort(key=lambda p: ('data\\' + p.replace('/', '\\')).lower(), reverse=True)
    highest_first = patches + ['patch.MPQ', '%s/patch-%s.MPQ' % (locale, locale)]
    order = [b.format(loc=locale) for b in BASE_ORDER] + list(reversed(highest_first))
    return [a for a in order if a.lower() not in skip and os.path.exists(os.path.join(data, a))]


def read_effective(client, name, order):
    """(archive, bytes) of the copy of `name` the client would use, or (None, None)."""
    data = os.path.join(client, 'Data')
    found = (None, None)
    for arch in order:
        d = MPQ(os.path.join(data, arch)).read(name)
        if d:
            found = (arch, d)
    return found
