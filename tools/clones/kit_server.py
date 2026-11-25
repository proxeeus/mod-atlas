"""Install the kit's server collision in the play setup's client-data volume: one vmaps/<model>.vmo per kit piece
and its entry in vmaps/GameObjectModels.dtree (read once at worldserver start). Dry run by default.

  python3 -I kit_server.py              build into client_patch/server/vmaps/, show what --apply would change
  python3 -I kit_server.py --apply      back up the stock dtree (once), then copy the .vmo files and the merged dtree
  python3 -I kit_server.py --remove     put the stock dtree back and delete our .vmo files

The dtree is a stock file: the first --apply keeps it as vmaps/GameObjectModels.dtree.atlas-stock in the volume and
as a host copy under ~/atlas-backups/; --remove restores it from the volume copy. Entries for displays 9900-9949 are
replaced, every other entry is kept byte for byte. Needs Docker; touches only the volume, through a throwaway
container (ATLAS_LINK_IMAGE, default mysql:8.4). Restart the worldserver afterwards.
"""
import argparse
import datetime
import os
import subprocess
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

import clonedata  # noqa: E402
import kitvmo  # noqa: E402
import pooldata  # noqa: E402

VOLUME = os.environ.get('ATLAS_DATA_VOLUME', 'azerothcore-wotlk_ac-client-data')
IMAGE = os.environ.get('ATLAS_LINK_IMAGE', 'mysql:8.4')
OUT = os.path.join(clonedata.MODULE, 'client_patch', 'server', 'vmaps')
DTREE = 'GameObjectModels.dtree'
STOCK = DTREE + '.atlas-stock'


def run(script, mount_out=False, capture=True):
    cmd = ['docker', 'run', '--rm', '-v', '%s:/data' % VOLUME]
    if mount_out:
        cmd += ['-v', '%s:/in:ro' % OUT]
    cmd += [IMAGE, 'sh', '-c', script]
    res = subprocess.run(cmd, capture_output=capture)
    if res.returncode:
        raise SystemExit('docker failed (%d): %s' % (res.returncode, (res.stderr or b'').decode(errors='replace')))
    return res.stdout


def build():
    cfg = pooldata.load_config()
    os.makedirs(OUT, exist_ok=True)
    entries, names = [], []
    for p in cfg['pieces']:
        data = kitvmo.vmo(p['verts'], p['tris'])
        kitvmo.read_vmo(data)
        name = p['model'] + '.vmo'
        with open(os.path.join(OUT, name), 'wb') as f:
            f.write(data)
        entries.append(kitvmo.dtree_entry(p['display'], p['model'], p['verts']))
        names.append(name)
        print('built %-30s display %d, %d triangles' % (name, p['display'], len(p['tris'])))
    return entries, names


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    g = ap.add_mutually_exclusive_group()
    g.add_argument('--apply', action='store_true')
    g.add_argument('--remove', action='store_true')
    args = ap.parse_args()

    entries, names = build()
    if args.remove:
        quoted = ' '.join("'/data/vmaps/%s'" % n for n in names)
        run("set -e; cd /data/vmaps; if [ -f %s ]; then cp %s %s && rm %s; fi; rm -f %s; ls -l %s"
            % (STOCK, STOCK, DTREE, STOCK, quoted, DTREE), capture=False)
        print('removed: stock dtree restored (if a backup was there), kit models deleted. Restart the worldserver.')
        return

    has_stock = run("[ -f /data/vmaps/%s ] && echo yes || echo no" % STOCK).strip() == b'yes'
    stock = run("cat /data/vmaps/%s" % (STOCK if has_stock else DTREE))
    merged = kitvmo.merge_dtree(stock, entries, pooldata.DISPLAY_ID_RANGE)
    n_stock = len(kitvmo.read_dtree(stock))
    current = run("cat /data/vmaps/%s" % DTREE)
    print('volume %s: dtree %d bytes now, stock %d entries (%s), merged %d bytes (+%d entries)%s'
          % (VOLUME, len(current), n_stock, 'from the backup' if has_stock else 'the live file', len(merged),
             len(entries), ' — already installed' if merged == current else ''))
    with open(os.path.join(OUT, DTREE), 'wb') as f:
        f.write(merged)

    if not args.apply:
        print('dry run: --apply would copy %s and the merged %s into vmaps/%s'
              % (', '.join(names), DTREE, '' if has_stock else ' (keeping the stock one as %s)' % STOCK))
        return

    if not has_stock:
        backup = os.path.expanduser('~/atlas-backups/%s-kit-dtree' % datetime.date.today().isoformat())
        os.makedirs(backup, exist_ok=True)
        with open(os.path.join(backup, DTREE), 'wb') as f:
            f.write(stock)
        run("set -e; cp -p /data/vmaps/%s /data/vmaps/%s" % (DTREE, STOCK))
        print('backed up the stock dtree to vmaps/%s and %s' % (STOCK, backup))
    copies = '; '.join("cp '/in/%s' '/data/vmaps/%s'" % (n, n) for n in names)
    run("set -e; %s; cp /in/%s /data/vmaps/%s.new && mv /data/vmaps/%s.new /data/vmaps/%s; chmod 644 /data/vmaps/%s %s"
        % (copies, DTREE, DTREE, DTREE, DTREE, DTREE, ' '.join("'/data/vmaps/%s'" % n for n in names)),
        mount_out=True)
    check = run("cat /data/vmaps/%s" % DTREE)
    if check != merged:
        raise SystemExit('the dtree in the volume differs from the merged one')
    print('applied: %d kit model(s) and the merged dtree are in the volume. Restart the worldserver.' % len(names))


if __name__ == '__main__':
    main()
