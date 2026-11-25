"""Atlas kit pieces as WMO files (3.3.5a, v17): the geometry of each piece shape (kit.json), and the root and group
files the client reads (layout per Wowdecomp2 MapObjRead.cpp, CMapObj::Load / CMapObjGroup::CreateDataPointers).

Every piece is one exterior group (flag 0x8: a gameobject's interior groups show only through portals, so a
doorway into the next piece would look into black) with a client collision tree (MOBN/MOBR, flag 0x1). Each face
is emitted once, its normal towards the inside of the piece, and every material is unculled (MOMT flag 0x4) so a
face shows from both sides (two faces on one plane z-fight). The group's liquid is 15 = none: without MOHD flag 0x4
the client reads GetLiquidType(groupLiquid + 1), so 0 would fill the piece with water (MapObjRead.cpp).
Pieces share one doorway (DOOR_W x DOOR_H, floor at z = 0); a socket sits on the floor at the middle of each
opening, facing out.
"""
import json
import math
import os
import struct

HERE = os.path.dirname(os.path.abspath(__file__))

DOOR_W = 8.0
DOOR_H = 7.0
UV_SCALE = 1.0 / 6.0          # texture repeats every 6 yd
BSP_LEAF = 32                 # faces per leaf (the client caches at most 1024)
WMO_DIR = 'World\\wmo\\Dungeon\\AtlasKit'
FACE_RENDER = 0x20            # MOPY: rendered (and collides)
MAT_UNCULLED = 0x4            # MOMT: drawn from both sides
NO_LIQUID = 15                # MOGP groupLiquid: none (see above)

# Materials (the Goldmine room's own textures, all stock).
MATERIALS = [
    ('DUNGEONS\\TEXTURES\\ROCK\\MM_CAVEWALL_08.BLP', 10, 0xff7f7f7f),    # 0 walls
    ('DUNGEONS\\TEXTURES\\FLOOR\\MM_CAVEFLOOR_03.BLP', 2, 0xff959595),   # 1 floor
    ('DUNGEONS\\TEXTURES\\ROCK\\MM_CAVEWALL_09.BLP', 10, 0xff7f7f7f),    # 2 ceiling
    ('DUNGEONS\\TEXTURES\\WOOD\\MM_MINEBEAM_01.BLP', 10, 0xff1baba6),    # 3 beams
]
WALL, FLOOR, CEIL, BEAM = range(4)


def load_kit(path=None):
    with open(path or os.path.join(HERE, 'kit.json')) as f:
        return json.load(f)


class Mesh:
    """Triangles with per-face vertices (flat normals), material ids and planar texture coordinates."""

    def __init__(self, inside=None):
        self.verts, self.normals, self.uvs, self.tris = [], [], [], []
        self.inside = inside      # a point inside the piece: normals face it

    def quad(self, a, b, c, d, mat):
        """A planar quad a-b-c-d, once, its normal towards `inside` (when set)."""
        n = _normal(a, b, c)
        if self.inside is not None:
            centre = [sum(p[i] for p in (a, b, c, d)) / 4 for i in range(3)]
            if sum(n[i] * (self.inside[i] - centre[i]) for i in range(3)) < 0:
                a, b, c, d = d, c, b, a
                n = tuple(-x for x in n)
        base = len(self.verts)
        for p in (a, b, c, d):
            self.verts.append(p)
            self.normals.append(n)
            self.uvs.append(_uv(p, n))
        self.tris.append((base, base + 1, base + 2, mat))
        self.tris.append((base, base + 2, base + 3, mat))

    def box_wall(self, x0, y0, x1, y1, z0, z1, mat):
        """A vertical quad from (x0, y0) to (x1, y1), z0 to z1."""
        self.quad((x0, y0, z0), (x1, y1, z0), (x1, y1, z1), (x0, y0, z1), mat)

    def flat(self, x0, y0, x1, y1, z, mat):
        self.quad((x0, y0, z), (x1, y0, z), (x1, y1, z), (x0, y1, z), mat)

    def bounds(self):
        return tuple(min(v[i] for v in self.verts) for i in range(3)) + tuple(max(v[i] for v in self.verts)
                                                                           for i in range(3))


