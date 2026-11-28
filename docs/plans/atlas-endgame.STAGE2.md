# Atlas stage 2: clones (implementation brief)

Written 2026-11-24 at the end of the session that finished stage 1, for a **fresh session** to implement stage 2
without that conversation. Read this first, then the sources it points to.

## 0. Start here

1. Read `AGENTS.md` (repo root) and the docs it makes mandatory for the task: `.agents/docs/cpp-guidelines.md`,
   `.agents/docs/sql-guidelines.md`, `.agents/docs/self-review-rules.md` before committing.
2. Read this file in full. Then, as needed:
   - `atlas-endgame.PLAN.md` §3 (stage 2 as first planned; this file supersedes it where they differ),
   - `atlas-endgame.CLONES.md` (starter set, catalog, zone side effects §4, per-clone checklist §5),
   - `atlas-endgame.DESIGN.md` §6.1 (clones), §17 (client patch), §19 (data model, reserved IDs),
   - `modules/mod-atlas/README.md` (what the module does today).
3. Memory: `~/.claude/projects/-Users-ac-playerbots-azerothcore-playerbots-azerothcore-wotlk/memory/` (play server
   rules, local test setup, the module's history).

## 1. Where things stand (2026-11-24)

- **Module:** `modules/mod-atlas` (own git repo, `origin` = github.com/proxeeus/mod-atlas, branch `main`). Last pushed
  commit `108122b`. Stage 1 is **done**: a player plus bots open a map (Echo Sigil) at the Timeways Lens in Dalaran,
  clear it, the master is summoned by the forces bar, the Cache of the Keepers gives gear and new sigils; ~30 affixes,
  Scourge corruption by tier, level-80 role scaling, atmosphere (light overrides), run HUD in the AtlasChronicle addon.
- **Runs today use stock dungeon instances** (V0's runner): stock creatures and gameobjects are cleared or swept,
  doors opened, stock encounters reset, the layout comes from the stock spawns (`StockLayoutProvider`).
  Stage 2 replaces that with **clones**: the same terrain under new map IDs (900+), with no stock spawns, no stock
  instance script, no quest gates.
- **Key code** (all under `modules/mod-atlas/src`): `AtlasMgr.{h,cpp}` (sessions, update loop), `AtlasPopulation.cpp`
  (populate, AI, forces bar, master summon), `AtlasMaps.cpp` (sigils, Lens, rifts, cache, in-map spawn queue),
  `StockLayoutProvider.{h,cpp}` + `AtlasLayout.h` (the layout seam: `LayoutProvider` interface, `GetLayout(mapId)`),
  `AtlasConfig.{h,cpp}`, `scripts/` (allmap: populate on enter, `OnMapUpdate` → `ProcessMapSpawns`; chronicle addon
  protocol; commands `.atlas ...`), pure headers with unit tests in `tests/`.
- **Echoes and the Endless Timeways** may stay on stock instances (decide in 2.6).

## 2. Rules from the user (still binding)

- **No core edits.** Never edit mod-playerbots or other modules. Everything lives in `modules/mod-atlas`.
- **No second Docker stack.** The user decided (2026-11-24): "don't do a new docker setup, we'll use mine". Clones
  are developed on the **play setup** (`docker-compose.yml` + `docker-compose.override.yml`, containers
  `ac-database`, `ac-worldserver`, `ac-authserver`, `ac-db-import`, `ac-client-data-init`). This overrides PLAN
  decision D5 and PLAN §3's "never on the play server". So every change must be **additive, reserved-ID only,
  reversible, and off by default** (see §6).
- **"Don't f* the DB up."** Read-only queries are fine any time. Writes only through the module's own SQL files
  (reserved IDs, idempotent, DELETE-before-INSERT of the module's own rows) or with the user's explicit OK.
  Take a backup before the first install of clone rows (§6).
- **Client:** the client patch is **one MPQ file only**, which the **user copies** into the client themselves. Never
  write into the client folder (`/Users/gaming/Desktop/World of Warcraft 3.3.5a`); reading its archives is fine
  (`tools/mpq.py`). **Wowdecomp2 stays pristine** (`~/azerothcore-playerbots/wowdecomp2`): read it to answer client
  questions, never edit it ("minus actual WoW bugs, for now").
- **Never configure or build.** The user builds. Use the syntax check and the unit-test harness (§8).
- **Push only when asked.** Commit messages end with `Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>`.
  Credit upstream authors when mirroring other cores' code (AGENTS.md).
- **Style:** AC C++ style, `apps/codestyle/codestyle-cpp.py` must pass (run it from the module dir), 120 columns,
  no double blank lines, east const, `{}` format strings, no `<random>` (the `RunRng` exception is documented).
  Module SQL: in the module's `data/sql`, idempotent (a changed base file is re-applied), DELETE before INSERT,
  InnoDB.
- When the play server's `env/dist/etc/modules/mod_atlas.conf` needs new keys: check it only differs from the last
  committed dist by `Atlas.Enable = 1` (no user edits), back it up as `.bak-before-<step>`, write the new dist with
  `Atlas.Enable = 1`, and tell the user.

## 3. Facts established (with sources)

### 3.1 Server side: clone maps need only SQL and symlinks

- AC loads `Map.dbc`, `MapDifficulty.dbc` and `Light.dbc` from the data folder **and** merges the world tables
  `map_dbc`, `mapdifficulty_dbc`, `light_dbc` (`src/server/game/DataStores/DBCStores.cpp:342-347`, `LOAD_DBC`).
  So clone maps are added by SQL rows; the server's DBC files stay untouched. `map_dbc` is empty in the base data.
- Columns (from `data/sql/base/db_world/*.sql`):
  - `map_dbc`: `ID, Directory, InstanceType, Flags, PVP, MapName_Lang_{enUS..itIT,Unk}, MapName_Lang_Mask,
    AreaTableID, MapDescription0_Lang_*, MapDescription0_Lang_Mask, MapDescription1_Lang_*,
    MapDescription1_Lang_Mask, LoadingScreenID, MinimapIconScale, CorpseMapID, CorpseX, CorpseY,
    TimeOfDayOverride, ExpansionID, RaidOffset, MaxPlayers` (66 values, same order as the client DBC).
  - `mapdifficulty_dbc`: `ID, MapID, Difficulty, Message_Lang_*, Message_Lang_Mask, RaidDuration, MaxPlayers,
    Difficultystring` (23 values).
  - `instance_template`: `map, parent, script, allowMount`. Stock rows of the starter sources:
    `(33,0,'instance_shadowfang_keep',0)`, `(36,0,'instance_deadmines',0)`, `(329,0,'instance_stratholme',0)`,
    `(600,571,'instance_drak_tharon_keep',1)`, `(602,571,'instance_halls_of_lightning',0)`.
    Clones get `script = ''` (no stock instance script) and the source's `parent` and `allowMount`.
- **Always install a clone's `map_dbc`, `mapdifficulty_dbc` and `instance_template` rows together** (DESIGN Q3:
  an instance map row without `instance_template` can abort the server).
- Terrain, collision and navmesh files are found **by name only** and carry no map ID inside:
  - `maps/{:03}{:02}{:02}.map` (`src/server/game/Grids/GridTerrainLoader.cpp:31,96`),
  - `vmaps/{:03}.vmtree` and `vmaps/{:03}_{:02}_{:02}.vmtile` (`src/common/Collision/Maps/MapTree.cpp`,
    `VMapMgr2::getMapFileName`, `getTileFileName`; the headers are checked for `VMAP_MAGIC` only),
  - `mmaps/{:03}.mmap` and `mmaps/{:03}{:02}{:02}.mmtile` (`src/common/Collision/Management/MMapMgr.h:48-49`;
    `MmapTileHeader` in `src/common/Collision/Maps/MapDefines.h:64` has magic, versions, size, liquids, no map ID).
  So clone 900 can use Drak'Tharon's files through **relative symlinks** (`900xxyy.map -> 600xxyy.map`, etc.)
  in the client-data volume. Instance maps that are WMO-only may have few or no `.map` files; link whatever exists
  with the source's prefix.
- The client-data volume is `azerothcore-wotlk_ac-client-data`, mounted **read-only** into the worldserver
  (`docker-compose.yml`: `${DOCKER_VOL_DATA:-ac-client-data}:/azerothcore/env/dist/data/:ro`) and read-write into
  `ac-client-data-init`. That init container downloads `data.zip` only when the data version changes and unzips with
  `-o` (overwrite), never deleting (`apps/installer/includes/functions.sh:160-185`): **added symlinks survive it**.
  Write to the volume with a one-shot container, for example
  `docker run --rm -v azerothcore-wotlk_ac-client-data:/d alpine sh -c 'cd /d/mmaps && ln -s ...'` (with the
  user's OK: it is a write to the play setup). Read it the same way with `:ro`.

### 3.2 Client side: one MPQ, named `patch-T.MPQ`, in `Data\`

- The client lists `Data\patch-?.MPQ` and `Data\<locale>\patch-<locale>-?.MPQ` together, sorts them **descending**
  by path (case-insensitive), then appends `Data\patch.MPQ` and `Data\<locale>\patch-<locale>.MPQ`
  (Wowdecomp2 `WoW/Source/WowServices/PatchFiles.cpp`). It opens the list from the end with increasing priority,
  so the **first entry wins** (`WoW/Source/Client.cpp:1361-1374`).
- Resulting priority on this client, highest first: `Data\Patch-A.MPQ` (the user's), `Data\patch-3`, `patch-2`,
  `Data\enUS\patch-enUS-N`, `-M`, `-3`, `-2`, `Data\patch.MPQ`, `Data\enUS\patch-enUS.MPQ`, then the base archives.
  **`Data\patch-T.MPQ` would sit above all of them** (`T` > `A`; "T" for Timeways). Use that name; never reuse
  `Patch-A` (the user's own patch).
- A patch **replaces whole DBC files**. Who currently supplies what (read with `tools/mpq.py`, 2026-11-24):

  | Archive | DBCs it holds (of those that matter) |
  |---|---|
  | `Data\Patch-A.MPQ` (user, 26 KB) | CharBaseInfo, CharStartOutfit, SkillRaceClassInfo |
  | `enUS\patch-enUS-3.MPQ` (stock) | **Map, MapDifficulty, Light**, AreaTable, LoadingScreens, Spell, SpellItemEnchantment, Item, DungeonEncounter, WorldMapArea, AreaTrigger, LFGDungeons |
  | `enUS\patch-enUS-M.MPQ` (custom) | WorldMapArea |
  | `enUS\patch-enUS-N.MPQ` (custom) | **AreaTable**, WorldMapArea |

  So `Map.dbc`, `MapDifficulty.dbc` and `Light.dbc` can be built from the stock `patch-enUS-3` copies. If the patch
  ever ships `AreaTable.dbc` or `WorldMapArea.dbc`, it **must start from the user's patch-enUS-N / -M versions**,
  or it silently breaks the user's custom patches.
- Copies of the effective client DBCs: `tools/client-dbc/` (Map, MapDifficulty, Light, AreaTable). Re-extract with
  `python3 -I tools/mpq.py "/Users/gaming/Desktop/World of Warcraft 3.3.5a" tools/client-dbc Map.dbc ...`
  (stock archive order only; `extract()` takes an `order` list to include custom patches).
- Client `Map.dbc`: 66 fields per row, 135 rows, highest ID 724 (900-949 free). `MapDifficulty.dbc`: 23 fields,
  187 rows, highest ID 753. Source rows:

  | Map | Directory | Type | Flags | LoadingScreen | Corpse map, x, y | Expansion | MapDifficulty rows (diff 0 / 1) |
  |---|---|---|---|---|---|---|---|
  | 33 Shadowfang Keep | `Shadowfang` | 1 | 0 | 204 | 0, -231.0, 1571.6 | 0 | 7 / - |
  | 36 Deadmines | `DeadminesInstance` | 1 | 0 | 142 | 0, -11207.8, 1681.2 | 0 | 10 / - |
  | 329 Stratholme | `Stratholme` | 1 | 1 | 101 | 0, 3392.3, -3378.5 | 0 | 30 / - |
  | 600 Drak'Tharon Keep | `DrakTheronKeep` | 1 | 13 | 228 | 571, 4774.6, -2023.0 | 2 | 96 / 151 (86400 s) |
  | 602 Halls of Lightning | `Ulduar80` | 1 | 13 | 231 | 571, 9185.1, -1386.8 | 2 | 98 / 153 (86400 s) |

- **A clone row = the source row with a new ID and the same `Directory`**: the client then loads the same WDT/WMO
  files, so the patch needs no terrain copies. Name it in lore (e.g. "Echo of Drak'Tharon Keep") or keep the stock
  name; it shows on the loading screen and in instance info. MapDifficulty rows: copy the source's normal row with
  new IDs (754+ free; only difficulty 0, runs are Normal-only).
- Reading and writing DBCs: `tools/dbc.py` reads (`load(path) -> rows, string getter`; `f32()` for floats); a writer
  has to rebuild the string block. Both client and server sides must get **identical rows**.

### 3.3 What a clone changes in play (expected, to verify)

- **No stock spawns at all** (creature/gameobject rows are keyed by the source map ID): nothing to clear, no doors
  (they are gameobjects), but also **no structural gameobjects** (elevators, bridges): check each clone for parts
  only reachable through a gameobject (CLONES §5 step 5).
- **No stock instance script** (`instance_template.script = ''`): no encounter states, no door logic.
- **No quest or item-level gates** (`access_requirement` is keyed by map ID).
- **Zones come from the terrain**: area IDs inside the tiles keep the source's zone, so zone-keyed effects follow:
  `spell_area` gives the **Wintergrasp buff in Drak'Tharon Keep and Halls of Lightning** (CLONES §4). Strip it, or
  start with the three starters that have none (Deadmines, Shadowfang Keep, Stratholme).
- **Graveyards** are chosen by zone: a ghost would release to the stock dungeon's graveyard. Runs already block
  release and auto-resurrect at the entrance; `OnPlayerBeforeChooseGraveyard` (hub graveyard) is the later fix.
- **Entrances**: no `areatrigger_teleport` for clone IDs; the module teleports to the layout entrance itself (as now).
- **Layouts**: the geometry is identical, so the **source map's stock layout works for the clone unchanged**
  (same coordinates). `StockLayoutProvider` can serve clone ID → source layout; frozen layouts (PLAN 2.3) come later.

## 4. Open questions (settle early, cheapest first)

| # | Question | How to settle |
|---|---|---|
| A | What does an **unpatched** client do when sent to map 900 (no Map.dbc row)? Must never happen. | Wowdecomp2 (world load / `SMSG_NEW_WORLD` handling, read-only), then gate (B). |
| B | **Patch detection.** Options: (1) a per-account opt-in, `.atlas clones on` or a Lens option, stored server-side: the user plays alone with bots, bots need no client; (2) the patch adds a Spell.dbc marker row and the addon tests `GetSpellInfo(id)` (needs the 49 MB Spell.dbc in the patch: better once the patch carries spells anyway); (3) a file inside the MPQ the addon can see, if the client loads addon files from MPQs. | Recommend (1) for now; record (2)/(3) for later. |
| C | Does the client need **AreaTable** rows for a clone (zone text, minimap)? Area IDs from the tiles point to rows whose ContinentID is the source map. | Try in game with the first clone; only then consider AreaTable (built on patch-enUS-N's version). |
| D | Does each clone need its own **Light.dbc** rows (the dungeon's native lights are keyed by map ID)? | Optional: the atmosphere already overrides via global anchors. Copy rows (client patch + `light_dbc`) only if needed. |
| E | **MPQ writer** correctness. | Round-trip with `tools/mpq.py`; then the client. Uncompressed single-unit files are enough; encrypt hash and block tables. |
| F | Exact file set to link per source (maps, vmaps, mmaps). | List the volume read-only: `docker run --rm -v azerothcore-wotlk_ac-client-data:/d:ro alpine ls /d/mmaps \| grep ^600`. |
| G | Does anything at startup validate `map_dbc` rows against the server's own DBC files or `instance_template` (beyond Q3)? | Read `ObjectMgr::LoadInstanceTemplate`, `MapMgr`, `DBCStores` (read-only) before installing rows. |

## 5. Work packages

### 2.1 Clone tooling (module `tools/clones/`, committed; output not committed)
- `dbc.py`: read and **write** WDBC files (records + string block).
- `mpq.py`: read (copy `.agents/plans/atlas-endgame/tools/mpq.py`) and **write** a v1 MPQ (hash + block tables,
  encrypted; uncompressed files; optional `(listfile)`).
- `clones.json` (or SQL-driven): clone ID → source ID, name, enabled.
- `build_patch.py`: reads the client's effective `Map.dbc` / `MapDifficulty.dbc` (via `mpq.py`), appends the clone
  rows, writes `patch-T.MPQ` into `modules/mod-atlas/client_patch/` (gitignored). The user copies it into their
  client's `Data\`. Print a checksum and the rows added.
- `gen_sql.py`: writes the server rows from the same source rows, so client and server never drift.
- `link_data.sh`: prints (dry run by default) and, with `--apply`, runs the one-shot container that creates the
  relative symlinks for each clone ID; `--remove` deletes them. Never touches other files.
- **Done when:** the patch round-trips through the reader with the right rows; the SQL matches the patch rows.

### 2.2 Server rows (module SQL), off by default
- `data/sql/db-world/base/atlas_clones.sql`: `map_dbc`, `mapdifficulty_dbc`, `instance_template` rows for the enabled
  clones (all three per clone, one file), and the registry `atlas_map_source` (clone ID, source ID, enabled, name).
  Inert until used: nothing teleports into a clone unless the module does.
- `data/sql/uninstall/atlas_clones_uninstall.sql` (not auto-applied): deletes exactly those rows.
- **Before the first restart with these rows: a full `mysqldump` of `acore_world`** (ask the user; store outside
  the repo). Then restart and read `Server.log` / `Errors.log` for map and instance-template errors.

### 2.3 Data links
- Run `link_data.sh --apply` for the starter clones (user's OK), restart, and check the worldserver can create the
  instance (a GM teleport `.go xyz <x> <y> <z> 902` with the patch installed, or `.tele` to the entrance).

### 2.4 Module: runs on clones
- `Atlas.Clones.Enable` (default 0) and the opt-in (question B).
- `atlas_map_source` loaded at startup; the layout for a clone ID is the source's layout
  (`StockLayoutProvider` builds or aliases it); entrance from the source's area trigger.
- Map runs (sigils) open in the clone of their source when clones are enabled, the clone is enabled and every real
  player of the party opted in; otherwise as today. Echoes and the Endless Timeways: keep stock for now.
- Skip what clones make useless (clearing stock spawns, opening doors, encounter resets), keep it for stock runs.
- Zone strip for DTK/HoL (Wintergrasp buff) before enabling 900/903; graveyard override later.
- **Done when:** with the patch, a tier-1 sigil for The Deadmines opens clone 902: party enters, packs spawn, no
  stock creatures or doors, the master comes at the forces bar, the cache works, the run closes, the instance is
  released; without the opt-in (or with clones off), the same sigil runs in the stock Deadmines as today.

### 2.5 Later (from PLAN §3)
- Frozen layouts (`atlas_layout_*`, `CloneLayoutProvider`, `.atlas layout show`), wing selection for Stratholme.
- Graveyards (`OnPlayerBeforeChooseGraveyard`), patrols, structural gameobjects.
- Patch growth: affix spells with icons (Spell.dbc), Item.dbc rows for sigils, screen effects (ATMOSPHERE §4).

## 6. Safety on the play setup

1. Every ID from the reserved ranges (DESIGN §19): maps 900-949, MapDifficulty 754+, instance_template by map ID.
2. Install order: DB backup → SQL rows (restart) → data links → patch in the client (user) → `Atlas.Clones.Enable 1`.
3. Rollback in reverse: `Atlas.Clones.Enable 0` → remove the patch (user) → `link_data.sh --remove` → uninstall SQL.
4. Never teleport a player into a clone without the opt-in. Bots go with their master (they have no client).
5. Check `Errors.log` after every restart for `map_dbc`, `instance_template`, MMAP or VMAP errors on 9xx maps.
6. **Nothing irreversible** (user, 2026-11-24). Before each write to the play setup: a mysqldump of every database
   it touches (`acore_world` and `acore_characters`), its undo command ready, and after any undo a check that the
   touched tables match the dump (`map_dbc`, `mapdifficulty_dbc`, `instance_template`, `updates`,
   `atlas_map_source`, `atlas_clone_optin`). A step without a clean undo is not done.

## 7. Starter order

902 Deadmines → 904 Shadowfang Keep → 901 Stratholme (no zone side effects), then 900 Drak'Tharon Keep and
903 Halls of Lightning after the Wintergrasp strip. One clone end to end first, then the rest.

## 8. Dev tools (durable copies, outside the scratchpad)

The session scratchpad gets wiped overnight; these live in `.agents/plans/atlas-endgame/tools/` (gitignored):
- `tools/dev/syntax.sh [files]`: `clang++ -fsyntax-only` of module sources against the real AC headers (no build).
  Clean means "syntax: clean". (Header files produce a harmless c-header warning; pass `.cpp` files.)
- `tools/dev/tests.sh`: compiles `modules/mod-atlas/tests/*.cpp` with a tiny gtest stand-in and runs them
  (51 tests at the end of stage 1).
- `tools/mpq.py`: read-only MPQ reader and the client archive order; `tools/dbc.py`: DBC reader.
- Style: `cd modules/mod-atlas && python3 ../../apps/codestyle/codestyle-cpp.py`.
- Docker is sometimes off (the user stops the stack): then read world data from `data/sql/base/db_world/*.sql`
  (`tools/sqlparse.py`) and client data through `tools/mpq.py`.

## 9. Progress (2026-11-24, implementation session)

Module commit `9be88bb` (not pushed): tools (2.1), clone 902 rows (2.2, staged), module support (2.4).

**A, settled (Wowdecomp2, read-only):** an unpatched client sent to an unknown map hangs on the loading screen
for good. `Packet_SMSG_NEW_WORLD` / `SMSG_LOGIN_VERIFY_WORLD` (Client.cpp:944-1002) log "Bad SMSG_NEW_WORLD
zoneID" and return; the result is ignored (NetClient.cpp:384), `LoadNewWorld` never runs, no worldport ack, and
nothing disables the loading screen. No crash, no disconnect. Gate (B) is mandatory: done as the opt-in.
With a row (Directory = source): the world loads from the Directory only (WorldClient/Map.cpp:3008); Light.dbc
falls back to Light 1 (no rows for the clone's ContinentID, DayNight.cpp:369-402: question D, the atmosphere
override may hide it); fog mode turns on for map IDs >= 530 (World.cpp:1402); minimap party dots hidden
(AreaTable ContinentID 36 != 902, MinimapFrame.cpp:2410); no world map / dungeon map (WorldMapArea by map ID);
zone text fine (area IDs from the terrain).

**G, settled (core, read-only):** `map_dbc` rows merge into sMapStore with no check against files or other
tables (DBCDatabaseLoader asserts the column count: 66 / 23). `LoadInstanceTemplate` only drops a template whose
map is unknown ("bad mapid"). Entering needs the template (`CANNOT_ENTER_UNINSTANCED_DUNGEON`) and a Normal
MapDifficulty row. A player saved in a clone whose instance is gone is sent to the homebind (no entrance trigger;
PlayerStorage.cpp:5376). The module also checks each clone at startup (AtlasCloneMaps.cpp).

**Install safety (corrected after a test restart):** the worldserver runs with `AC_UPDATES_ENABLE_DATABASES=0`;
module SQL is applied by `ac-db-import` from the modules baked into its image, i.e. only after the user rebuilds.
The clone rows wait in `data/sql/staged/` until `gen_sql.py --install` (after the DB backup), so a build never
installs them early. `atlas_clone_optin` (characters, empty table) is in base.

**Install log (2026-11-24):** full dumps of `acore_world` and `acore_characters` plus a snapshot of the touched
tables in `~/atlas-backups/2026-11-24-stage2/`; `gen_sql.py --install` done; `link_data.sh --apply` made 79 links
(36 maps, 13 vmaps, 30 mmaps), all resolve inside `ac-worldserver`. A restart on the old build loaded fine and left
the touched tables identical to the snapshot. Next: the user's build applies the rows (then check `DBImport.log`,
`Server.log` for "Atlas: clone map 902").

**Still open:** F (file list per source; Docker was off), C/D in game, all of 2.3 and the in-game test of 2.4.

**Installed (2026-11-24, after the user's build):** `ac-db-import` applied `atlas_clones.sql` and
`atlas_clones_characters.sql`; Server.log: "Atlas: clone map 902 (Echo of the Deadmines) echoes map 36." The
touched tables differ from the snapshot by exactly 4 rows (map_dbc 902, mapdifficulty_dbc 754, instance_template
902, updates). Play conf backed up as `mod_atlas.conf.bak-before-clones`, new dist written with `Atlas.Enable = 1`
and `Atlas.Clones.Enable = 1` (inert until an account opts in; `.reload config` applies it). Waiting on the
in-game test: patch-T.MPQ in the client, `.atlas clones on`, a tier-1 Deadmines sigil.

**First clone run (2026-11-24, user, Proxeeus + 4 bots):** tier-1 Deadmines sigil opened clone 902 ("map 36 in
clone 902"); 212 mobs, 0 stock creatures swept ("Cleared 0 tracked creatures from clone map 902"), the master
(Weegli Blastfuse, 7607) came at 150/318 forces, killed, "Map run 1 mended", EndSession success, binds released,
atlas_run_active empty, run log map_id 902, progress keyed to source 36. Errors.log: no new lines during the run.
Instance 4 (map 902) row remains until the 30-min unload delay, as for stock runs. Not Atlas: the rogue bot did no
damage because its saved playerbots combat strategies (`playerbots_db_store`, key `co`) hold `+melee` (a shaman
name) and no `assassin`; fix in game with a whisper `co +assassin`. Still to do: the control run (opt-in off →
stock Deadmines), C/D (AreaTable/Light) judged from what the user saw.

## 10. Generated-map spike (DESIGN 6.2), built 2026-11-24

Goal: retire the risks of kit maps on the user's own client (Wowdecomp2 native macOS, OpenGL) before any big build.
- Pool map 950 "The Timeways": Map.dbc copy of 36 with Directory `AtlasPool`; `World\Maps\AtlasPool\AtlasPool.wdt` in
  patch-T is empty (MVER 18, MPHD no flags, MAIN all zero, empty MWMO; `CMap::LoadWdt` reads only these and skips a
  missing WDL). Server: map_dbc/mapdifficulty 759/instance_template, no map/vmap/mmap files.
- One kit piece: gameobject 900100, type 14 (MAP_OBJECT, client `CGGameObject_C_Type_MapObj`, `CWorld::ObjectCreate`
  makes a CMapObjDef for `.wmo`), display 649 `MD_Goldmine_1Room.wmo` (106 x 65 yd, 5 groups, own MFOG end 69 yd).
  Display 649 is in the server's `vmaps/GameObjectModels.dtree` (`Md_Goldmine_1Room.wmo.vmo`): collision, LoS and
  floor height (`Map::GetHeight` includes the dynamic tree) work without re-extraction.
- Placed at (-266, -266, 0), mid-grid; entrance (0, -5, 0.5) local, from the WMO's collidable up-facing triangles.
- `.atlas spike [leave]` (player, opt-in, Atlas.Clones.Enable): player + bots beside them; arrivals rooted 5 s,
  then put on the floor the server sees; the piece is summoned from the map's first update with a Gigantic
  visibility override.
- Questions it answers in game: does the room render, collide for the player, show its own fog; do bots stand on it
  and follow without a navmesh (no mmap: straight-line paths); do creatures path? Next if yes: a navmesh tile
  baked for the piece (params-only 950.mmap + .mmtile), then a second piece and a joint.

### 10.x Kit survey (2026-11-25, after the two-mine joint)
The joint works but is open air. Stock art offers no snap-together tileset: 165 collidable WMO displays (no
tunnels), the 40Man AQ pieces are blockouts, dungeon doorways have no common size. Next: our own tileset
(standard doorway) as generated WMOs + generated server collision. See the shared handover note for details.
