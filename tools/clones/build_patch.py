"""Build the Atlas client patch, Data/patch-T.MPQ: the client's own Map.dbc and MapDifficulty.dbc plus one row
each per enabled clone (clones.json), and its own ScreenEffect.dbc and Spell.dbc plus each fog's rows (fogs.json).
Reads the client archives read-only; writes only into modules/mod-atlas/client_patch/ (gitignored). The user copies
the MPQ into the client's Data folder.

  python3 -I build_patch.py [--client DIR | --dbc-dir DIR] [--out FILE]
"""
import argparse
import hashlib
import json
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

import clonedata  # noqa: E402
import dbc  # noqa: E402
import fogdata  # noqa: E402
import lightdata  # noqa: E402
import mpq  # noqa: E402
import wmofog  # noqa: E402

DEFAULT_CLIENT = '/Users/gaming/Desktop/World of Warcraft 3.3.5a'


def check_fogs(archive, old_effects, old_spells, fogs):
    """Stock ScreenEffect rows unchanged; Spell.dbc = the stock records and strings, then the fog rows."""
    new = dbc.Dbc.parse(archive.read(fogdata.DBC_SCREEN_EFFECT))
    old_rows = {r[0]: clonedata.decode(old_effects, r, fogdata.SCREEN_EFFECT_FIELDS) for r in old_effects.rows}
    new_rows = {r[0]: clonedata.decode(new, r, fogdata.SCREEN_EFFECT_FIELDS) for r in new.rows}
    if set(new_rows) - set(old_rows) != {f['effect'] for f, _, _ in fogs} or set(old_rows) - set(new_rows):
        raise SystemExit('ScreenEffect.dbc: unexpected row set change')
    if any(new_rows[rid] != values for rid, values in old_rows.items()):
        raise SystemExit('ScreenEffect.dbc: a stock row changed')

    spells = dbc.RawDbc(archive.read(fogdata.DBC_SPELL))
    if (spells.count != old_spells.count + len(fogs) or spells.records[:len(old_spells.records)] != old_spells.records
            or spells.strings[:len(old_spells.strings)] != old_spells.strings):
        raise SystemExit('Spell.dbc: the stock records or strings changed')
    fields = fogdata.spell_fields()
    for fog, _, row in fogs:
        if spells.row(fog['spell']) != row:
            raise SystemExit('Spell.dbc: fog spell %d differs' % fog['spell'])
        values = fogdata.decode_spell(spells, row, fields)
        print('fog %d "%s" (%s) -> screen effect %d: colour %s spread %d strength %d%%'
              % (fog['spell'], values[[n for n, _ in fields].index('Name_Lang_enUS')], fog['aura'], fog['effect'],
                 fog['colour'], fog['spread'], fog['strength']))