def _normal(a, b, c):
    ux, uy, uz = b[0] - a[0], b[1] - a[1], b[2] - a[2]
    vx, vy, vz = c[0] - a[0], c[1] - a[1], c[2] - a[2]
    n = (uy * vz - uz * vy, uz * vx - ux * vz, ux * vy - uy * vx)
    ln = math.sqrt(sum(x * x for x in n)) or 1.0
    return tuple(x / ln for x in n)


def _uv(p, n):
    ax = max(range(3), key=lambda i: abs(n[i]))
    if ax == 2:
        return p[0] * UV_SCALE, p[1] * UV_SCALE
    if ax == 0:
        return p[1] * UV_SCALE, -p[2] * UV_SCALE
    return p[0] * UV_SCALE, -p[2] * UV_SCALE


# ---- piece shapes ------------------------------------------------------------------------------------------------

def _door_wall(mesh, side, half, height, door):
    """One wall of a square room of half-size `half`: solid, or with a doorway in the middle."""
    hw = DOOR_W / 2
    # the wall runs along t from -half to +half; (x, y) of a point at t on this side
    def at(t):
        return {'E': (half, t), 'W': (-half, -t), 'N': (-t, half), 'S': (t, -half)}[side]
    if not door:
        (x0, y0), (x1, y1) = at(-half), at(half)
        mesh.box_wall(x0, y0, x1, y1, 0.0, height, WALL)
        return
    for t0, t1 in ((-half, -hw), (hw, half)):
        (x0, y0), (x1, y1) = at(t0), at(t1)
        mesh.box_wall(x0, y0, x1, y1, 0.0, height, WALL)
    (x0, y0), (x1, y1) = at(-hw), at(hw)
    mesh.box_wall(x0, y0, x1, y1, DOOR_H, height, WALL)            # lintel


SIDES = {'E': (1, 0, 0.0), 'N': (0, 1, 90.0), 'W': (-1, 0, 180.0), 'S': (0, -1, 270.0)}


def build_room(size, height, doors):
    half = size / 2.0
    m = Mesh(inside=(0.0, 0.0, height / 2))
    m.flat(-half, -half, half, half, 0.0, FLOOR)
    m.flat(-half, -half, half, half, height, CEIL)
    for side in 'ENWS':
        _door_wall(m, side, half, height, side in doors)
    sockets = [{'local': [SIDES[s][0] * half, SIDES[s][1] * half, 0.0], 'facing': SIDES[s][2]} for s in 'ENWS'
               if s in doors]
    return m, sockets, [0.0, 0.0, 0.1]


def build_corridor(length):
    hl, hw = length / 2.0, DOOR_W / 2
    m = Mesh(inside=(0.0, 0.0, DOOR_H / 2))
    m.flat(-hl, -hw, hl, hw, 0.0, FLOOR)
    m.flat(-hl, -hw, hl, hw, DOOR_H, CEIL)
    m.box_wall(-hl, hw, hl, hw, 0.0, DOOR_H, WALL)
    m.box_wall(-hl, -hw, hl, -hw, 0.0, DOOR_H, WALL)
    # a beam frame across the middle, for scale
    for y in (-hw + 0.3, hw - 0.3):
        m.box_wall(-0.3, y, 0.3, y, 0.0, DOOR_H, BEAM)
    m.box_wall(-0.3, -hw, -0.3, hw, DOOR_H - 0.6, DOOR_H, BEAM)
    sockets = [{'local': [hl, 0.0, 0.0], 'facing': 0.0}, {'local': [-hl, 0.0, 0.0], 'facing': 180.0}]
    return m, sockets, [0.0, 0.0, 0.1]


