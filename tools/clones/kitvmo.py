"""Server collision for Atlas kit pieces: the model file (vmaps/<name>.vmo) and the GameObjectModels.dtree entry
the worldserver reads for a gameobject display (AzerothCore src/common/Collision: WorldModel::readFile,
GroupModel::readFromFile, BIH::readFromFile, LoadGameObjectModelList). Little-endian, no padding.

  .vmo   "VMAP_4.8" "WMOD" u32 8, u32 rootWMOID, "GMOD" u32 groups, groups, "GBIH" BIH(group indices)
  group  AABox(lo, hi), u32 mogpFlags, u32 groupWMOID, "VERT" u32 size u32 n Vector3[n],
         "TRIM" u32 size u32 m u32[3][m], "MBIH" BIH(triangle indices), "LIQU" u32 0 (no liquid)
  BIH    f32 lo[3] hi[3], u32 treeSize, u32 tree[treeSize], u32 count, u32 objects[count]
         one leaf holding everything: tree = [0xC0000000 | first, count, 0] (what AC's own builder writes for small
         sets; traversal tests every object of the leaf)
  dtree  "VMAP_4.8" then entries to EOF: u32 displayId, u8 isWmo, u32 nameLen, name (no NUL), f32 lo[3], hi[3]

Vertices are the WMO's own model coordinates (the server maps world points into them with the gameobject's
position, orientation and scale). Group flags 0x8: outdoors (mounting allowed, area from the map).
"""
import struct

VMAP_MAGIC = b'VMAP_4.8'
LEAF = 0xC0000000
MOGP_OUTDOORS = 0x8


def model_name(spec):
    """The dtree name of a piece: its WMO file name with extension (the .vmo is <name>.vmo)."""
    return spec['file'] + '.wmo'


def _bih(lo, hi, count):
    return (struct.pack('<3f3f', *lo, *hi) + struct.pack('<I', 3) + struct.pack('<3I', LEAF, count, 0)
            + struct.pack('<I', count) + b''.join(struct.pack('<I', i) for i in range(count)))


def bounds(verts):
    lo = tuple(min(v[i] for v in verts) for i in range(3))
    hi = tuple(max(v[i] for v in verts) for i in range(3))
    return lo, hi


def vmo(verts, tris):
    """The .vmo bytes of a one-group model from vertices and (i0, i1, i2[, material]) triangles."""
    if not verts or not tris:
        raise SystemExit('a kit piece needs geometry')
    lo, hi = bounds(verts)
    vert = struct.pack('<I', len(verts)) + b''.join(struct.pack('<3f', *v) for v in verts)
    trim = struct.pack('<I', len(tris)) + b''.join(struct.pack('<3I', *t[:3]) for t in tris)
    group = (struct.pack('<3f3f', *lo, *hi) + struct.pack('<II', MOGP_OUTDOORS, 0)
             + b'VERT' + struct.pack('<I', len(vert)) + vert
             + b'TRIM' + struct.pack('<I', len(trim)) + trim
             + b'MBIH' + _bih(lo, hi, len(tris))
             + b'LIQU' + struct.pack('<I', 0))
    return (VMAP_MAGIC + b'WMOD' + struct.pack('<II', 8, 0) + b'GMOD' + struct.pack('<I', 1) + group
            + b'GBIH' + _bih(lo, hi, 1))


def dtree_entry(display, name, verts):
    lo, hi = bounds(verts)
    raw = name.encode('ascii')
    return struct.pack('<IBI', display, 1, len(raw)) + raw + struct.pack('<3f3f', *lo, *hi)


def read_dtree(raw):
    """[(displayId, isWmo, name, lo, hi, entry bytes)] of a GameObjectModels.dtree."""
    if raw[:8] != VMAP_MAGIC:
        raise SystemExit('not a GameObjectModels.dtree (magic %r)' % raw[:8])
    out, o = [], 8
    while o < len(raw):
        start = o
        did, wmo, n = struct.unpack_from('<IBI', raw, o)
        o += 9
        name = raw[o:o + n].decode('latin1')
        o += n
        lo, hi = struct.unpack_from('<3f', raw, o), struct.unpack_from('<3f', raw, o + 12)
        o += 24
        if o > len(raw):
            raise SystemExit('GameObjectModels.dtree ends inside an entry')
        out.append((did, wmo, name, lo, hi, raw[start:o]))
    return out


def merge_dtree(stock, entries, reserved):
    """`stock` without any entry in `reserved` (display id range), then `entries` (bytes each)."""
    keep = [e[5] for e in read_dtree(stock) if not reserved[0] <= e[0] <= reserved[1]]
    return VMAP_MAGIC + b''.join(keep) + b''.join(entries)


def read_vmo(raw):
    """(lo, hi, mogp, verts, tris) of a one-group .vmo written by vmo(): a self-check of the layout."""
    assert raw[:8] == VMAP_MAGIC and raw[8:12] == b'WMOD' and raw[20:24] == b'GMOD'
    o = 24
    groups, = struct.unpack_from('<I', raw, o)
    o += 4
    assert groups == 1
    lo, hi = struct.unpack_from('<3f', raw, o), struct.unpack_from('<3f', raw, o + 12)
    mogp, _ = struct.unpack_from('<II', raw, o + 24)
    o += 32
    assert raw[o:o + 4] == b'VERT'
    n, = struct.unpack_from('<I', raw, o + 8)
    o += 12
    verts = [struct.unpack_from('<3f', raw, o + 12 * i) for i in range(n)]
    o += 12 * n
    assert raw[o:o + 4] == b'TRIM'
    m, = struct.unpack_from('<I', raw, o + 8)
    o += 12
    tris = [struct.unpack_from('<3I', raw, o + 12 * i) for i in range(m)]
    o += 12 * m
    assert raw[o:o + 4] == b'MBIH'
    o += 4 + 24
    tsize, = struct.unpack_from('<I', raw, o)
    tree = struct.unpack_from('<%dI' % tsize, raw, o + 4)
    o += 4 + 4 * tsize
    count, = struct.unpack_from('<I', raw, o)
    o += 4 + 4 * count
    assert tree == (LEAF, m, 0) and count == m
    assert raw[o:o + 8] == b'LIQU' + b'\0\0\0\0'
    o += 8
    assert raw[o:o + 4] == b'GBIH'
    o += 4 + 24 + 4 + 12 + 4 + 4
    assert o == len(raw), (o, len(raw))
    assert all(max(t) < n for t in tris)
    return lo, hi, mogp, verts, tris
