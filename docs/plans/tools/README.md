# atlas-endgame tools (read-only)

Scripts that produced the numbers in the CLONES and SCOURGE appendices. They only read
`data/sql/base/db_world/*.sql` (paths are hardcoded in `maps.py`); they never touch a database or server.

Run with `python3 -I <script>` from this folder, in this order:

1. `maps.py` parses the SQL dumps into `cache.pkl` (about 6 s; delete it to re-parse).
2. `mapstats.py` writes `mapstats.json` (per-instance spawn statistics).
3. `clones_table.py` renders the catalog table from `mapstats.json` plus the judgement table inside it.
4. `scourge.py` writes `scourge.csv` (Scourge candidates); `pool.py` adds boss/C++-script flags → `pool.csv`.

5. `natives.py` writes `natives.json` (per-map native creatures, spells, script status) for the THEMES appendix.

`sqlparse.py` is a tiny mysqldump parser (`load(path) -> (columns, rows)`).
The base dump predates later `updates/`, so counts are approximate.

## Client data and dev tools (added 2026-11-24)

- `mpq.py`: read-only MPQ reader for the 3.3.5a client archives, with the client's archive order
  (`python3 -I mpq.py "<client dir>" <out dir> Map.dbc ...`). Never writes to the client.
- `client-dbc/`: effective client copies of Map, MapDifficulty, Light, AreaTable (2026-11-24).
- `dev/syntax.sh [files]`: syntax-only check of mod-atlas sources against AC headers (no build).
- `dev/tests.sh`: runs mod-atlas's unit tests with a tiny gtest stand-in.
These live here because the session scratchpad is wiped overnight.
