"""Let a clone's own map fog reach inside its source's WMOs. Inside a WMO the client uses the WMO's fog entries
(MFOG) instead of the map's light, unless the WMO has exactly one entry (CMapEntity::QueryCameraFog returns early
for m_fogsCount == 1), and WMO fog can never be thick (it ends at 30 yd or more and turns into a haze in fog mode
1). So the patch carries the root file of each WMO the source map places that has more than one fog entry, with
only its first entry (the WMO's global fog) kept. The group files are untouched: their fog indices are only read by
that same query, which no longer reads them.

The change applies to the user's client wherever these WMOs are used, the source dungeon included; there the source
map's own light now shows inside them in place of the WMO's local fog spheres. Client only.
"""
import struct

import mpq

MFOG_ENTRY = 48


def _chunks(b):
    o, out = 0, []
    while o + 8 <= len(b):
        tag = b[o:o + 4][::-1].decode('latin1')
        n = struct.unpack_from('<I', b, o + 4)[0]
        out.append((tag, b[o + 8:o + 8 + n]))
        o += 8 + n
    return out


def _chunk(tag, data):
    return tag[::-1].encode('ascii') + struct.pack('<I', len(data)) + data


def map_wmos(client, directory, order):
    """The WMO root names a map's WDT and terrain tiles place (as the files spell them)."""
    _, wdt = mpq.read_effective(client, 'World\\Maps\\%s\\%s.wdt' % (directory, directory), order)
    if not wdt:
        raise SystemExit('no WDT for map directory %s' % directory)
    c = dict(_chunks(wdt))
    names = set()
    if struct.unpack_from('<I', c['MPHD'])[0] & 1:
        names.add(c['MWMO'].split(b'\0')[0].decode('latin1'))
    main = c['MAIN']
    for i in range(4096):
        if struct.unpack_from('<I', main, i * 8)[0] & 1:
            x, y = i % 64, i // 64
            _, adt = mpq.read_effective(client, 'World\\Maps\\%s\\%s_%d_%d.adt' % (directory, directory, x, y), order)
            for tag, data in _chunks(adt or b''):
                if tag == 'MWMO':
                    names.update(n.decode('latin1') for n in data.split(b'\0') if n)
    return sorted(names)


def one_fog(root):
    """`root` (a WMO root file) with only its first fog entry, or None when it already has at most one."""
    chunks = _chunks(root)
    mfog = dict(chunks).get('MFOG', b'')
    if len(mfog) <= MFOG_ENTRY:
        return None
    return b''.join(_chunk(t, d[:MFOG_ENTRY] if t == 'MFOG' else d) for t, d in chunks)


def patch_files(client, directory, patch_name='patch-T.MPQ'):
    """{WMO root path: one-fog root} for the WMOs of a map directory that need it."""
    order = mpq.client_order(client, exclude=[patch_name])
    out = {}
    for name in map_wmos(client, directory, order):
        _, raw = mpq.read_effective(client, name, order)
        if not raw:
            continue
        fixed = one_fog(raw)
        if fixed is not None:
            out[name] = fixed
    return out
