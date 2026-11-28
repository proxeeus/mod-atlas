import sys, os, struct, math
sys.path.insert(0, '/Users/ac-playerbots/azerothcore-playerbots/azerothcore-wotlk/modules/mod-atlas/tools/clones')
import mpq
C = '/Users/gaming/Desktop/World of Warcraft 3.3.5a'
order = mpq.client_order(C, exclude=['patch-T.MPQ'])
def chunks(b, o=0, end=None):
    end = len(b) if end is None else end
    while o + 8 <= end:
        t = b[o:o+4][::-1].decode('latin1'); n = struct.unpack_from('<I', b, o+4)[0]
        yield t, b[o+8:o+8+n]; o += 8 + n
def survey(root_path):
    _, r = mpq.read_effective(C, root_path, order)
    if not r: return None
    rc = dict(chunks(r))
    ng = struct.unpack_from('<I', rc['MOHD'], 4)[0]
    pv = rc.get('MOPV', b''); verts = [struct.unpack_from('<3f', pv, i*12) for i in range(len(pv)//12)]
    pt = rc.get('MOPT', b''); portals = []
    for i in range(len(pt)//20):
        s, n = struct.unpack_from('<HH', pt, i*20); q = verts[s:s+n]
        zs = [v[2] for v in q]; h = max(zs) - min(zs)
        w = max(math.hypot(a[0]-b[0], a[1]-b[1]) for a in q for b in q)
        portals.append((round(w), round(h)))
    refs = rc.get('MOPR', b''); refs = [struct.unpack_from('<HHh', refs, i*8) for i in range(len(refs)//8)]
    mogi = rc['MOGI']; out = []
    for g in range(ng):
        fl = struct.unpack_from('<I', mogi, g*32)[0]; bb = struct.unpack_from('<6f', mogi, g*32+4)
        _, graw = mpq.read_effective(C, '%s_%03d.wmo' % (root_path[:-4], g), order)
        if not graw: continue
        mogp = [d for t, d in chunks(graw) if t == 'MOGP'][0]
        ps, pc = struct.unpack_from('<HH', mogp, 36)
        sub = dict(chunks(mogp, 68)); ntri = len(sub.get('MOVI', b''))//6
        doors = [(portals[refs[k][0]], refs[k][1]) for k in range(ps, ps+pc)]
        out.append((g, fl, [round(bb[i+3]-bb[i]) for i in range(3)], ntri, doors, len(sub.get('MODR', b''))//2))
    return out
if __name__ == '__main__':
    for p in sys.argv[1:]:
        res = survey(p)
        if not res: print(p, 'missing'); continue
        print('==', p.split('\\')[-1], len(res), 'groups')
        for g, fl, size, ntri, doors, nd in res:
            kind = 'out' if fl & 8 else 'in '
            print('  g%-2d %s size %-14s tris %5d doodads %3d doors %d: %s' % (g, kind, size, ntri, nd, len(doors),
                  ' '.join('%dx%d->g%d' % (w, h, o) for (w, h), o in doors)))
