"""Real wings (TILESET.md, "real wings"): a stock WMO used WHOLE as a kit piece. Every interior group is kept byte
for byte with the portals between them, so the inside is as closed as Blizzard made it; the exterior groups (the
hillside or street around the entrances) are dropped, and each portal that led outside (a mouth) gets a generated
adapter from the mouth's outline to the theme's doorway, ending in a socket.

Groups: the kept interior groups first (in source order), then one adapter per mouth. Portals are renumbered; MOPR
is rebuilt so each group's refs are contiguous (its header's portal range is rewritten), a mouth's ref names its
adapter, and each adapter refers back to its group from the other side. The root keeps the source's materials,
lights, doodad sets, doodads, visibility blocks and fogs (fog turned into the pool map's), so every index in the
kept group files stays valid. Server collision: each kept group's colliding triangles plus the adapters'.
"""
import math
import struct

import kitwmo
import stockroom as sr

MOGP_INDOORS = 0x2000
MOGP_OUTDOORS = 0x8


def _mouth(portal_verts, plane, owner_box):
    """A mouth as stockroom doorways describe it: outline, plane, outward (away from the owner group), centre,
    rect (right min, right max, z min, z max)."""
    cx = sum(v[0] for v in portal_verts) / len(portal_verts)
    cy = sum(v[1] for v in portal_verts) / len(portal_verts)
    nx, ny = plane[0], plane[1]
    ln = math.hypot(nx, ny)
    if ln < 0.5:
        return None                                   # a skylight or a floor hole, not a doorway
    dx, dy = nx / ln, ny / ln
    gx, gy = (owner_box[0] + owner_box[3]) / 2, (owner_box[1] + owner_box[4]) / 2
    if dx * (cx - gx) + dy * (cy - gy) < 0:
        dx, dy = -dx, -dy
    rx, ry = dy, -dx
    rs = [(v[0] - cx) * rx + (v[1] - cy) * ry for v in portal_verts]
    zs = [v[2] for v in portal_verts]
    return {'poly': portal_verts, 'plane': plane, 'out': (dx, dy), 'right': (rx, ry), 'centre': (cx, cy),
            'rect': (min(rs), max(rs), min(zs), max(zs))}


