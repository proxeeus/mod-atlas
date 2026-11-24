# Clone tools (Atlas stage 2)

Clone maps (IDs 900-949) reuse a stock dungeon's terrain under a new map ID: a copy of the source's `Map.dbc` row
with the same `Directory`, so the client loads the same WDT/WMO files and the server the same `maps/`, `vmaps/`,
`mmaps/` files (through symlinks). `clones.json` says which clones exist; only `enabled` ones are built.

The same patch carries the **fogs** (`fogs.json`): swirling-fog screen effects (ScreenEffect.dbc type 3) on aura-260
spells (Spell.dbc), drawn over the scene so a dungeon's own WMO fog can't hide them, picked per theme, corruption
level or affix. The spells also go to the server's `spell_dbc`; the ScreenEffect rows are client-only.

Run with `python3 -I` from anywhere; nothing here writes to the client folder or to a database.

| Tool | What it does |
|---|---|
| `build_patch.py` | Reads the client's effective `Map.dbc`, `MapDifficulty.dbc`, `ScreenEffect.dbc` and `Spell.dbc` (read-only, archive order as the client, our own `patch-T.MPQ` left out), appends the clone and fog rows and writes `client_patch/patch-T.MPQ` (gitignored, about 49 MB: Spell.dbc is whole) plus a `.json` manifest. Round-trips the archive and checks that no stock row changed. |
| `gen_sql.py` | Writes `atlas_clones.sql` (`map_dbc`, `mapdifficulty_dbc`, `instance_template`, `atlas_map_source`), `atlas_fogs.sql` (`spell_dbc` 90400-90449, `atlas_fog`) and their uninstall scripts in `data/sql/uninstall/` from the same rows. Each waits in `data/sql/staged/` (never applied) until `--install clones` / `--install fogs` moves it to `data/sql/db-world/base/`. `--check` only compares. |
| `verify.py` | Compares the built patch with the SQL, field by field (clones, fog spells, their screen effects). |
| `link_data.sh` | Data symlinks in the worldserver's client-data volume: dry run by default, `--apply`, `--remove`. |
| `dbc.py`, `mpq.py`, `clonedata.py`, `fogdata.py` | WDBC reader/writer (and a raw mode for Spell.dbc), MPQ reader/writer and client archive order, the shared clone and fog rows. |

## Adding or changing a clone

1. Edit `clones.json` (next free ID in 900-949, next MapDifficulty ID 754+).
2. `build_patch.py`, `gen_sql.py`, `verify.py` (must print `OK`).
3. Install in order (STAGE2 6): world DB backup, `gen_sql.py --install` and a build (the rows), `link_data.sh
   --apply` and a restart, the patch in the client (`Data\patch-T.MPQ`), then `Atlas.Clones.Enable = 1`. Roll back
   in reverse. On the play setup, module SQL is applied by `ac-db-import` from the modules baked into its image, so
   whatever sits in `data/sql/db-world/base/` lands with the next image build. That is why the clone rows wait in
   `staged/` until the backup is taken.

## Why `patch-T.MPQ`

The client sorts `Data\patch-?.MPQ` and `Data\<locale>\patch-<locale>-?.MPQ` descending and gives the first the
highest priority, so `patch-T` sits above every stock and custom patch. A patch replaces whole DBC files, so the
builder always starts from the client's current effective copies.

## An unpatched client must never be sent to a clone

Without the `Map.dbc` row the client stays on its loading screen for good (Wowdecomp2 `Client.cpp`,
`Packet_SMSG_NEW_WORLD`: "Bad SMSG_NEW_WORLD zoneID", no world load, no worldport ack). Runs therefore only open in a
clone for accounts that opted in (`.atlas clones on`), with `Atlas.Clones.Enable = 1`.
