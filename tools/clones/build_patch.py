"""Build the Atlas client patch, Data/patch-T.MPQ: the client's own Map.dbc and MapDifficulty.dbc plus one row
each per enabled clone (clones.json). Reads the client archives read-only; writes only into
modules/mod-atlas/client_patch/ (gitignored). The user copies the MPQ into the client's Data folder.

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
import mpq  # noqa: E402

DEFAULT_CLIENT = '/Users/gaming/Desktop/World of Warcraft 3.3.5a'


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

    os.makedirs(os.path.dirname(out), exist_ok=True)
    raw = mpq.write(out, files)
    roundtrip(out, files, sources, added)

    digest = clonedata.checksum(map_dbc, md_dbc, added)
    sha = hashlib.sha256(raw).hexdigest()
    for c, row, md in added:
        m = clonedata.decode(map_dbc, row, clonedata.MAP_FIELDS)
        d = clonedata.decode(md_dbc, md, clonedata.MAPDIFFICULTY_FIELDS)
        print('clone %d <- %d  "%s"  Directory=%s type=%d flags=%d loading=%d corpse=%d (%.1f, %.1f) '
              'expansion=%d maxplayers=%d | MapDifficulty %d: map %d diff %d reset %d players %d'
              % (c['id'], c['source'], m[clonedata.MAP_NAME_ENUS], m[1], m[2], m[3], m[57], m[59], m[60], m[61],
                 m[63], m[65], d[0], d[1], d[2], d[20], d[21]))
    print('Map.dbc %d rows, MapDifficulty.dbc %d rows' % (len(map_dbc.rows), len(md_dbc.rows)))
    print('wrote %s (%d bytes)\nsha256 %s\nrows checksum %s' % (out, len(raw), sha, digest))

    manifest = {'patch': os.path.basename(out), 'sha256': sha, 'rows_checksum': digest,
                'clones': [c['id'] for c, _, _ in added], 'sources': where}
    with open(out + '.json', 'w') as f:
        json.dump(manifest, f, indent=2)
        f.write('\n')


if __name__ == '__main__':
    main()