def build_cap():
    """A slab that closes a doorway left open: its socket faces -x, the slab stands just inside it."""
    hw = DOOR_W / 2 + 0.5
    m = Mesh()
    for x in (0.2, 0.8):                     # two faces 0.6 yd apart: a slab with some thickness
        m.box_wall(x, -hw, x, hw, -0.5, DOOR_H + 0.5, WALL)
    return m, [{'local': [0.0, 0.0, 0.0], 'facing': 180.0}], [-2.0, 0.0, 0.1]


def build_piece(spec):
    shape = spec['shape']
    if shape == 'room':
        return build_room(spec['size'], spec['height'], spec['doors'])
    if shape == 'corridor':
        return build_corridor(spec['length'])
    if shape == 'cap':
        return build_cap()
    raise SystemExit('unknown kit shape %s' % shape)


# ---- WMO files ---------------------------------------------------------------------------------------------------

def _chunk(tag, data):
    return tag[::-1].encode('ascii') + struct.pack('<I', len(data)) + data


def _strings(items):
    """A block of NUL-terminated strings, each starting 4-aligned. Returns (block, offsets)."""
    block, offsets = b'', []
    for s in items:
        offsets.append(len(block))
        b = s.encode('ascii') + b'\0'
        block += b + b'\0' * ((-len(b)) % 4)
    return block, offsets


