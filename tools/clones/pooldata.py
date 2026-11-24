"""The generated-map pool (pool.json): Map.dbc and MapDifficulty.dbc rows of a pool map whose WDT is empty (no
terrain tiles, no WMO), the WDT itself, and the kit pieces mod-atlas places in it as gameobjects. The client reads
MVER, MPHD and MAIN and only looks for a WMO when MPHD flag 1 is set (Wowdecomp2 CMap::LoadWdt, WorldClient/
Map.cpp:2892); a missing WDL is skipped (MapLowDetail.cpp:123).
"""
import json
import os
import struct

import clonedata
import dbc

POOL_MAP_RANGE = (950, 999)
PIECE_ENTRY_RANGE = (900100, 900199)
DBC_GO_DISPLAY = 'DBFilesClient\\GameObjectDisplayInfo.dbc'


def load_config(path=None):
    with open(path or os.path.join(clonedata.HERE, 'pool.json')) as f:
        cfg = json.load(f)
    m = cfg['map']
    if not POOL_MAP_RANGE[0] <= m['id'] <= POOL_MAP_RANGE[1]:
        raise ValueError('pool map %d outside %d-%d' % (m['id'], *POOL_MAP_RANGE))
    if m['mapdifficulty_id'] < clonedata.MAPDIFFICULTY_ID_MIN:
        raise ValueError('pool MapDifficulty %d below %d' % (m['mapdifficulty_id'], clonedata.MAPDIFFICULTY_ID_MIN))
    clone_md = {c['mapdifficulty_id'] for c in clonedata.load_config()['clones']}
    if m['mapdifficulty_id'] in clone_md:
        raise ValueError('pool MapDifficulty %d is taken by a clone in clones.json' % m['mapdifficulty_id'])
    for p in cfg['pieces']:
        if not PIECE_ENTRY_RANGE[0] <= p['go_entry'] <= PIECE_ENTRY_RANGE[1]:
            raise ValueError('piece %d outside %d-%d' % (p['go_entry'], *PIECE_ENTRY_RANGE))
    return cfg


def wdt_path(cfg):
    d = cfg['map']['directory']
    return 'World\\Maps\\%s\\%s.wdt' % (d, d)


def empty_wdt():
    """MVER 18, MPHD (32 bytes, no flags: no global WMO), MAIN (64 x 64 tiles, none present), empty MWMO."""
    def chunk(tag, data):
        return tag[::-1].encode('ascii') + struct.pack('<I', len(data)) + data
    return (chunk('MVER', struct.pack('<I', 18)) + chunk('MPHD', bytes(32)) + chunk('MAIN', bytes(64 * 64 * 8))
            + chunk('MWMO', b''))


def add_pool(map_dbc, md_dbc, cfg):
    """Append the pool map's rows (a copy of the template map's, with our ID, Directory and name)."""
    m = cfg['map']
    src = map_dbc.row(m['template'])
    if not src:
        raise SystemExit('pool template map %d has no Map.dbc row' % m['template'])
    if m['id'] in map_dbc.ids():
        raise SystemExit('Map.dbc already has a row %d' % m['id'])
    if m['mapdifficulty_id'] in md_dbc.ids():
        raise SystemExit('MapDifficulty.dbc already has a row %d' % m['mapdifficulty_id'])
    src_md = [r for r in md_dbc.rows if r[1] == m['template'] and r[2] == 0]
    if len(src_md) != 1:
        raise SystemExit('pool template map %d: expected one normal MapDifficulty row' % m['template'])

    row = list(src)
    row[0] = m['id']
    row[1] = map_dbc.add_string(m['directory'])
    row[clonedata.MAP_NAME_ENUS] = map_dbc.add_string(m['name'])
    md = list(src_md[0])
    md[0] = m['mapdifficulty_id']
    md[1] = m['id']
    map_dbc.rows.append(row)
    md_dbc.rows.append(md)
    map_dbc.rows.sort(key=lambda r: r[0])
    md_dbc.rows.sort(key=lambda r: r[0])
    return row, md


def check_pieces(display_dbc, cfg):
    """Each piece's display exists in the client and shows the piece's WMO."""
    for p in cfg['pieces']:
        r = display_dbc.row(p['display'])
        if not r:
            raise SystemExit('piece %d: GameObjectDisplayInfo %d not in the client' % (p['go_entry'], p['display']))
        model = display_dbc.string(r[1])
        if model.lower() != p['wmo'].lower():
            raise SystemExit('piece %d: display %d shows %s, not %s' % (p['go_entry'], p['display'], model, p['wmo']))


def entrance(piece):
    """World position of a piece's entrance (rotation 0 only, for the spike)."""
    px, py, pz, po = piece['position']
    if po:
        raise SystemExit('piece %d: rotated pieces are not supported yet' % piece['go_entry'])
    ex, ey, ez = piece['entrance_local']
    return px + ex, py + ey, pz + ez
