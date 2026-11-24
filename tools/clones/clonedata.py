"""The clone rows, computed once from the client's own Map.dbc / MapDifficulty.dbc, for both the client patch
(build_patch.py) and the server SQL (gen_sql.py), so the two never drift.

Field layouts (3.3.5a, 12340): Map.dbc 66 fields, MapDifficulty.dbc 23 fields, both matching the column order of
the world tables `map_dbc` and `mapdifficulty_dbc` (data/sql/base/db_world/).
"""
import hashlib
import json
import os
import re

import dbc
import mpq

HERE = os.path.dirname(os.path.abspath(__file__))
MODULE = os.path.normpath(os.path.join(HERE, '..', '..'))
REPO = os.path.normpath(os.path.join(MODULE, '..', '..'))

MAP_ID_RANGE = (900, 949)
MAPDIFFICULTY_ID_MIN = 754

LOCALES = ['enUS', 'enGB', 'koKR', 'frFR', 'deDE', 'enCN', 'zhCN', 'enTW', 'zhTW', 'esES', 'esMX', 'ruRU', 'ptPT',
           'ptBR', 'itIT', 'Unk']


def _lang(prefix):
    return [('%s_Lang_%s' % (prefix, loc), 's') for loc in LOCALES] + [('%s_Lang_Mask' % prefix, 'u')]


# (column, type): i = int32, u = uint32, f = float, s = string offset.
MAP_FIELDS = ([('ID', 'i'), ('Directory', 's'), ('InstanceType', 'i'), ('Flags', 'i'), ('PVP', 'i')]
              + _lang('MapName') + [('AreaTableID', 'i')] + _lang('MapDescription0') + _lang('MapDescription1')
              + [('LoadingScreenID', 'i'), ('MinimapIconScale', 'f'), ('CorpseMapID', 'i'), ('CorpseX', 'f'),
                 ('CorpseY', 'f'), ('TimeOfDayOverride', 'i'), ('ExpansionID', 'i'), ('RaidOffset', 'i'),
                 ('MaxPlayers', 'i')])
MAPDIFFICULTY_FIELDS = ([('ID', 'i'), ('MapID', 'i'), ('Difficulty', 'i')] + _lang('Message')
                        + [('RaidDuration', 'i'), ('MaxPlayers', 'i'), ('Difficultystring', 's')])
assert len(MAP_FIELDS) == 66 and len(MAPDIFFICULTY_FIELDS) == 23

MAP_NAME_ENUS = 5
DBC_MAP = 'DBFilesClient\\Map.dbc'
DBC_MAPDIFFICULTY = 'DBFilesClient\\MapDifficulty.dbc'


def load_config(path=None):
    with open(path or os.path.join(HERE, 'clones.json')) as f:
        cfg = json.load(f)
    seen_ids, seen_md = set(), set()
    for c in cfg['clones']:
        if not MAP_ID_RANGE[0] <= c['id'] <= MAP_ID_RANGE[1]:
            raise ValueError('clone %d is outside the reserved map IDs %d-%d' % (c['id'], *MAP_ID_RANGE))
        if c['mapdifficulty_id'] < MAPDIFFICULTY_ID_MIN:
            raise ValueError('clone %d: MapDifficulty ID %d is below the reserved %d+'
                             % (c['id'], c['mapdifficulty_id'], MAPDIFFICULTY_ID_MIN))
        if c['id'] in seen_ids or c['mapdifficulty_id'] in seen_md:
            raise ValueError('clone %d: duplicate map or MapDifficulty ID' % c['id'])
        seen_ids.add(c['id'])
        seen_md.add(c['mapdifficulty_id'])
    return cfg


def enabled(cfg):
    return [c for c in cfg['clones'] if c.get('enabled')]


