"""Find a kit piece's sockets: a top view of a WMO's floors, and floor heights walking out of one of its portals.
Reads the client's archives read-only.

  python3 -I wmofloor.py <wmo> --png FILE [--scale S]  floor map (S px per yard, default 8; north up, 10 yd grid)
  python3 -I wmofloor.py <wmo> --portal N [--out D]  walk out of portal N; suggest a socket D yards out

<wmo> is the root file as the client names it, e.g. World\\wmo\\Dungeon\\MD_Goldmine\\MD_Goldmine_1Room.wmo.
A socket's facing is the portal's outward direction; put it where the floor ends (the joined copy meets it there,
mirrored, so both floors have the same height at the joint).
"""
import argparse
import math
import os
import struct
import sys
import zlib

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

import mpq  # noqa: E402

DEFAULT_CLIENT = '/Users/gaming/Desktop/World of Warcraft 3.3.5a'


def chunks(b, o=0, end=None):
    end = len(b) if end is None else end
    while o + 8 <= end:
        tag = b[o:o + 4][::-1].decode('latin1')
        n = struct.unpack_from('<I', b, o + 4)[0]
        yield tag, b[o + 8:o + 8 + n]
        o += 8 + n


def load(client, wmo, patch_name='patch-T.MPQ'):
    """(groups [(flags, collision triangles)], portals [[(x, y, z)] * 4]) of a root WMO and its group files."""
    order = mpq.client_order(client, exclude=[patch_name])
    _, root = mpq.read_effective(client, wmo, order)
    if not root:
        raise SystemExit('%s not found in the client archives' % wmo)
    parts = dict(chunks(root))
    n_groups = struct.unpack_from('<I', parts['MOHD'], 4)[0]
    pv = parts.get('MOPV', b'')
    verts = [struct.unpack_from('<3f', pv, i * 12) for i in range(len(pv) // 12)]
    pt = parts.get('MOPT', b'')
    portals = []
    for i in range(len(pt) // 20):
        start, count = struct.unpack_from('<HH', pt, i * 20)
        portals.append(verts[start:start + count])

    groups = []
    stem = wmo[:-4]
    for g in range(n_groups):
        _, raw = mpq.read_effective(client, '%s_%03d.wmo' % (stem, g), order)
        mogp = [d for t, d in chunks(raw) if t == 'MOGP'][0]
        flags = struct.unpack_from('<I', mogp, 8)[0]
        sub = dict(chunks(mogp, 68))
        v = [struct.unpack_from('<3f', sub['MOVT'], i * 12) for i in range(len(sub['MOVT']) // 12)]
        idx = struct.unpack('<%dH' % (len(sub['MOVI']) // 2), sub['MOVI'])
        mopy = sub['MOPY']
        tris = []
        for t in range(len(idx) // 3):
            f, mat = mopy[2 * t], mopy[2 * t + 1]
            if (f & 0x08) or (mat != 0xFF and not (f & 0x04)):   # collides (as the vmap extractor keeps it)
                tris.append((v[idx[3 * t]], v[idx[3 * t + 1]], v[idx[3 * t + 2]]))
        groups.append((flags, tris))
    return groups, portals


def _walkable(a, b, c):
    ux, uy, uz = b[0] - a[0], b[1] - a[1], b[2] - a[2]
    vx, vy, vz = c[0] - a[0], c[1] - a[1], c[2] - a[2]
    nx, ny, nz = uy * vz - uz * vy, uz * vx - ux * vz, ux * vy - uy * vx
    return abs(nz) / (math.sqrt(nx * nx + ny * ny + nz * nz) or 1) > 0.6


def floor_at(groups, x, y, below):
    """(height, group) of the highest walkable surface under (x, y) lower than `below`, or None."""
    best = None
    for gi, (_, tris) in enumerate(groups):
        for a, b, c in tris:
            (ax, ay, az), (bx, by, bz), (cx, cy, cz) = a, b, c
            den = (by - cy) * (ax - cx) + (cx - bx) * (ay - cy)
            if abs(den) < 1e-9:
                continue
            l1 = ((by - cy) * (x - cx) + (cx - bx) * (y - cy)) / den
            l2 = ((cy - ay) * (x - cx) + (ax - cx) * (y - cy)) / den
            l3 = 1 - l1 - l2
            if min(l1, l2, l3) < 0 or not _walkable(a, b, c):
                continue
            z = l1 * az + l2 * bz + l3 * cz
            if z < below and (best is None or z > best[0]):
                best = (z, gi)
    return best


def portal_mouth(portal, inside):
    """(centre x, y, bottom z, width, outward facing in degrees) of a portal; outward = away from `inside`."""
    low = sorted(portal, key=lambda p: p[2])[:2]
    (x0, y0, z0), (x1, y1, z1) = low
    cx, cy = (x0 + x1) / 2, (y0 + y1) / 2
    width = math.hypot(x1 - x0, y1 - y0)
    nx, ny = (y1 - y0) / width, -(x1 - x0) / width
    if nx * (cx - inside[0]) + ny * (cy - inside[1]) < 0:
        nx, ny = -nx, -ny
    return cx, cy, min(z0, z1), width, math.degrees(math.atan2(ny, nx))


def probe(groups, portals, n, inside, out):
    cx, cy, cz, width, facing = portal_mouth(portals[n], inside)
    print('portal %d: mouth (%.2f, %.2f) floor %.2f, %.1f yd wide, facing out %.2f deg' % (n, cx, cy, cz, width,
                                                                                            facing))
    dx, dy = math.cos(math.radians(facing)), math.sin(math.radians(facing))
    last = None
    for step in range(0, 61):
        s = step * 0.5
        f = floor_at(groups, cx + dx * s, cy + dy * s, cz + 4.0)
        if f and step % 4 == 0:
            print('  %5.1f yd: floor %6.2f (group %d)' % (s, f[0], f[1]))
        if f:
            last = s
        elif last is not None and s - last > 1.5:
            break
    print('  floor ends about %.1f yd out' % (last or 0))
    at = out if out is not None else max(0.0, (last or 0) - 1.0)
    f = floor_at(groups, cx + dx * at, cy + dy * at, cz + 4.0)
    if f:
        print('socket: "local": [%.2f, %.2f, %.2f], "facing": %.2f   (%.1f yd out)'
              % (cx + dx * at, cy + dy * at, f[0], facing, at))
    else:
        print('no floor %.1f yd out' % at)


def png(groups, path, scale=8):
    pts = [v for _, tris in groups for t in tris for v in t]
    x0, x1 = min(p[0] for p in pts) - 2, max(p[0] for p in pts) + 2
    y0, y1 = min(p[1] for p in pts) - 2, max(p[1] for p in pts) + 2
    S = scale
    W, H = int((x1 - x0) * S), int((y1 - y0) * S)
    img = bytearray([24, 24, 32] * W * H)
    top = [-1e9] * (W * H)
    cols = [(230, 80, 80), (80, 200, 80), (90, 140, 255), (230, 200, 60), (200, 90, 220), (80, 210, 210)]
    for gi, (_, tris) in enumerate(groups):
        for a, b, c in tris:
            if not _walkable(a, b, c):
                continue
            px = [((p[0] - x0) * S, (y1 - p[1]) * S, p[2]) for p in (a, b, c)]
            (ax, ay, az), (bx, by, bz), (cx, cy, cz) = px
            den = (by - cy) * (ax - cx) + (cx - bx) * (ay - cy)
            if abs(den) < 1e-9:
                continue
            for yy in range(int(max(0, min(ay, by, cy))), int(min(H - 1, max(ay, by, cy))) + 1):
                for xx in range(int(max(0, min(ax, bx, cx))), int(min(W - 1, max(ax, bx, cx))) + 1):
                    l1 = ((by - cy) * (xx - cx) + (cx - bx) * (yy - cy)) / den
                    l2 = ((cy - ay) * (xx - cx) + (ax - cx) * (yy - cy)) / den
                    l3 = 1 - l1 - l2
                    if min(l1, l2, l3) < 0:
                        continue
                    z = l1 * az + l2 * bz + l3 * cz
                    k = yy * W + xx
                    if z < 6 and z > top[k]:   # floors, not roofs
                        top[k] = z
                        shade = max(0.35, min(1.0, 0.6 + z / 10))
                        r, g, bl = cols[gi % len(cols)]
                        img[3 * k:3 * k + 3] = bytes((int(r * shade), int(g * shade), int(bl * shade)))
    for gx in range(int(x0) // 10 * 10, int(x1) + 1, 10):
        X = int((gx - x0) * S)
        for yy in range(0, H, 3):
            if 0 <= X < W:
                img[3 * (yy * W + X):3 * (yy * W + X) + 3] = b'\xff\xff\xff' if gx == 0 else b'\x60\x60\x60'
    for gy in range(int(y0) // 10 * 10, int(y1) + 1, 10):
        Y = int((y1 - gy) * S)
        for xx in range(0, W, 3):
            if 0 <= Y < H:
                img[3 * (Y * W + xx):3 * (Y * W + xx) + 3] = b'\xff\xff\xff' if gy == 0 else b'\x60\x60\x60'
    raw = b''.join(b'\0' + bytes(img[y * W * 3:(y + 1) * W * 3]) for y in range(H))

    def chunk(tag, data):
        return struct.pack('>I', len(data)) + tag + data + struct.pack('>I', zlib.crc32(tag + data) & 0xffffffff)
    with open(path, 'wb') as f:
        f.write(b'\x89PNG\r\n\x1a\n' + chunk(b'IHDR', struct.pack('>IIBBBBB', W, H, 8, 2, 0, 0, 0))
                + chunk(b'IDAT', zlib.compress(raw, 9)) + chunk(b'IEND', b''))
    print('wrote %s: x %.1f..%.1f, y %.1f..%.1f, groups coloured red, green, blue, yellow, purple, cyan'
          % (path, x0, x1, y0, y1))


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('wmo')
    ap.add_argument('--client', default=DEFAULT_CLIENT)
    ap.add_argument('--png')
    ap.add_argument('--scale', type=float, default=8, help='pixels per yard (default 8)')
    ap.add_argument('--portal', type=int)
    ap.add_argument('--inside', type=float, nargs=2, default=(0.0, 0.0), metavar=('X', 'Y'),
                    help='a point inside the piece, to tell the portal\'s outward side (default 0 0)')
    ap.add_argument('--out', type=float, help='socket distance from the mouth (default: 1 yd before the floor ends)')
    args = ap.parse_args()
    groups, portals = load(args.client, args.wmo)
    for gi, (flags, tris) in enumerate(groups):
        print('group %d: flags %x (%s), %d collision triangles' % (gi, flags, 'outside' if flags & 0x8 else 'inside',
                                                                    len(tris)))
    for i, p in enumerate(portals):
        print('portal %d: %s' % (i, ' '.join('(%.1f, %.1f, %.1f)' % v for v in p)))
    if args.png:
        png(groups, args.png, args.scale)
    if args.portal is not None:
        probe(groups, portals, args.portal, args.inside, args.out)


if __name__ == '__main__':
    main()