def _bsp(tris, verts):
    """Client collision tree (CAaBspNode list, face index list): split on the longest axis at the median."""
    nodes, faces = [], []
    boxes = []
    for i0, i1, i2, _ in tris:
        p = (verts[i0], verts[i1], verts[i2])
        boxes.append((tuple(min(q[a] for q in p) for a in range(3)), tuple(max(q[a] for q in p) for a in range(3))))

    def build(ids, depth):
        idx = len(nodes)
        nodes.append(None)
        lo = [min(boxes[i][0][a] for i in ids) for a in range(3)]
        hi = [max(boxes[i][1][a] for i in ids) for a in range(3)]
        if len(ids) > BSP_LEAF and depth < 24:
            axis = max(range(3), key=lambda a: hi[a] - lo[a])
            mids = sorted((boxes[i][0][axis] + boxes[i][1][axis]) / 2 for i in ids)
            dist = mids[len(mids) // 2]
            neg = [i for i in ids if boxes[i][0][axis] < dist]
            pos = [i for i in ids if boxes[i][1][axis] > dist]
            if neg and pos and len(neg) < len(ids) and len(pos) < len(ids):
                n = build(neg, depth + 1)
                p = build(pos, depth + 1)
                nodes[idx] = (axis, n, p, 0, 0, dist)
                return idx
        nodes[idx] = (4, 0xFFFF, 0xFFFF, len(ids), len(faces), 0.0)
        faces.extend(ids)
        return idx

    build(list(range(len(tris))), 0)
    return nodes, faces


def group_file(mesh, name_offset):
    """The group file (MVER, MOGP) of a one-group piece."""
    # Faces sorted by material, vertices re-laid per batch so each batch's vertices are contiguous.
    order = sorted(range(len(mesh.tris)), key=lambda t: mesh.tris[t][3])
    verts, normals, uvs, tris, batches = [], [], [], [], []
    remap = {}
    for mat in sorted({t[3] for t in mesh.tris}):
        vstart, istart = len(verts), len(tris) * 3
        for t in order:
            if mesh.tris[t][3] != mat:
                continue
            new = []
            for vi in mesh.tris[t][:3]:
                if vi not in remap:
                    remap[vi] = len(verts)
                    verts.append(mesh.verts[vi])
                    normals.append(mesh.normals[vi])
                    uvs.append(mesh.uvs[vi])
                new.append(remap[vi])
            tris.append((new[0], new[1], new[2], mat))
        bv = verts[vstart:]
        lo = [math.floor(min(v[a] for v in bv)) for a in range(3)]
        hi = [math.ceil(max(v[a] for v in bv)) for a in range(3)]
        batches.append(struct.pack('<6hIHHHBB', *lo, *hi, istart, len(tris) * 3 - istart, vstart, len(verts) - 1,
                                   0, mat))
    if len(verts) > 65535:
        raise SystemExit('piece has too many vertices')

    nodes, faces = _bsp(tris, verts)
    bounds = mesh.bounds()
    header = struct.pack('<3I6f6H4B4I', name_offset, 0, 0x8 | 0x1, *bounds, 0, 0, 0, 0, len(batches), 0, 0, 0, 0, 0,
                         NO_LIQUID, 0, 0, 0)
    assert len(header) == 68
    sub = (_chunk('MOPY', b''.join(struct.pack('<BB', FACE_RENDER, t[3]) for t in tris))
           + _chunk('MOVI', b''.join(struct.pack('<3H', *t[:3]) for t in tris))
           + _chunk('MOVT', b''.join(struct.pack('<3f', *v) for v in verts))
           + _chunk('MONR', b''.join(struct.pack('<3f', *n) for n in normals))
           + _chunk('MOTV', b''.join(struct.pack('<2f', *uv) for uv in uvs))
           + _chunk('MOBA', b''.join(batches))
           + _chunk('MOBN', b''.join(struct.pack('<HHHHIf', *n) for n in nodes))
           + _chunk('MOBR', b''.join(struct.pack('<H', f) for f in faces)))
    return _chunk('MVER', struct.pack('<I', 17)) + _chunk('MOGP', header + sub), tris, verts


# The global fog every WMO carries (MFOG entry 0): flags, position, radii, then fog end / start scalar / colour,
# underwater end / start scalar / colour (as in stock WMOs).
GLOBAL_FOG = struct.pack('<I3f2f2fI2fI', 0, 0.0, 0.0, 0.0, 0.0, 0.0, 444.4445, 0.25, 0xff000000, 222.2222, -0.5,
                         0xff000000)
assert len(GLOBAL_FOG) == 48


def root_file(mesh, wmo_id, group_name):
    motx, tex_off = _strings([t for t, _, _ in MATERIALS] + [''])
    empty = tex_off[-1]
    momt = b''.join(struct.pack('<16I', MAT_UNCULLED, 0, 0, tex_off[i], 0, 0, empty, colour, ground, 0, 0, 0, 0,
                                0, 0, 0)
                    for i, (_, ground, colour) in enumerate(MATERIALS))
    mogn = b'\0\0' + group_name.encode('ascii') + b'\0'
    mogn += b'\0' * ((-len(mogn)) % 4)
    bounds = mesh.bounds()
    mohd = struct.pack('<7II I6fHH', len(MATERIALS), 1, 0, 0, 0, 0, 1, 0xff7f7f7f, wmo_id, *bounds, 0, 0)
    assert len(mohd) == 64
    mogi = struct.pack('<I6fI', 0x8 | 0x1, *bounds, 2)
    mods = b'Set_$DefaultGlobal'.ljust(20, b'\0') + struct.pack('<3I', 0, 0, 0)
    parts = [('MVER', struct.pack('<I', 17)), ('MOHD', mohd), ('MOTX', motx), ('MOMT', momt), ('MOGN', mogn),
             ('MOGI', mogi), ('MOSB', b'\0\0\0\0'), ('MOPV', b''), ('MOPT', b''), ('MOPR', b''), ('MOVV', b''),
             ('MOVB', b''), ('MOLT', b''), ('MODS', mods), ('MODN', b''), ('MODD', b''), ('MFOG', GLOBAL_FOG)]
    return b''.join(_chunk(t, d) for t, d in parts)


def wmo_path(spec):
    return '%s\\%s.wmo' % (WMO_DIR, spec['file'])


def build_files(spec):
    """{client path: bytes} of a piece's root and group file, and its data for pool.json / collision."""
    mesh, sockets, entrance = build_piece(spec)
    root = root_file(mesh, spec['wmo_id'], spec['file'])
    group, tris, verts = group_file(mesh, 2)
    path = wmo_path(spec)
    files = {path: root, path[:-4] + '_000.wmo': group}
    return files, {'sockets': sockets, 'entrance': entrance, 'bounds': mesh.bounds(), 'tris': tris, 'verts': verts}
