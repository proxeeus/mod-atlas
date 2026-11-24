"""Check that the built client patch and the server SQL hold the same clone rows, field by field.

  python3 -I verify.py [--patch FILE] [--sql FILE]

Reads client_patch/patch-T.MPQ, atlas_clones.sql and atlas_fogs.sql (staged or installed); exits 1 on any
difference.
"""
import argparse
import os
import re
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

import clonedata  # noqa: E402
import dbc  # noqa: E402
import fogdata  # noqa: E402
import mpq  # noqa: E402
from gen_sql import clones_sql, part_sql  # noqa: E402


def _tokens(text):
    """Values of one `(...)` tuple list: strings unquoted, numbers as text."""
    i, n = 0, len(text)
    rows, row = [], None
    while i < n:
        c = text[i]
        if c == '(':
            row = []
            i += 1
        elif c == ')':
            rows.append(row)
            row = None
            i += 1
        elif c == "'":
            j, out = i + 1, []
            while True:
                if text[j] == '\\':
                    out.append(text[j + 1])
                    j += 2
                elif text[j] == "'" and j + 1 < n and text[j + 1] == "'":
                    out.append("'")
                    j += 2
                elif text[j] == "'":
                    break
                else:
                    out.append(text[j])
                    j += 1
            row.append(('s', ''.join(out)))
            i = j + 1
        elif row is not None and (c.isdigit() or c in '-.'):
            m = re.match(r'-?[0-9.eE+-]+', text[i:])
            row.append(('n', m.group(0)))
            i += len(m.group(0))
        else:
            i += 1
    return rows


def sql_rows(path, table):
    text = open(path, encoding='utf8').read()
    m = re.search(r'INSERT INTO `%s`\s*\((.*?)\)\s*VALUES\s*(.*?);' % re.escape(table), text, re.S)
    if not m:
        raise SystemExit('no INSERT INTO %s in %s' % (table, path))
    cols = re.findall(r'`([^`]+)`', m.group(1))
    return cols, _tokens(m.group(2))


def same(value, kind, token):
    tk, raw = token
    if kind == 's':
        return tk == 's' and raw == value
    if tk != 'n':
        return False
    if kind == 'f':
        return dbc.u32f(float(raw)) == dbc.u32f(value)
    return int(raw) == value


def verify_fogs(archive, sql_path):
    """Fog spells: patch Spell.dbc rows = spell_dbc rows; each has a swirling-fog ScreenEffect row; atlas_fog only
    names fog spells."""
    errors = 0
    cfg = fogdata.load_config()
    fields = fogdata.spell_fields()
    spells = dbc.RawDbc(archive.read(fogdata.DBC_SPELL))
    effects = dbc.Dbc.parse(archive.read(fogdata.DBC_SCREEN_EFFECT))
    cols, rows = sql_rows(sql_path, 'spell_dbc')
    if cols != [n for n, _ in fields]:
        print('spell_dbc: column list differs from Spell.dbc')
        return 1
    by_id = {int(r[0][1]): r for r in rows}
    want = {f['spell']: f for f in cfg['fogs']}
    if set(by_id) != set(want):
        print('spell_dbc: SQL rows %s, fogs %s' % (sorted(by_id), sorted(want)))
        errors += 1
    misc = [n for n, _ in fields].index('EffectMiscValue_1')
    for sid, fog in sorted(want.items()):
        raw = spells.row(sid)
        if not raw or sid not in by_id:
            print('fog spell %d missing from the patch or the SQL' % sid)
            errors += 1
            continue
        bad = sum(1 for (col, kind), value, token in zip(fields, fogdata.decode_spell(spells, raw, fields), by_id[sid])
                  if not same(value, kind, token))
        effect = effects.row(raw[misc])
        if not effect or effect[2] != fogdata.SCREEN_EFFECT_SWIRLING_FOG:
            print('fog spell %d: no swirling-fog ScreenEffect row %d' % (sid, raw[misc]))
            bad += 1
        errors += bad
        print('Spell.dbc          row %d: %d of %d fields match, screen effect %d' % (sid, len(fields) - bad,
                                                                                   len(fields), raw[misc]))
    _, fog_rows = sql_rows(sql_path, 'atlas_fog')
    unknown = {int(r[4][1]) for r in fog_rows} - set(want)
    if unknown:
        print('atlas_fog names spells that are not fogs: %s' % sorted(unknown))
        errors += 1
    print('atlas_fog          %d rows' % len(fog_rows))
    return errors


def main():
    cfg = clonedata.load_config()
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('--patch', default=os.path.join(clonedata.MODULE, 'client_patch', cfg['patch_name']))
    ap.add_argument('--sql', default=clones_sql())
    ap.add_argument('--fog-sql', default=part_sql('fogs'))
    args = ap.parse_args()

    archive = mpq.MPQ(args.patch)
    errors = 0
    for name, table, fields, key in (('Map.dbc', 'map_dbc', clonedata.MAP_FIELDS, 'id'),
                                     ('MapDifficulty.dbc', 'mapdifficulty_dbc', clonedata.MAPDIFFICULTY_FIELDS,
                                      'mapdifficulty_id')):
        d = dbc.Dbc.parse(archive.read('DBFilesClient\\' + name))
        cols, rows = sql_rows(args.sql, table)
        if cols != [f for f, _ in fields]:
            print('%s: column list differs from the client layout' % table)
            errors += 1
            continue
        by_id = {int(r[0][1]): r for r in rows}
        want = {c[key] for c in clonedata.enabled(cfg)}
        if set(by_id) != want:
            print('%s: SQL rows %s, enabled clones %s' % (table, sorted(by_id), sorted(want)))
            errors += 1
        for rid in sorted(want):
            raw = d.row(rid)
            if not raw:
                print('%s: row %d missing from the patch' % (name, rid))
                errors += 1
                continue
            if rid not in by_id:
                continue
            decoded = clonedata.decode(d, raw, fields)
            bad = 0
            for (col, kind), value, token in zip(fields, decoded, by_id[rid]):
                if not same(value, kind, token):
                    print('%s row %d: %s patch=%r sql=%r' % (table, rid, col, value, token[1]))
                    bad += 1
            errors += bad
            print('%-18s row %d: %d of %d fields match' % (name, rid, len(fields) - bad, len(fields)))
    errors += verify_fogs(archive, args.fog_sql)
    print('OK' if not errors else '%d difference(s)' % errors)
    sys.exit(1 if errors else 0)


if __name__ == '__main__':
    main()