def roundtrip(path, want, sources, added):
    """Read the archive back and check every file and every row."""
    archive = mpq.MPQ(path)
    for name, data in want.items():
        got = archive.read(name)
        if got != data:
            raise SystemExit('round trip failed for %s' % name)
    names = set(archive.listfile())
    if names != set(want):
        raise SystemExit('listfile mismatch: %s' % sorted(names))

    clone_map_ids = {c['id'] for c, _, _ in added}
    clone_md_ids = {c['mapdifficulty_id'] for c, _, _ in added}
    for name, fields, skip in (('Map.dbc', clonedata.MAP_FIELDS, clone_map_ids),
                               ('MapDifficulty.dbc', clonedata.MAPDIFFICULTY_FIELDS, clone_md_ids)):
        new = dbc.Dbc.parse(archive.read('DBFilesClient\\' + name))
        old = sources[name]
        old_rows = {r[0]: clonedata.decode(old, r, fields) for r in old.rows}
        new_rows = {r[0]: clonedata.decode(new, r, fields) for r in new.rows}
        if set(new_rows) - set(old_rows) != skip or set(old_rows) - set(new_rows):
            raise SystemExit('%s: unexpected row set change' % name)
        for rid, values in old_rows.items():
            if new_rows[rid] != values:
                raise SystemExit('%s: stock row %d changed' % (name, rid))
    return archive


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('--client', default=DEFAULT_CLIENT, help='client folder (read-only)')
    ap.add_argument('--dbc-dir', help='read Map.dbc and MapDifficulty.dbc from this folder instead of the client')
    ap.add_argument('--out', help='output MPQ (default client_patch/<patch_name>)')
    args = ap.parse_args()

    cfg = clonedata.load_config()
    out = args.out or os.path.join(clonedata.MODULE, 'client_patch', cfg['patch_name'])
    out = os.path.abspath(out)
    if os.path.commonpath([out, os.path.abspath(args.client)]) == os.path.abspath(args.client):
        raise SystemExit('refusing to write into the client folder; the user copies the patch in')

    clones = clonedata.enabled(cfg)
    if not clones:
        raise SystemExit('no enabled clone in clones.json')

    map_dbc, md_dbc, where = clonedata.source_dbcs(args.client if not args.dbc_dir else None, args.dbc_dir,
                                                   cfg['patch_name'])
    for name, arch in where.items():
        print('source %-18s %s' % (name, arch))
    sources = {'Map.dbc': dbc.Dbc.parse(map_dbc.serialize()), 'MapDifficulty.dbc': dbc.Dbc.parse(md_dbc.serialize())}

    added = clonedata.add_clones(map_dbc, md_dbc, clones)
    files = {clonedata.DBC_MAP: map_dbc.serialize(), clonedata.DBC_MAPDIFFICULTY: md_dbc.serialize()}

    fog_cfg = fogdata.load_config()
    client = args.client if not args.dbc_dir else None
    raw_effects, where['ScreenEffect.dbc'] = clonedata.effective_raw(client, args.dbc_dir, 'ScreenEffect.dbc',
                                                                     cfg['patch_name'])
    raw_spells, where['Spell.dbc'] = clonedata.effective_raw(client, args.dbc_dir, 'Spell.dbc', cfg['patch_name'])
    print('source %-18s %s\nsource %-18s %s' % ('ScreenEffect.dbc', where['ScreenEffect.dbc'], 'Spell.dbc',
                                                where['Spell.dbc']))
    effects, spells = dbc.Dbc.parse(raw_effects), dbc.RawDbc(raw_spells)
    fogs = fogdata.add_fogs(effects, spells, fog_cfg)
    files[fogdata.DBC_SCREEN_EFFECT] = effects.serialize()
    files[fogdata.DBC_SPELL] = spells.serialize()

    # Looks: each clone's own light (thick fog, sky of its colour) and its source's WMOs with one fog entry.
    light_src = lightdata.sources(client, args.dbc_dir, cfg['patch_name'])
    light_stock = {n: dbc.Dbc.parse(t.serialize()) for n, (t, _) in light_src.items()}
    light_tables = {n: t for n, (t, _) in light_src.items()}
    light_added = {n: [] for n in lightdata.DBCS}
    looks = []
    for c in clones:
        if not c.get('look'):
            continue
        look = cfg['looks'][c['look']]
        light, params = 2600 + c['id'] - 900, 950 + 2 * (c['id'] - 900)
        for n, ids in lightdata.add_light(light_tables, c['id'], c['source'], light, params, look).items():
            light_added[n] += ids
        directory = sources['Map.dbc'].string(sources['Map.dbc'].row(c['source'])[1])
        wmos = wmofog.patch_files(args.client, directory, cfg['patch_name']) if client else {}
        files.update(wmos)
        looks.append((c, light, params, look, sorted(wmos)))
    if looks:
        for n, t in light_tables.items():
            files['DBFilesClient\\%s.dbc' % n] = t.serialize()

    os.makedirs(os.path.dirname(out), exist_ok=True)
    raw = mpq.write(out, files)
    archive = roundtrip(out, files, sources, added)
    check_fogs(archive, dbc.Dbc.parse(raw_effects), dbc.RawDbc(raw_spells), fogs)
    if looks:
        lightdata.check(archive, light_stock, light_added)
    for c, light, params, look, wmos in looks:
        print('look %s on clone %d: light %d, params %d/%d, fog %s yd colour %s; %d WMO(s) with one fog entry: %s'
              % (c['look'], c['id'], light, params, params + 1, look['fog_end_yd'], look['fog_colour'], len(wmos),
                 ', '.join(w.split('\\')[-1] for w in wmos)))

    digest = clonedata.checksum(map_dbc, md_dbc, added)
    sha = hashlib.sha256(raw).hexdigest()
    for c, row, md in added:
        m = clonedata.decode(map_dbc, row, clonedata.MAP_FIELDS)
        d = clonedata.decode(md_dbc, md, clonedata.MAPDIFFICULTY_FIELDS)
        print('clone %d <- %d  "%s"  Directory=%s type=%d flags=%d loading=%d corpse=%d (%.1f, %.1f) '
              'expansion=%d maxplayers=%d | MapDifficulty %d: map %d diff %d reset %d players %d'
              % (c['id'], c['source'], m[clonedata.MAP_NAME_ENUS], m[1], m[2], m[3], m[57], m[59], m[60], m[61],
                 m[63], m[65], d[0], d[1], d[2], d[20], d[21]))
    print('Map.dbc %d rows, MapDifficulty.dbc %d rows, ScreenEffect.dbc %d rows, Spell.dbc %d rows'
          % (len(map_dbc.rows), len(md_dbc.rows), len(effects.rows), spells.count))
    print('wrote %s (%d bytes)\nsha256 %s\nrows checksum %s' % (out, len(raw), sha, digest))

    manifest = {'patch': os.path.basename(out), 'sha256': sha, 'rows_checksum': digest,
                'clones': [c['id'] for c, _, _ in added], 'fogs': [f['spell'] for f, _, _ in fogs], 'sources': where}
    with open(out + '.json', 'w') as f:
        json.dump(manifest, f, indent=2)
        f.write('\n')


if __name__ == '__main__':
    main()
