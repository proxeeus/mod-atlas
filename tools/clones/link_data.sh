#!/usr/bin/env bash
# Server data links for the enabled clones (clones.json): in the worldserver's client-data volume, every terrain,
# collision and navmesh file of the source map gets a relative symlink under the clone's ID, e.g.
#   maps/9023232.map -> 0363232.map, vmaps/902.vmtree -> 036.vmtree, mmaps/902.mmap -> 036.mmap.
# The server finds these files by name only; none carries a map ID inside (STAGE2 3.1).
#
#   link_data.sh            dry run: lists what would be linked (mounts the volume read-only)
#   link_data.sh --apply    creates the links (writes to the play setup: needs the user's OK)
#   link_data.sh --remove   deletes the clones' links (symlinks only, never a real file)
#
# Only files named after a clone ID are created or removed; nothing else in the volume is touched. Real files under
# a clone's name are reported and left alone. Environment: ATLAS_DATA_VOLUME (default
# azerothcore-wotlk_ac-client-data), ATLAS_LINK_IMAGE (a local image with /bin/sh, default alpine:3.20).
set -euo pipefail

here="$(cd "$(dirname "$0")" && pwd)"
volume="${ATLAS_DATA_VOLUME:-azerothcore-wotlk_ac-client-data}"
image="${ATLAS_LINK_IMAGE:-alpine:3.20}"
mode="dry"

case "${1:-}" in
    "") ;;
    --apply) mode="apply" ;;
    --remove) mode="remove" ;;
    *) echo "usage: $0 [--apply | --remove]" >&2; exit 2 ;;
esac

pairs="$(python3 -I -c '
import json, sys
cfg = json.load(open(sys.argv[1]))
print(" ".join("%d:%d" % (c["id"], c["source"]) for c in cfg["clones"] if c.get("enabled")))
' "$here/clones.json")"

if [ -z "$pairs" ]; then
    echo "no enabled clone in clones.json" >&2
    exit 1
fi

if [ -z "${ATLAS_LINK_TEST_ROOT:-}" ] && ! docker volume inspect "$volume" > /dev/null 2>&1; then
    echo "volume $volume not found (is Docker running?)" >&2
    exit 1
fi

mount="/d:ro"
[ "$mode" = "dry" ] || mount="/d"

# Runs inside the container; ROOT is the volume root.
# shellcheck disable=SC2016
script='
set -eu
patterns() { # $1 = 3-digit map ID
    echo "maps $1[0-9][0-9][0-9][0-9].map"
    echo "vmaps $1.vmtree"
    echo "vmaps $1_[0-9][0-9]_[0-9][0-9].vmtile"
    echo "mmaps $1.mmap"
    echo "mmaps $1[0-9][0-9][0-9][0-9].mmtile"
}
for pair in $PAIRS; do
    c=$(printf %03d "${pair%%:*}"); s=$(printf %03d "${pair##*:}")
    if [ "$MODE" = remove ]; then
        patterns "$c" | while read -r dir glob; do
            [ -d "$ROOT/$dir" ] || continue
            cd "$ROOT/$dir"
            for f in $glob; do
                [ -e "$f" ] || [ -L "$f" ] || continue
                if [ -L "$f" ]; then rm "$f"; echo "removed $dir/$f"; else echo "LEFT real file $dir/$f"; fi
            done
        done
        continue
    fi
    patterns "$s" | while read -r dir glob; do
        [ -d "$ROOT/$dir" ] || continue
        cd "$ROOT/$dir"
        for f in $glob; do
            [ -e "$f" ] || continue
            new="$c${f#"$s"}"
            if [ -L "$new" ]; then
                if [ "$(readlink "$new")" = "$f" ]; then echo "ok      $dir/$new -> $f"
                else echo "SKIP    $dir/$new links to $(readlink "$new"), not $f"; fi
            elif [ -e "$new" ]; then
                echo "SKIP    $dir/$new is a real file"
            elif [ "$MODE" = apply ]; then
                ln -s "$f" "$new"; echo "linked  $dir/$new -> $f"
            else
                echo "would   $dir/$new -> $f"
            fi
        done
    done
done
'

echo "volume $volume, clones $pairs, mode $mode"
if [ -n "${ATLAS_LINK_TEST_ROOT:-}" ]; then # self-test against a local folder instead of the volume
    PAIRS="$pairs" MODE="$mode" ROOT="$ATLAS_LINK_TEST_ROOT" sh -c "$script"
    exit
fi
docker run --rm --network none -e "PAIRS=$pairs" -e "MODE=$mode" -e ROOT=/d -v "$volume:$mount" -w /d "$image" \
    sh -c "$script"