def build_files(spec, client=sr.DEFAULT_CLIENT):
    src = spec['source']
    root_raw = sr._read(client, src)
    chunks = sr._chunks(root_raw)
    root = dict(chunks)
    order = [t for t, _ in chunks]
    want = ['MVER', 'MOHD', 'MOTX', 'MOMT', 'MOGN', 'MOGI', 'MOSB', 'MOPV', 'MOPT', 'MOPR', 'MOVV', 'MOVB', 'MOLT',
            'MODS', 'MODN', 'MODD', 'MFOG']
    if order[:len(want)] != want:
        raise SystemExit('%s: unexpected root chunk order %s' % (src, order))
    mohd = bytearray(root['MOHD'])
    ng = struct.unpack_from('<I', mohd, 4)[0]
    mogi = root['MOGI']
    gflags = [struct.unpack_from('<I', mogi, i * 32)[0] for i in range(ng)]
    gbox = [struct.unpack_from('<6f', mogi, i * 32 + 4) for i in range(ng)]
    keep = [g for g in range(ng) if gflags[g] & MOGP_INDOORS]
    if not keep:
        raise SystemExit('%s has no interior group' % src)
    new_index = {g: i for i, g in enumerate(keep)}

    pv = root['MOPV']
    pverts = [struct.unpack_from('<3f', pv, i * 12) for i in range(len(pv) // 12)]
    pt, pr = root['MOPT'], root['MOPR']

    def portal(p):
        vs, vc = struct.unpack_from('<HH', pt, p * 20)
        return pverts[vs:vs + vc], struct.unpack_from('<4f', pt, p * 20 + 4)

    # Each kept group: its file, header, refs (portal, other group, side), collision.
    groups = []
    for g in keep:
        raw = bytearray(sr._read(client, '%s_%03d.wmo' % (src[:-4], g)))
        header = bytes(raw[20:20 + 68])
        start, count = struct.unpack_from('<HH', header, 0x24)
        refs = [struct.unpack_from('<HHh', pr, k * 8) for k in range(start, start + count)]
        sub = dict(sr._chunks(bytes(raw), 20 + 68))
        verts, tris = sr.room_collision(sub)
        groups.append({'g': g, 'raw': raw, 'refs': refs, 'verts': verts, 'tris': tris})

    # Mouths: refs to a dropped (exterior) group. Portals: renumbered in order of first use.
    portal_map, portals, mouths = {}, [], []
    for G in groups:
        for p, other, side in G['refs']:
            if p not in portal_map:
                portal_map[p] = len(portals)
                portals.append(portal(p))
            if other not in new_index:
                m = _mouth(*portal(p), gbox[G['g']])
                if m is None:
                    raise SystemExit('%s: group %d opens to the outside through a non-vertical portal %d'
                                     % (src, G['g'], p))
                m.update({'portal': portal_map[p], 'owner': new_index[G['g']], 'side': side})
                m['floor'] = sr.door_floor(m, G['verts'], G['tris'])
                mouths.append(m)
    if not mouths:
        raise SystemExit('%s has no mouth (no portal from inside to outside)' % src)
    lo_m, hi_m = spec.get('mouths', [1, 6])
    if not lo_m <= len(mouths) <= hi_m:
        raise SystemExit('%s has %d mouths' % (src, len(mouths)))

    # Materials: the source's, then the adapters' (theme textures, unculled).
    textures = spec.get('adapter_textures', sr.ADAPTER_TEXTURES)
    motx, momt = root['MOTX'], root['MOMT']
    base = len(momt) // 64
    for tex in textures:
        motx, off = sr._texture_offset(motx, tex)
        momt += struct.pack('<16I', kitwmo.MAT_UNCULLED, 0, 0, off, 0, 0, 0, 0xff7f7f7f, 10, 0, 0, 0, 0, 0, 0, 0)
    mats = tuple(base + i for i in range(len(textures)))

    k = len(keep)
    names = ['%s_in%d' % (spec['file'], G['g']) for G in groups] + ['%s_mouth%d' % (spec['file'], i)
                                                                     for i in range(len(mouths))]
    mogn, name_off = b'\0\0', []
    for n in names:
        name_off.append(len(mogn))
        mogn += n.encode('ascii') + b'\0'
    mogn += b'\0' * ((-len(mogn)) % 4)

    # MOPR: each kept group's refs (mouths now name their adapter), then each adapter's ref back.
    mopr, starts = b'', []
    adapter_index = {}
    for i, m in enumerate(mouths):
        adapter_index[(m['owner'], m['portal'])] = k + i
    for i, G in enumerate(groups):
        starts.append(len(mopr) // 8)
        for p, other, side in G['refs']:
            np_ = portal_map[p]
            target = new_index[other] if other in new_index else adapter_index[(i, np_)]
            mopr += struct.pack('<HHhH', np_, target, side, 0)
    for i, m in enumerate(mouths):
        mopr += struct.pack('<HHhH', m['portal'], m['owner'], -m['side'], 0)
    mopv, mopt = b'', b''
    for verts, plane in portals:
        mopt += struct.pack('<HH4f', len(mopv) // 12, len(verts), *plane)
        mopv += b''.join(struct.pack('<3f', *v) for v in verts)

    files, coll, sockets = {}, [], []
    path = kitwmo.wmo_path(spec)
    new_mogi = b''
    lo = [min(gbox[g][a] for g in keep) for a in range(3)]
    hi = [max(gbox[g][3 + a] for g in keep) for a in range(3)]
    for i, G in enumerate(groups):
        raw = G['raw']
        struct.pack_into('<I', raw, 20, name_off[i])
        struct.pack_into('<HH', raw, 20 + 0x24, starts[i], len(G['refs']))
        files['%s_%03d.wmo' % (path[:-4], i)] = bytes(raw)
        coll.append((G['verts'], G['tris'], gflags[G['g']] & (MOGP_INDOORS | MOGP_OUTDOORS)))
        new_mogi += struct.pack('<I6fI', gflags[G['g']], *gbox[G['g']], name_off[i])
    size = tuple(spec.get('door', (kitwmo.DOOR_W, kitwmo.DOOR_H)))
    for i, m in enumerate(mouths):
        mesh, socket = sr.adapter_mesh(m, mats, size, spec.get('adapter_len', 10.0), spec.get('frame', False))
        data, tris, verts = kitwmo.group_file(mesh, name_off[k + i], len(mopr) // 8 - len(mouths) + i, 1)
        files['%s_%03d.wmo' % (path[:-4], k + i)] = data
        sockets.append(socket)
        coll.append((verts, tris, MOGP_OUTDOORS))
        b = mesh.bounds()
        new_mogi += struct.pack('<I6fI', 0x8 | 0x1, *b, name_off[k + i])
        lo = [min(lo[a], b[a]) for a in range(3)]
        hi = [max(hi[a], b[3 + a]) for a in range(3)]

    struct.pack_into('<I', mohd, 0, len(momt) // 64)
    struct.pack_into('<II', mohd, 4, k + len(mouths), len(portals))
    struct.pack_into('<I', mohd, 32, spec['wmo_id'])
    struct.pack_into('<6f', mohd, 36, *lo, *hi)
    parts = {'MOHD': bytes(mohd), 'MOTX': motx, 'MOMT': momt, 'MOGN': mogn, 'MOGI': new_mogi, 'MOPV': mopv,
             'MOPT': mopt, 'MOPR': mopr, 'MFOG': kitwmo.with_pool_fog(root['MFOG'])}
    tail = [(t, d) for t, d in chunks[len(want):]]          # e.g. MCVP, kept as is
    files[path] = (b''.join(sr._chunk(t, parts.get(t, root[t])) for t in want)
                   + b''.join(sr._chunk(t, d) for t, d in tail))

    first = groups[0]
    entrance = [(gbox[first['g']][0] + gbox[first['g']][3]) / 2, (gbox[first['g']][1] + gbox[first['g']][4]) / 2,
                min(m['floor'] for m in mouths) + 0.1]
    return files, {'sockets': sockets, 'entrance': entrance, 'bounds': tuple(lo) + tuple(hi), 'groups': coll,
                   'verts': [v for c in coll for v in c[0]], 'tris': [], 'mouths': len(mouths), 'rooms': k}
