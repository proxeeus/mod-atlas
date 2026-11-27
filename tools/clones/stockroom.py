"""Real-room kit pieces: one group cut out of a stock dungeon WMO (Blizzard geometry, textures, lighting and
doodads), shipped as its own WMO with a generated adapter at each of its doorways (TILESET.md, "Mix and match").

The room stays an INTERIOR group, copied byte for byte (only its portal range and name are rewritten), so it keeps
its vertex lighting, lights, fog and doodads; the root keeps the source's materials, lights, doodad sets, doodads
and fogs unchanged so every index in the room's group file stays valid. Each of the room's portals (to groups we
drop) gets an EXTERIOR adapter group: a passage lofted from the portal's outline to the standard doorway (8 x 7 yd)
ADAPTER_LEN yards out, ending in a socket. The portal itself is kept and links room and adapter, so from outside
the client sees the adapter (exterior) and, through the portal, the room, as the stock mine shows its inside
through its mouth. Server collision: the room's colliding triangles (as the vmap extractor keeps them) plus the
adapters'.
"""
import math
import struct

import kitwmo
import mpq

DEFAULT_CLIENT = '/Users/gaming/Desktop/World of Warcraft 3.3.5a'
ADAPTER_LEN = 14.0         # long enough that a real room's irregular walls keep clear of its neighbours
COLLAR = 3.0               # a rock ring this wide around the adapter's room end hides the seam with the room
SHELL_DROP = 1.0           # a rock slab this far under the room's lowest point catches anyone a gap lets through
ADAPTER_TEXTURES = ['DUNGEONS\\TEXTURES\\ROCK\\MM_CAVEWALL_08.BLP', 'DUNGEONS\\TEXTURES\\FLOOR\\MM_CAVEFLOOR_03.BLP',
                    'DUNGEONS\\TEXTURES\\ROCK\\MM_CAVEWALL_09.BLP',
                    'DUNGEONS\\TEXTURES\\WOOD\\MM_MINEBEAM_01.BLP']   # wall, floor, ceiling, beam
MOGP_INDOORS = 0x2000
MOGP_OUTDOORS = 0x8


def _chunks(b, o=0, end=None):
    end = len(b) if end is None else end
    out = []
    while o + 8 <= end:
        tag = b[o:o + 4][::-1].decode('latin1')
        n = struct.unpack_from('<I', b, o + 4)[0]
        out.append((tag, b[o + 8:o + 8 + n]))
        o += 8 + n
    return out


def _chunk(tag, data):
    return tag[::-1].encode('ascii') + struct.pack('<I', len(data)) + data


def _read(client, path):
    order = mpq.client_order(client, exclude=['patch-T.MPQ'])
    _, raw = mpq.read_effective(client, path, order)
    if not raw:
        raise SystemExit('%s not found in the client archives' % path)
    return raw