def source_dbcs(client=None, dbc_dir=None, patch_name='patch-T.MPQ'):
    """(Map, MapDifficulty, where) as the client sees them without our own patch."""
    if dbc_dir:
        return (dbc.Dbc.load(os.path.join(dbc_dir, 'Map.dbc')),
                dbc.Dbc.load(os.path.join(dbc_dir, 'MapDifficulty.dbc')), {'Map.dbc': dbc_dir,
                                                                          'MapDifficulty.dbc': dbc_dir})
    order = mpq.client_order(client, exclude=[patch_name])
    arch_m, raw_m = mpq.read_effective(client, DBC_MAP, order)
    arch_d, raw_d = mpq.read_effective(client, DBC_MAPDIFFICULTY, order)
    if not raw_m or not raw_d:
        raise SystemExit('Map.dbc or MapDifficulty.dbc not found in the client archives')
    return dbc.Dbc.parse(raw_m), dbc.Dbc.parse(raw_d), {'Map.dbc': arch_m, 'MapDifficulty.dbc': arch_d}


def effective_raw(client, dbc_dir, name, patch_name='patch-T.MPQ'):
    """(bytes, where) of a client DBC as the client sees it without our own patch, or from `dbc_dir`."""
    if dbc_dir:
        with open(os.path.join(dbc_dir, name), 'rb') as f:
            return f.read(), dbc_dir
    order = mpq.client_order(client, exclude=[patch_name])
    arch, raw = mpq.read_effective(client, 'DBFilesClient\\' + name, order)
    if not raw:
        raise SystemExit('%s not found in the client archives' % name)
    return raw, arch


def decode(table, row, fields):
    """Typed values of a raw row: ints, floats and strings."""
    out = []
    for (name, kind), raw in zip(fields, row):
        if kind == 's':
            out.append(table.string(raw))
        elif kind == 'f':
            out.append(dbc.f32(raw))
        elif kind == 'i':
            out.append(dbc.i32(raw))
        else:
            out.append(raw)
    return out


def add_clones(map_dbc, md_dbc, clones):
    """Append each clone's rows to the (copied) DBCs. Returns [(clone, map_row, md_row)] of raw rows."""
    added = []
    map_ids, md_ids = map_dbc.ids(), md_dbc.ids()
    for c in clones:
        src = map_dbc.row(c['source'])
        if not src:
            raise SystemExit('clone %d: source map %d has no Map.dbc row' % (c['id'], c['source']))
        if c['id'] in map_ids:
            raise SystemExit('clone %d: Map.dbc already has a row %d (is an old patch-T.MPQ in the order?)'
                             % (c['id'], c['id']))
        if c['mapdifficulty_id'] in md_ids:
            raise SystemExit('clone %d: MapDifficulty.dbc already has a row %d' % (c['id'], c['mapdifficulty_id']))
        src_md = [r for r in md_dbc.rows if r[1] == c['source'] and r[2] == 0]
        if len(src_md) != 1:
            raise SystemExit('clone %d: source map %d has %d normal MapDifficulty rows, expected 1'
                             % (c['id'], c['source'], len(src_md)))

        row = list(src)
        row[0] = c['id']
        row[MAP_NAME_ENUS] = map_dbc.add_string(c['name'])
        md = list(src_md[0])
        md[0] = c['mapdifficulty_id']
        md[1] = c['id']
        map_dbc.rows.append(row)
        md_dbc.rows.append(md)
        added.append((c, row, md))
    map_dbc.rows.sort(key=lambda r: r[0])
    md_dbc.rows.sort(key=lambda r: r[0])
    return added


def checksum(map_dbc, md_dbc, added):
    """A digest of the decoded clone rows, the same for the patch and the SQL built from them."""
    h = hashlib.sha1()
    for _, row, md in added:
        h.update(repr(decode(map_dbc, row, MAP_FIELDS)).encode())
        h.update(repr(decode(md_dbc, md, MAPDIFFICULTY_FIELDS)).encode())
    return h.hexdigest()[:16]


def source_instance_templates():
    """{map: (parent, script, allowMount)} from the base world dump (read-only)."""
    path = os.path.join(REPO, 'data', 'sql', 'base', 'db_world', 'instance_template.sql')
    with open(path, encoding='utf8') as f:
        text = f.read()
    rows = {}
    for m in re.finditer(r"\((\d+),(\d+),'([^']*)',(\d+)\)", text):
        rows[int(m.group(1))] = (int(m.group(2)), m.group(3), int(m.group(4)))
    return rows
