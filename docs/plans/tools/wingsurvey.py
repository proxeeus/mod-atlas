"""Score every root WMO in the client as a 'real wing' (TILESET.md): interior groups kept whole, adapters at the
portals that lead outside (mouths). Root files only: MOGI flags tell inside (0x2000) from outside (0x8); each
portal appears in two MOPR entries whose group fields name the two groups it joins. Output: TSV on stdout."""
import os, struct, sys, math
sys.path.insert(0, '/Users/ac-playerbots/azerothcore-playerbots/azerothcore-wotlk/modules/mod-atlas/tools/clones')
import mpq

C = '/Users/gaming/Desktop/World of Warcraft 3.3.5a'

def chunks(b):
    o, out = 0, {}
    while o + 8 <= len(b):
        t = b[o:o+4][::-1].decode('latin1'); n = struct.unpack_from('<I', b, o+4)[0]
        out.setdefault(t, b[o+8:o+8+n]); o += 8 + n
    return out

def main():
    order = mpq.client_order(C, exclude=['patch-T.MPQ'])
    names = set()
    for arch in order:
        try:
            a = mpq.MPQ(os.path.join(C, 'Data', arch))
        except Exception:
            continue
        for n in a.listfile():
            l = n.lower()
            if l.endswith('.wmo') and not (len(n) > 8 and n[-8] == '_' and n[-7:-4].isdigit()):
                names.add(n)
    print('path\tgroups\tinside\tmouths\tmouth_sizes\tinside_size\tinside_tris_hint', flush=True)
    for path in sorted(names, key=str.lower):
        try:
            _, raw = mpq.read_effective(C, path, order)
            if not raw:
                continue
            c = chunks(raw)
            ng = struct.unpack_from('<I', c['MOHD'], 4)[0]
            mogi = c['MOGI']
            flags = [struct.unpack_from('<I', mogi, i*32)[0] for i in range(ng)]
            boxes = [struct.unpack_from('<6f', mogi, i*32+4) for i in range(ng)]
            inside = [i for i in range(ng) if flags[i] & 0x2000]
            if not inside:
                continue
            pv = c.get('MOPV', b''); verts = [struct.unpack_from('<3f', pv, i*12) for i in range(len(pv)//12)]
            pt = c.get('MOPT', b''); refs = c.get('MOPR', b'')
            joins = {}
            for k in range(len(refs)//8):
                p, g, side = struct.unpack_from('<HHh', refs, k*8)
                joins.setdefault(p, set()).add(g)
            mouths = []
            for p, gs in joins.items():
                gs = [g for g in gs if g < ng]
                if len(gs) == 2 and (flags[gs[0]] & 0x2000) != (flags[gs[1]] & 0x2000):
                    s, n = struct.unpack_from('<HH', pt, p*20); q = verts[s:s+n]
                    if not q:
                        continue
                    w = max(math.hypot(a[0]-b[0], a[1]-b[1]) for a in q for b in q)
                    h = max(v[2] for v in q) - min(v[2] for v in q)
                    mouths.append((round(w), round(h)))
            lo = [min(boxes[i][a] for i in inside) for a in range(3)]
            hi = [max(boxes[i][3+a] for i in inside) for a in range(3)]
            size = [round(hi[a]-lo[a]) for a in range(3)]
            print('%s\t%d\t%d\t%d\t%s\t%s\t%d' % (path, ng, len(inside), len(mouths),
                  ' '.join('%dx%d' % m for m in mouths), 'x'.join(map(str, size)), len(raw)), flush=True)
        except Exception as e:
            print('%s\tERROR %s' % (path, e), file=sys.stderr)

main()