def room_collision(sub):
    """(vertices, triangles) of a group's colliding faces (MOPY flag 0x08, or rendered and not detail)."""
    verts = [struct.unpack_from('<3f', sub['MOVT'], i * 12) for i in range(len(sub['MOVT']) // 12)]
    idx = struct.unpack('<%dH' % (len(sub['MOVI']) // 2), sub['MOVI'])
    mopy = sub['MOPY']
    tris = []
    for t in range(len(idx) // 3):
        f, mat = mopy[2 * t], mopy[2 * t + 1]
        if (f & 0x08) or (mat != 0xFF and not (f & 0x04)):
            tris.append((idx[3 * t], idx[3 * t + 1], idx[3 * t + 2]))
    return verts, tris


def floor_at(verts, tris, x, y, below):
    """The highest walkable surface under (x, y) lower than `below`, or None."""
    best = None
    for i0, i1, i2 in tris:
        (ax, ay, az), (bx, by, bz), (cx, cy, cz) = verts[i0], verts[i1], verts[i2]
        den = (by - cy) * (ax - cx) + (cx - bx) * (ay - cy)
        if abs(den) < 1e-9:
            continue
        l1 = ((by - cy) * (x - cx) + (cx - bx) * (y - cy)) / den
        l2 = ((cy - ay) * (x - cx) + (ax - cx) * (y - cy)) / den
        if min(l1, l2, 1 - l1 - l2) < 0:
            continue
        ux, uy, uz = bx - ax, by - ay, bz - az
        vx, vy, vz = cx - ax, cy - ay, cz - az
        nz = ux * vy - uy * vx
        if abs(nz) / (math.sqrt((uy * vz - uz * vy) ** 2 + (uz * vx - ux * vz) ** 2 + nz ** 2) or 1) < 0.6:
            continue
        z = l1 * az + l2 * bz + (1 - l1 - l2) * cz
        if z < below and (best is None or z > best):
            best = z
    return best


def door_floor(door, verts, tris):
    """The room's own floor just inside a doorway (portal outlines often reach below it), else the outline's
    bottom."""
    (dx, dy), (rx, ry), (cx, cy) = door['out'], door['right'], door['centre']
    z0, z1 = door['rect'][2], door['rect'][3]
    found = []
    for back in (1.0, 2.0):
        for side in (-1.5, 0.0, 1.5):
            z = floor_at(verts, tris, cx - dx * back + rx * side, cy - dy * back + ry * side, z0 + 3.0)
            if z is not None and z > z0 - 2.0:
                found.append(z)
    return max(found) if found else z0


def doorways(root, header, room_center):
    """The room's portals as doorways: [{portal verts, plane, side, outward (dx, dy), centre, rect}]."""
    pv = root['MOPV']
    verts = [struct.unpack_from('<3f', pv, i * 12) for i in range(len(pv) // 12)]
    pt, pr = root['MOPT'], root['MOPR']
    start, count = struct.unpack_from('<HH', header, 0x24)
    out = []
    for k in range(start, start + count):
        portal, other, side = struct.unpack_from('<HHh', pr, k * 8)
        vs, vc = struct.unpack_from('<HH', pt, portal * 20)
        plane = struct.unpack_from('<4f', pt, portal * 20 + 4)
        poly = verts[vs:vs + vc]
        cx = sum(v[0] for v in poly) / len(poly)
        cy = sum(v[1] for v in poly) / len(poly)
        nx, ny = plane[0], plane[1]
        ln = math.hypot(nx, ny)
        if ln < 0.5:
            raise SystemExit('portal %d is not a doorway (its plane is not vertical)' % portal)
        dx, dy = nx / ln, ny / ln
        if dx * (cx - room_center[0]) + dy * (cy - room_center[1]) < 0:
            dx, dy = -dx, -dy                       # outward: away from the room
        rx, ry = dy, -dx                            # right, looking out
        rs = [(v[0] - cx) * rx + (v[1] - cy) * ry for v in poly]
        zs = [v[2] for v in poly]
        out.append({'portal': portal, 'other': other, 'side': side, 'poly': poly, 'plane': plane,
                    'out': (dx, dy), 'right': (rx, ry), 'centre': (cx, cy),
                    'rect': (min(rs), max(rs), min(zs), max(zs))})
    return out


def adapter_mesh(door, mats, size=(kitwmo.DOOR_W, kitwmo.DOOR_H), length=ADAPTER_LEN, frame=True):
    """The passage from a doorway's outline to the standard doorway (`size`), `length` yards out, and its socket."""
    (dx, dy), (rx, ry), (cx, cy) = door['out'], door['right'], door['centre']
    r0, r1, _, z1 = door['rect']
    z0 = door['floor']
    z1 = max(z1, z0 + 3.0)
    ex, ey = cx + dx * length, cy + dy * length
    hw, dh = size[0] / 2, size[1]

    def p(bx, by, r, z):
        return (bx + rx * r, by + ry * r, z)
    near = [p(cx, cy, r0, z0), p(cx, cy, r1, z0), p(cx, cy, r1, z1), p(cx, cy, r0, z1)]      # bl br tr tl
    far = [p(ex, ey, -hw, z0), p(ex, ey, hw, z0), p(ex, ey, hw, z0 + dh), p(ex, ey, -hw, z0 + dh)]
    mid = ((cx + ex) / 2, (cy + ey) / 2, z0 + dh / 2)
    m = kitwmo.Mesh(inside=mid)
    wall, floor, ceil, beam = mats
    m.quad(near[0], near[1], far[1], far[0], floor)
    m.quad(near[3], near[2], far[2], far[3], ceil)
    m.quad(near[0], far[0], far[3], near[3], wall)
    m.quad(near[1], far[1], far[2], near[2], wall)
    # The collar: a rock ring around the room end, in the portal's plane, over any seam with the room's walls.
    c = COLLAR
    m.quad(p(cx, cy, r0 - c, z0), p(cx, cy, r0, z0), p(cx, cy, r0, z1 + c), p(cx, cy, r0 - c, z1 + c), wall)
    m.quad(p(cx, cy, r1, z0), p(cx, cy, r1 + c, z0), p(cx, cy, r1 + c, z1 + c), p(cx, cy, r1, z1 + c), wall)
    m.quad(p(cx, cy, r0, z1), p(cx, cy, r1, z1), p(cx, cy, r1, z1 + c), p(cx, cy, r0, z1 + c), wall)
    # A mine frame at the outer end, the way the Goldmine shores up its tunnels.
    if frame:
        kitwmo.mine_frame(m, (ex - dx * 0.6, ey - dy * 0.6), (dx, dy), z0, 0.6, beam, size)
    socket = {'local': [round(ex, 3), round(ey, 3), round(z0, 3)],
              'facing': round(math.degrees(math.atan2(dy, dx)) % 360.0, 3)}
    return m, socket


def _texture_offset(motx, name):
    """(MOTX block, offset of `name`), appending it when the source has no such texture."""
    key = name.encode('ascii').lower() + b'\0'
    i = motx.lower().find(key)
    if i >= 0 and (i == 0 or motx[i - 1:i] == b'\0'):
        return motx, i
    off = len(motx)
    raw = name.encode('ascii') + b'\0'
    return motx + raw + b'\0' * ((-len(raw)) % 4), off


def build_files(spec, client=DEFAULT_CLIENT):
    """{client path: bytes} of the piece's root and group files, and its sockets, entrance, box and collision."""
    src = spec['source']
    root_raw = _read(client, src)
    root = dict(_chunks(root_raw))
    order = [t for t, _ in _chunks(root_raw)]
    g = spec['group']
    mohd = bytearray(root['MOHD'])
    n_groups = struct.unpack_from('<I', mohd, 4)[0]
    if not 0 <= g < n_groups:
        raise SystemExit('%s has no group %d' % (src, g))
    group_raw = bytearray(_read(client, '%s_%03d.wmo' % (src[:-4], g)))
    top = _chunks(bytes(group_raw))
    if [t for t, _ in top][:2] != ['MVER', 'MOGP']:
        raise SystemExit('%s group %d: unexpected layout' % (src, g))
    mogp_at = 12 + 8                                     # MVER chunk (12 bytes), then MOGP's own header
    header = bytes(group_raw[mogp_at:mogp_at + 68])
    flags = struct.unpack_from('<I', header, 8)[0]
    if not flags & MOGP_INDOORS:
        raise SystemExit('%s group %d is not an interior room' % (src, g))
    gbox = struct.unpack_from('<6f', header, 12)
    centre = ((gbox[0] + gbox[3]) / 2, (gbox[1] + gbox[4]) / 2)
    sub = dict(_chunks(bytes(group_raw), mogp_at + 68))
    doors = doorways(root, header, centre)
    if not doors:
        raise SystemExit('%s group %d has no doorway' % (src, g))
    rverts, rtris = room_collision(sub)
    for d in doors:
        d['floor'] = door_floor(d, rverts, rtris)

    # Materials: the source's, then the adapters' (unculled copies of cave wall, floor, ceiling).
    motx = root['MOTX']
    momt = root['MOMT']
    base = len(momt) // 64
    for i, tex in enumerate(ADAPTER_TEXTURES):
        motx, off = _texture_offset(motx, tex)
        momt += struct.pack('<16I', kitwmo.MAT_UNCULLED, 0, 0, off, 0, 0, 0, 0xff7f7f7f, 10, 0, 0, 0, 0, 0, 0, 0)
    mats = tuple(base + i for i in range(len(ADAPTER_TEXTURES)))

    # Groups: 0 = the room, 1.. = one adapter per doorway. Names in MOGN.
    names = ([spec['file'] + '_room'] + ['%s_door%d' % (spec['file'], i) for i in range(len(doors))]
             + [spec['file'] + '_shell'])
    mogn, name_off = b'\0\0', []
    for n in names:
        name_off.append(len(mogn))
        mogn += n.encode('ascii') + b'\0'
    mogn += b'\0' * ((-len(mogn)) % 4)

    # Portals: the room's own, renumbered; refs: the room's first (its sides), then each adapter's (opposite side).
    mopv, mopt, mopr = b'', b'', b''
    for k, d in enumerate(doors):
        mopt += struct.pack('<HH4f', len(mopv) // 12, len(d['poly']), *d['plane'])
        mopv += b''.join(struct.pack('<3f', *v) for v in d['poly'])
    for k, d in enumerate(doors):
        mopr += struct.pack('<HHhH', k, 1 + k, d['side'], 0)
    for k, d in enumerate(doors):
        mopr += struct.pack('<HHhH', k, 0, -d['side'], 0)

    files, sockets, groups = {}, [], []
    path = kitwmo.wmo_path(spec)
    # The room group: the stock file with its name and portal range rewritten.
    struct.pack_into('<I', group_raw, mogp_at + 0, name_off[0])
    struct.pack_into('<HH', group_raw, mogp_at + 0x24, 0, len(doors))
    files[path[:-4] + '_000.wmo'] = bytes(group_raw)
    groups.append((rverts, rtris, flags & (MOGP_INDOORS | MOGP_OUTDOORS)))

    mogi = struct.pack('<I6fI', flags, *gbox, name_off[0])
    lo, hi = list(gbox[:3]), list(gbox[3:])
    for k, d in enumerate(doors):
        mesh, socket = adapter_mesh(d, mats)
        data, tris, verts = kitwmo.group_file(mesh, name_off[1 + k], len(doors) + k, 1)
        files['%s_%03d.wmo' % (path[:-4], 1 + k)] = data
        sockets.append(socket)
        groups.append((verts, tris, MOGP_OUTDOORS))
        b = mesh.bounds()
        mogi += struct.pack('<I6fI', 0x8 | 0x1, *b, name_off[1 + k])
        lo = [min(lo[i], b[i]) for i in range(3)]
        hi = [max(hi[i], b[3 + i]) for i in range(3)]

    # The shell: a rock slab under the whole room, so a gap in a cut room's floor drops nobody into the void.
    shell = kitwmo.Mesh(inside=(centre[0], centre[1], gbox[2] + 5.0))
    sz = gbox[2] - SHELL_DROP
    shell.flat(gbox[0] - 2.0, gbox[1] - 2.0, gbox[3] + 2.0, gbox[4] + 2.0, sz, mats[1])
    data, tris, verts = kitwmo.group_file(shell, name_off[-1])
    files['%s_%03d.wmo' % (path[:-4], 1 + len(doors))] = data
    groups.append((verts, tris, MOGP_OUTDOORS))
    b = shell.bounds()
    mogi += struct.pack('<I6fI', 0x8 | 0x1, *b, name_off[-1])
    lo = [min(lo[i], b[i]) for i in range(3)]
    hi = [max(hi[i], b[3 + i]) for i in range(3)]

    struct.pack_into('<I', mohd, 0, len(momt) // 64)
    struct.pack_into('<II', mohd, 4, 2 + len(doors), len(doors))
    struct.pack_into('<I', mohd, 32, spec['wmo_id'])
    struct.pack_into('<6f', mohd, 36, *lo, *hi)
    parts = {'MOHD': bytes(mohd), 'MOTX': motx, 'MOMT': momt, 'MOGN': mogn, 'MOGI': mogi, 'MOPV': mopv,
             'MOPT': mopt, 'MOPR': mopr, 'MOVV': b'', 'MOVB': b'', 'MFOG': kitwmo.with_pool_fog(root['MFOG'])}
    want = ['MVER', 'MOHD', 'MOTX', 'MOMT', 'MOGN', 'MOGI', 'MOSB', 'MOPV', 'MOPT', 'MOPR', 'MOVV', 'MOVB', 'MOLT',
            'MODS', 'MODN', 'MODD', 'MFOG']
    if order[:len(want)] != want:
        raise SystemExit('%s: unexpected root chunk order %s' % (src, order))
    files[path] = b''.join(_chunk(t, parts.get(t, root[t])) for t in want)

    floor = min(d['floor'] for d in doors)
    return files, {'sockets': sockets, 'entrance': [centre[0], centre[1], floor + 0.1],
                   'bounds': tuple(lo) + tuple(hi), 'groups': groups,
                   'verts': [v for gr in groups for v in gr[0]], 'tris': []}
