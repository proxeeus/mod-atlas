# atlas-endgame: design document

Module: **mod-atlas** — repo `proxeeus/mod-atlas`, folder `modules/mod-atlas`.
Status: **V0 exists** (2026-11-22): the former mod-dungeon-master (a fork of trauntrow/mod-dungeon-master),
cleaned up, re-themed as the Keepers of Time and renamed. V0 runs "echoes" in *stock* dungeon instances
(the prototype mode of the roadmap) with no client patch. Everything else below is design: later phases
build on V0's run manager, scaling, loot and bot handling and move runs to clone map IDs. See
`atlas-endgame.ANALYSIS.md` for what V0 inherited and fixed. Written 2026-11-22.

This is the reference for the module. When implementation disagrees with this doc, either the code is
wrong or this doc gets updated in the same change. Section 24 lists every fact about AC or mod-playerbots
this design relies on, with where it was checked, so it can be re-checked after AC updates.

**Appendices** (same folder):
- `atlas-endgame.PLAN.md`: the implementation plan from V0 (stages, work packages, done-when checks,
  decisions D1-D6).
- `atlas-endgame.ATMOSPHERE.md`: lights, fog and screen effects catalogue for echo atmosphere.
- `atlas-endgame.CLONES.md`: clone catalog (every instance rated, tier bands, phase-1 starter set,
  zone-aura audit, per-clone checklist).
- `atlas-endgame.SCOURGE.md`: the Scourge creature pool (127 curated entries by archetype and pack role,
  rares, Scourge-touched natives, shadow entries).
- `atlas-endgame.THEMES.md`: native themes of the five phase-1 maps (85 entries by pack role, 25 stock-boss
  candidates, library bosses needed for DTK and HoL, Stratholme wing split, map-specific mechanics).
- `atlas-endgame.AFFIXES.md`: the affix list (91 affixes with mechanism, values per tier band, bot safety,
  phase-1 subset).
- `atlas-endgame.ANALYSIS.md`: review of `modules/mod-dungeon-master` (risks to the play server, lessons
  for Atlas, a test-stack prototype shortcut, and bot strategy registration via mod-dungeon-clear).
- `tools/`: the read-only scripts that parse `data/sql/base` to produce the numbers in those appendices.

---

## 1. Vision

A repeatable endgame for level 80, modelled on the Path of Exile map system, that keeps a character busy
while bot raid strategies are still being written.

- A run is a **map**: an item you open at a **map device**, which takes your group into an instance with
  random **affixes** (modifiers), random monster packs and random extra encounters.
- Maps drop maps, plus crafting currency to make maps harder and more rewarding. Higher tiers are harder
  and give better loot. An account-wide **atlas** tracks progress and unlocks passives.
- Playable solo, solo with playerbots, or as a group with bots.
- **Every map plays at level 80**, whatever expansion the original dungeon came from.
- Phase 1 uses **existing instances** (cloned). **Procedurally generated maps come later** and plug into the
  same system without changing anything else (section 6).

### Design pillars

1. **Variety from modifiers first, from layouts second.** PoE maps are fixed layouts; the variety is in the
   affixes, packs and mechanics. WoW already has ~60+ handcrafted dungeon and raid layouts.
2. **Everything on one level-80 curve.** Source data from three expansions and 5/10/20/25/40-player tuning is
   normalized by role, not scaled from its original numbers.
3. **Bot-playable by construction.** A mechanic or affix that bots can't handle is either excluded or flagged
   and rewarded as an opt-in.
4. **Zero diff against upstream.** No edits to AC core or mod-playerbots. AC updates must merge cleanly.

---

## 2. Hard constraints

| # | Constraint | Consequence |
|---|---|---|
| C1 | No edits to AC core (`src/`). | Only existing ScriptMgr hooks and public accessors. If a hook is missing, redesign; never patch core. |
| C2 | No edits to mod-playerbots. | It is pulled from upstream (`liyunfan1223/mod-playerbots`). Bot support uses only existing bot behaviour and public interfaces (section 16). |
| C3 | Nothing is pushed to AC upstream. | No "get the hook merged" plans. |
| C4 | AC updates are merged regularly. | One adapter file for AC internals, startup self-checks, frozen data snapshots (section 18). |
| C5 | Unpatched 3.3.5a clients keep working on the server. | The module is gated per session on a client-patch handshake. Unpatched players never receive custom IDs (section 17). |
| C6 | Client patches are allowed. | DBC/MPQ data patches for phases 1–5; binary edits (Wowdecomp2) only for the optional phase 6. |
| C7 | Repo rules from `AGENTS.md`. | Module SQL lives in the module's own `data/sql/`; core `data/sql/` is untouched. |
| C8 | Runs are reproducible from a seed. | Every random choice draws from a run-seeded RNG; the seed is persisted with the run. |

---

## 3. Glossary

| Term | Meaning |
|---|---|
| Map (item) | Consumable item describing one run: source map, tier, rarity, affixes. |
| Map device | Hub gameobject that consumes a map item and opens portals. |
| Run | One activation of a map item: one instance, one seed, one portal budget. |
| Source map | The original dungeon/raid a clone is based on (e.g. Deadmines, map 36). |
| Clone map | A new Map.dbc ID that reuses a source map's assets and server data files. |
| Wing | A subset of a large source map used as its own map (BRD Prison, Naxx Arachnid Quarter…). |
| Layout | Provider-neutral description of a map: entrance, exit, anchors, arenas, patrols, structural GOs. |
| Anchor | A position where a pack spawns, with a radius and a pack-size budget. |
| Affix | A modifier on a map, rolled onto the map item. |
| Tier | Map difficulty level, 1–16. |
| Rarity | Normal / magic / rare / corrupted: how many affixes a map can carry. |
| Encounter module | An extra mechanic placed inside a map (PoE "league mechanic" analog). |
| Atlas | Account-wide progression: completed maps, bonuses, passive points. |
| Kit | Set of modular room pieces used by generated maps (phase 5). |
| Pool map ID | A custom map ID used by at most one live generated run at a time (phase 5). |

---

## 4. The player loop

1. A level-80 character gets a starter set of tier-1 maps (quest or vendor in the hub).
2. They take a map item to the **map device** in the hub (Dalaran by default) and use it.
3. The device opens **portals** (default 6). The group, real players and bots, enters.
4. Inside: a cloned dungeon (or a wing of one), populated with level-80 packs from a theme, carrying the
   map's affixes, possibly with an encounter module (e.g. a Scourge Rift), ending with a boss.
5. Dying costs nothing directly, but re-entering uses a portal. Out of portals means the run is lost.
6. Killing the boss completes the map: atlas progress, a reward chest/drops, and **map drops** (same tier,
   chance of higher tier) so the loop sustains itself.
7. Crafting currency drops along the way. Players use it on map items to add, reroll or corrupt affixes,
   trading risk for loot quantity and rarity.
8. Completing maps grants atlas points for passives (more of a mechanic, better sustain, etc.).
9. At the top: **fragments** drop from high-tier maps and open **pinnacle encounters**.

Gear progression comes from tiered loot tables tied to the WotLK item level ladder (section 14), so maps
can gear a character to raid level while bot raids are not ready.

---

## 5. Architecture

```
                 ┌──────────────────────────────────────────────────────────┐
 Map item ─────▶ │ RunService: creates a Run {seed, source, tier, affixes}   │
 (enchant slots) └──────────────┬───────────────────────────────────────────┘
                                │ portals, instance creation, binds, teardown
                                ▼
       ┌────────────────────────────────────────────────────────┐
       │ LayoutProvider (interface)                             │
       │   CloneLayoutProvider  (phase 1: frozen harvested data)│
       │   KitLayoutProvider    (phase 5: generated, pool IDs)  │
       └──────────────┬─────────────────────────────────────────┘
                      │ Layout {entrance, exit, anchors, arenas, patrols, structGOs}
                      ▼
 ┌──────────────┐ ┌───────────────┐ ┌───────────────┐ ┌────────────────┐ ┌─────────────┐
 │ Population   │ │ Scaling       │ │ Affix engine  │ │ Encounters &   │ │ Rewards     │
 │ themes,packs │ │ role normalize│ │ handlers      │ │ bosses         │ │ loot, maps, │
 │ rares,patrols│ │ dmg/heal hooks│ │               │ │ mechanic lib   │ │ currency    │
 └──────────────┘ └───────────────┘ └───────────────┘ └────────────────┘ └─────────────┘
                      │
                      ▼
        Atlas (account progress, passives)  ◀──▶  Client addon UI (addon messages)
        Bot integration (existing bot behaviour; section 16)
        CoreAdapter (all touches of AC internals; section 18)
```

### Rules for the architecture

- **Only the LayoutProvider knows where a layout came from.** Population, scaling, affixes, encounters,
  rewards and the atlas consume a `Layout` and never ask whether it is a clone or generated. Adding
  procedural generation must not change any other component.
- **Only CoreAdapter touches AC internals** that are not stable script API: `MapCollisionData`, Detour
  structures, mmap file formats, `MMAP_VERSION`. Everything else uses ScriptMgr hooks and public methods.
- **All randomness goes through the run RNG** (C8). No `urand()` in run logic.

### Layout (provider-neutral data)

| Field | Content |
|---|---|
| `mapId` | The map ID the run happens on (clone ID or pool ID). |
| `entrance`, `exit` | Positions; `exit` is where the completion portal/chest appears. |
| `anchors[]` | Position, orientation, radius, pack budget, zone tag (corridor / room / arena edge), wing. |
| `arenas[]` | Boss arenas: centre, radius, entrance points, boss slot type (stock / library). |
| `patrols[]` | Waypoint lists verified walkable on the navmesh. |
| `structGOs[]` | Gameobjects the layout needs to be traversable (bridges, platforms). No doors. |
| `metadata` | Theme hints (indoor/outdoor, undead/demon/…), size class, expected run length. |

---

## 6. Map sources

### 6.1 Cloned instances (phase 1 onward)

Which maps, in what order, and the phase-1 starter set (Drak'Tharon Keep, Stratholme, Deadmines, Halls of
Lightning, Shadowfang Keep as clone IDs 900–904): `atlas-endgame.CLONES.md`.

A **clone** is a new map ID in `Map.dbc` whose `Directory` points at an existing dungeon's assets, e.g.
`PG_Deadmines` → the Deadmines directory.

**Client side (patch MPQ):** new `Map.dbc` rows, `MapDifficulty.dbc` rows (required) and copied `Light.dbc`
rows (recommended); optional rows are listed in section 17. The client loads the same terrain and WMOs under
the new ID (Q1, verified in the client source).

**Server side:**
- Same rows on the server, added as module SQL to the world DB tables `map_dbc` and `mapdifficulty_dbc`
  (AC merges these over the DBC files by ID at startup; Q3). No DBC files are replaced on the server.
  Clone `Map` rows: instance type 1 (dungeon), expansion ≤ 2, entrance fields pointing at the hub or -1.
- **Symlink** the source map's server data files under the clone ID. Map IDs only appear in the file
  *names*, not inside the files (verified, section 24):
  - `maps/{src:03}{x:02}{y:02}.map` → `maps/{clone:03}{x:02}{y:02}.map`
  - `vmaps/{src:03}.vmtree`, `vmaps/{src:03}_{y:02}_{x:02}.vmtile` → clone equivalents
  - `mmaps/{src:03}.mmap`, `mmaps/{src:03}{x:02}{y:02}.mmtile` → clone equivalents
  A small tool generates the symlinks from the `atlas_map_source` table. Zero disk cost.
- `instance_template` row for the clone ID with the module's instance script (parent 0). **Never leave a
  clone's map row without its `instance_template` row**: AC aborts the whole server if it creates an instance
  for a dungeon map with no template (Q3, section 18 uninstall order).

**What a clone gives for free:**
- Full collision, line of sight, heights and pathing (stock mmaps).
- **No database spawns, no stock scripts, no stock lockouts**: the world DB has no `creature`/`gameobject`
  rows for the clone ID.
- Every instance of a clone map is a run, so there is no "is this instance a run?" question.
- Any number of concurrent instances: nobody modifies the navmesh, so sharing it is fine.

**Wings.** Large maps (BRD, Molten Core, Naxxramas, Ulduar, ICC…) are split into wings. A wing is a subset
of anchors and one arena, entered at a chosen entrance. Several clone IDs may point at the same source if
that is simpler than wing selection inside one ID; prefer one clone ID per source with wings as data.

**Harvest pipeline (offline tool, output frozen as module SQL):**
1. Read the source map's `creature` rows: positions, entries, ranks.
2. Cluster positions into packs (density clustering, a few yards). Each cluster → an anchor with
   radius and pack budget from the original cluster size.
3. Boss rows (rank 3 / known boss entries) → arenas.
4. Original waypoint data near clusters → patrol candidates, re-validated against the navmesh.
5. `gameobject` rows of structural types (no doors, no scripted GOs) → `structGOs`.
6. Manual pass: name wings, set entrances/exits, drop silly anchors, mark theme hints.
7. Export as module SQL (`atlas_layout_*` tables).

**The export is frozen.** AC updates change world DB spawns. Layouts must not drift silently, so the harvest
output is committed module data, re-harvested only on purpose.

**Known limits:**
- Stock mmaps never include gameobject geometry. Anything walkable only thanks to a GO is unreachable for
  bots on stock maps too, so the harvest can ignore it.
- Elevators and transports (Ulduar, ICC…) are out of scope for phase 1; such wings get excluded or split.

### 6.2 Generated maps from kits (phase 5)

**Kit pieces:** modular room WMOs (corridor, 20×20 room, corner, T-junction, stairs, bridge, boss arena…),
one kit per theme (Utgarde, Nerubian, Scourge, Titan…), cut from existing dungeon art. Shipped as new
`GameObjectDisplayInfo` rows + model files in the client patch. Spawned as gameobjects on a grid.

**Collision and line of sight** are free: gameobject collision is per instance (`DynamicMapTree`), and floor
heights account for GOs (`Map::GetGameObjectFloor`). Re-run `vmap4_extractor` with the patched DBC/MPQ so the
new display IDs get collision models.

**Pathing without generating mmaps per map:**
- **Bake once, offline:** one Detour navmesh tile per kit piece per rotation (4), with the same Recast settings
  and area/flag conventions as `mmaps_generator`. Re-bake only when a piece changes or AC bumps
  Recast / `MMAP_VERSION`.
- **Assemble per run (milliseconds):** copy the tile for the chosen rotation, translate vertices, detail
  vertices, off-mesh connections and bounds to the grid cell, set tile `x/y/layer`, `addTile`. Detour links
  neighbours where border edges line up. Translation keeps the BV tree and border-portal sides valid; that is
  why rotation happens only at bake time. Remember Detour's axis order: WoW (x, y, z) ↔ Recast (y, z, x).

**Map-ID pool (how a run gets its own navmesh with zero core edits):**
- All instances of one map ID share the base map's navmesh. So each live generated run gets its **own pool
  map ID** (`PG_POOL_001`…`PG_POOL_N`), all pointing at one empty WDT. The ID is released at teardown.
- The base map's navmesh is created by AC from a **params-only** `mmaps/{poolId:03}.mmap` file
  (`dtNavMeshParams` with tile width = kit cell size, not the stock 533.33-yd `GRID_SIZE`).
- The module gets it through the public `MapCollisionData::GetMMapNavMeshSharedPtr()` (non-const
  `shared_ptr<dtNavMesh>`) and calls `addTile` / `removeTile`.
- AC's grid-load tile streaming finds no `.mmtile` files for pool IDs and does nothing (debug log only).
- One live run per pool ID → only that instance's map thread touches its navmesh. No locking needed.
- `DisableMgr` must have pathfinding enabled for pool IDs.
- Pool size N = maximum concurrent generated runs (64–128 is cheap).

**Kit rules (otherwise tiles won't link):**
- Doorways at fixed positions on cell borders, fixed width and floor height.
- Floor N at `N × floorHeight`; stairs pieces go between standard heights at their borders.
- A piece's geometry stays inside its own cell.
- `PathGenerator::HaveTile` only checks **layer 0**. Multi-level pieces are baked as **one tile** (Recast
  handles overlapping floors inside a tile). Separate floors go in separate x/y regions, joined by ramps or
  same-map teleports. Never stack separate tiles at one (x, y).

**Variety without re-baking:** decor that doesn't block movement is free; blocking decor comes as pre-baked
piece variants (`room_a`, `room_a_pillars`, `room_a_collapsed`). Optional later: Detour tile cache obstacles.

**Generation (layout grammar):** mission graph first (entrance → combat rooms → side branch with key →
miniboss → locked gate → boss), then embed into the grid. Bot rules: no jump-only connections, no narrow
ledges over drops, minimum room size per pack, corners for line-of-sight pulls, no vehicles or platforming.
The `KitLayoutProvider` outputs the same `Layout` struct as clones.

### 6.3 Fully client-generated maps (phase 6, optional)

Only if kits look too repetitive. A shared deterministic generator (integer/fixed-point math) compiled into
the module and into the client via Wowdecomp2. The cleanest client hook is a virtual file layer under the MPQ
loader that serves generated ADT/WMO data for `World\Maps\PG_<seed>\…`. To avoid per-map Recast, the
generator still places **prototype pieces with baked tiles**; generated visuals, but walkable space comes
from the kit. Costs: texturing, lighting, minimaps, determinism. Not needed for anything in phases 1–5.

---

## 7. Run lifecycle

1. **Open.** The player uses the map item (on-use spell) within range of a map device. The spell script
   validates: level 80, client patch handshake done for every real player in the group, group size within
   the map's cap, no active run for this group.
2. **Create run.** `RunService` creates a `Run {runId, seed, source/layout, tier, rarity, affixes,
   portalsLeft, groupGuid, leaderGuid}` and persists it to `atlas_run_active`. The item is consumed.
3. **Portals.** The device spawns portal GOs (despawn after a timeout or when portals run out).
4. **Enter.** Using a portal teleports the player (and the group's bots, see 16) to the layout entrance.
   Before the first teleport, remove any existing bind of the members to that clone ID so a fresh instance is
   created.
5. **Bind run to instance.** On the instance's first player enter, the instance script looks up the pending
   run by group/leader GUID and stores the `runId` in its save data. Reconnects and server restarts reload
   the run from `atlas_run_active`.
6. **Populate.** Deterministic from the seed: structural GOs, packs, rares, patrols, encounter modules, boss.
   Spawns are temporary (not saved to the world DB).
   - Spawns live as long as the instance (Q4). An instance left empty for `Instance.UnloadDelay` (default
     30 min), reset, or lost to a restart comes back **empty**.
   - So population is **idempotent**: the instance script stores a compact progress record (cleared anchors,
     killed rares, encounter states, boss state) in its normal save data, and on load rebuilds everything
     from the seed, skipping what is cleared. Packs that were half-fought come back at full health.
   - Don't change `Instance.UnloadDelay` for this (global setting on the play server).
7. **Play.** Deaths: without the module, AC picks the graveyard by **zone**, so a ghost from a Deadmines clone
   would land in Westfall and run to the *stock* Deadmines (Q2). The module overrides the graveyard with
   `OnPlayerBeforeChooseGraveyard` (hub graveyard) and can take over repop with `OnPlayerCanRepopAtGraveyard`.
   Re-entering uses a portal; a ghost arriving through a portal is resurrected at the entrance. Bots are players,
   so the same hooks apply to them; see section 16.
8. **Complete.** Boss death → completion: atlas update, reward chest at `exit`, map drops, a portal home.
9. **Teardown.** After everyone leaves or a timeout: delete the run row, clear binds, let the instance unload.
   For pool IDs: remove all navmesh tiles (`OnDestroyInstance`) and release the ID.

Failure states: out of portals, group disbanded, timeout (configurable, off by default). A failed run keeps
nothing but drops already looted.

---

## 8. Population

### Themes
A theme is a set of creature entries with a shared look (undead, Defias, Scarlet, Burning Legion, elementals,
Nerubians, Iron Dwarves…), built from `creature_template` by type, family, faction and model, then curated.
Each map has a native theme (its original inhabitants) plus allowed swap themes (an affix can swap it).

### Scourge corruption (adopted 2026-11-22)
A second axis on top of themes, giving the atlas its WotLK frame: the Lich King's plague has reached every
place in the atlas, and the player pushes it back.

- Every map has a **corruption level** that grows with tier: low tiers are mostly native inhabitants;
  higher tiers have packs replaced by Scourge, plague affixes and Scourge Rifts; top tiers are fully overrun.
  Pinnacle encounters are Lich King lieutenants.
- **Scourge-touched natives:** native entries with an undead visual aura and a Scourge affix, which is cheap
  variety between the two extremes.
- **Build order:** the Scourge pool comes **first** in phase 1. It is one curated pool, already level 80 with
  WotLK-tuned spells (Naxxramas, Utgarde, Ahn'kahet, Culling of Stratholme, ICC 5-mans, ICC trash), so the
  spell rescaling barely applies and curation is done once for every map. Native themes are added map by map
  as curation progresses.
- Native themes keep each map's identity; without them, 60+ maps would play as one.

### Creature pool and curation
The Scourge pool, rares and Scourge-touched natives are listed in `atlas-endgame.SCOURGE.md`.

- Spawning original entries keeps their SmartAI and spell rotations, so maps keep their character.
- C++ creature AIs created through an instance factory (`GetIcecrownCitadelAI<…>` and friends) don't attach
  on a clone: the factory returns null and the creature falls back to SmartAI or default AI (verified,
  section 24). Safe but plain. Other C++ scripts do run and may call into the module's InstanceScript, which
  is the real crash risk. Each entry carries an **allow status**: `allowed`, `blocked`, `untested`.
- **Shadow entries:** module-owned copies of stock `creature_template` rows (module creature range) with the
  module's generic AI and the stock spell list. Used for factory-scripted entries and anything that needs a
  rename or a fixed script. No client change needed; generated by a tool, frozen like layouts.
- The module's InstanceScript ignores unknown `SetData`/`GetData` IDs (SmartAI `SET_INST_DATA` reaches it).
- Automatic `blocked` rule: any entry with a model whose `creature_model_info` has `BoundingRadius <= 0` or
  `CombatReach <= 0` (spawns invisible but fights; lesson from mod-dungeon-master, ANALYSIS appendix).
- The generic AI refuses `EVADE_REASON_BOUNDARY` / `NO_PATH` evades while in combat (otherwise creatures run
  home and heal mid-fight).
- **Curation harness** (GM command, run on a test clone): spawns each untested entry, makes it fight a test
  dummy group, logs crashes / errors / stuck AI, and records the result. The allow list grows over time.
- Guid-based SmartAI (`entryorguid < 0`) never fires for temporary spawns; that is expected.

### Packs
- Each anchor has a **pack budget** in threat points. Tier, affix "+pack size" and atlas passives scale it.
- A pack = 1 leader + members chosen by role weights from the theme.
- **Rares:** a chance per pack (raised by affixes) to replace the leader with a rare: name prefix, 2–3 rare
  modifiers (auras from the affix handlers, section 10), better loot.
- **Patrols** use harvested or generated waypoint lists.
- **Caps (Q5):** a configurable maximum of live creatures per map and per anchor, applied after pack-size
  affixes and atlas passives, so stacked bonuses can't multiply client and server load without limit.
- **Visibility (Q5):** clones get the plain instance view distance, not the larger values AC hardcodes for
  some big raids. Bosses, rares and large structural objects get `SetVisibilityDistanceOverride` (Large or
  Gigantic) so they show from across big rooms. Don't raise `Visibility.Distance.Instances` globally on the
  play server for this.

### Structural gameobjects
Spawned from `Layout.structGOs` before packs. No doors: maps are open layouts.

---

## 9. Level-80 scaling

### Principle: normalize by role
Ignore the template's `HealthModifier` / `DamageModifier`. Every map creature gets a role and stats are
computed from the level-80 base:

```
HP        = base80Hp(unit_class)  × roleHp[role]  × tierMult(tier) × affixHpMult
MeleeDmg  = base80Dmg(unit_class) × roleDmg[role] × tierMult(tier) × affixDmgMult
Armor     = base80Armor(unit_class) × roleArmor[role]
```

`base80*` come from `creature_classlevelstats` at level 80, WotLK expansion column.

**Roles:** `trash`, `elite`, `rare`, `miniboss`, `boss`, `add`.

**Starting values** (tuning targets, to be adjusted in play):

| Role | roleHp | roleDmg | Notes |
|---|---|---|---|
| trash | 1.0 | 1.0 | Fodder, big pulls. |
| elite | 2.5 | 1.5 | Normal dungeon trash. |
| rare | 6 | 2.0 | Pack leaders with rare modifiers. |
| miniboss | 15 | 2.5 | |
| boss | 30 | 3.0 | ~WotLK heroic boss HP at tier 1. |
| add | 0.5 | 0.7 | Summons, spawned waves. |

`tierMult(t) = 1.075^(t-1)` (tier 16 ≈ 3×). All of these are config/SQL values, not constants.

The level is a run parameter whose default is 80, not a constant inside the formulas: the backlog's
levelling content runs the same engines at levels 15–79 (`atlas-endgame.BACKLOG.md`, rules R1–R2).

### Hooks (all exist today, section 24)
- `OnBeforeCreatureSelectLevel` → force level 80 for map creatures.
- `OnCreatureSelectLevel` → apply the role stats. It runs again on respawn and entry change, so stats can't
  revert.
- **Spell damage:** classic spells have fixed base points (a Defias Fireball still hits for ~100). At spawn,
  compute a per-creature `spellScale = expectedDmg80(role, tier) / expectedDmgOriginal(template)` and apply it
  in `ModifySpellDamageTaken` and `ModifyPeriodicDamageAurasTick` when the attacker (or its owner, for
  summons/totems/pets) is a map creature. Relative weight of abilities is kept.
- **Healing by creatures:** the same factor (by HP ratio) in `ModifyHealReceived`.
- **Melee:** set directly at spawn; the melee hook only applies affix multipliers.
- Every hook early-outs on "is this map a run map?" (a map ID set lookup) to keep the cost near zero elsewhere.

### What is dropped from the original creature
Original loot (cleared on death, replaced by run loot), XP, reputation on kill, gold (replaced by scaled gold).

### Party power (solo vs bots vs humans)
A run-level difficulty multiplier chosen at the device (e.g. "Relaxed / Standard / Hard") scales HP and
damage, with matching reward changes. Bot strength varies a lot; this is the release valve.

---

## 10. Affix system

The full list (91 affixes, phase-1 subset of 30) is in `atlas-endgame.AFFIXES.md`. Two rules from it:
- Numeric monster modifiers are pure hook math (`HOOK`), player modifiers are visible auras (`AURA`).
- Ground effects are built only in the three shapes playerbots dodge (dynamic-object aura, trap GO,
  non-selectable trigger NPC), each ≤ 15 yd radius and damage over time; one-shot blasts are weak or not
  bot-safe (section 24).

### Data model (`atlas_affix`)

| Column | Meaning |
|---|---|
| `id` | Affix ID. |
| `name_key` | Display string key (tooltip text comes from the enchant DBC row). |
| `handler` | Handler type (table below). |
| `spell_id`, `param1..param4` | Handler parameters. |
| `scope` | all / packs / rares / boss / players. |
| `tags` | Bitmask: monster, player, area, loot, boss, encounter. |
| `min_tier`, `max_tier`, `weight` | Where and how often it rolls. |
| `exclusive_group` | Affixes in the same group never roll together. |
| `bot_safe` | 1 if bots handle it with existing behaviour. |
| `quantity_bonus`, `rarity_bonus`, `packsize_bonus` | Reward contribution. |
| `roll_min`, `roll_max`, `roll_steps` | Value range; each step has its own enchant ID. |
| `enchant_id_base` | First `SpellItemEnchantment` ID for this affix's roll steps. |

### Handlers (about ten generic handlers cover hundreds of affixes)

| Handler | Examples |
|---|---|
| `AURA_MONSTER` (with custom base points) | extra fire damage, haste, damage shields, enrage at 30% HP |
| `AURA_PLAYER` | reduced healing, reduced armor, no regeneration in combat, reduced max resistances |
| `STAT_MULT` | HP, damage, attack speed, cast speed, movement speed, crit |
| `ON_DEATH` | explode, split into adds, leave a ground effect, buff nearby allies |
| `PERIODIC_AREA` | lightning strikes, monster consecrated ground, void zones |
| `PACK_COMPOSITION` | +pack size, extra rares, extra packs of a theme, theme swap |
| `BOSS_MOD` | extra library mechanics, enrage timer, adds |
| `ENCOUNTER` | adds an encounter module (section 12) |
| `LOOT` | quantity, rarity, extra fragments / currency |
| `ENVIRONMENT` | slow fall everywhere, darkness, burning ground at intervals |

Affixes use **existing spells** wherever possible (auras with custom base points via custom casts). Custom
spells come from the patched `Spell.dbc` only where no existing spell fits or a correct tooltip matters.

### Rarity

| Rarity | Affixes | Notes |
|---|---|---|
| normal | 0 | |
| magic | 1–2 | |
| rare | 3–6 | |
| corrupted | up to 8 + 1 corrupted implicit | Can't be modified further. |

### Storing affixes on the item
- An item has **12 enchant slots** (`MAX_ENCHANTMENT_SLOT`). The 5 random-property slots
  (`PROP_ENCHANTMENT_SLOT_0..4`) plus the temporary and bonus slots hold up to 8 affixes. `Item::SetEnchantment`
  is public, and enchant slots persist in `item_instance` natively: **no extra table for map items**.
- Tier, source map and rarity: the item entry encodes source + tier (one entry per source per tier band, or
  one entry per tier with the source in an enchant slot — decide in phase 1, see open question Q6).
- Tooltips: one generated `SpellItemEnchantment.dbc` row per affix per roll step, so tooltips show real
  numbers ("Monsters deal 25% extra Shadow damage"). 300 affixes × 3 steps ≈ 900 rows, generated by a script
  from `atlas_affix`.

### Bots and affixes
`bot_safe = 0` affixes (reflect, no-regeneration, …) are excluded when the atlas setting "bot-safe only" is
on (default for groups with bots), or roll with an extra reward bonus as an opt-in.

---

## 11. Map items and crafting currency

### Map items
- Bind on pickup. Stackable only when identical (normal rarity); affixed maps are unique items.
- On use: opens the map at a nearby device (section 7).
- Tiers 1–16. Source map pools per tier band (low tiers: short classic dungeons; high tiers: big wings).

### Currency (WoW-flavoured names; working list)

| Currency | Effect (PoE analog) |
|---|---|
| Shard of Empowerment | normal → magic (Transmutation) |
| Arcane Reagent | reroll magic affixes (Alteration) |
| Rune of Augmentation | add an affix to a magic map (Augmentation) |
| Titan Reforging Core | normal → rare (Alchemy) |
| Chaos Ember | reroll a rare map's affixes (Chaos) |
| Essence of Annulment | remove a random affix (Annulment) |
| Scouring Dust | back to normal (Scouring) |
| Saronite Corruptor | corrupt: unpredictable result, locks the map (Vaal) |
| Atlas Compass | add one encounter module to a map (Sextant/Scarab) |

Implementation: each currency is an item whose on-use spell **targets an item** (like enchanting scrolls).
A spell script validates the target is a map item and rewrites its enchant slots using a crafting RNG
(crafting happens outside runs, so it does not use a run seed). Native "click currency, click map" interaction.

### Fragments and pinnacle
Fragments drop at tiers 14–16. A full set opens a pinnacle encounter: a 5-player version of a raid boss
(stock boss if its script is curated, otherwise a library boss with that boss's model and theme).

---

## 12. Encounter modules (league-mechanic analogs)

| Module | Analog | Summary |
|---|---|---|
| Scourge Rift | Breach | A zone that grows while fighting; waves of Scourge; boss of the rift if fully opened. Reuse Scourge Invasion visuals. |
| Titan Cache | Strongbox | Opening the chest triggers waves; rarity of the cache adds modifiers. |
| Nerubian Burrow | Abyss | Cracks spread along the floor and spawn mobs; ends in a burrow chest. |
| Whispers of the Old God | Delirium | Fog that ramps difficulty and rewards over time; leave or push. |
| Argent Trial | Ritual | Arena of waves from previously killed monsters; reward pick. |
| Bound Elemental | Essence | A rare imprisoned in a crystal; freeing it is a harder rare fight with a currency reward. |

Each module is a self-contained class with: placement rules (needs an anchor of size ≥ X, or an arena),
spawn logic, completion condition, reward hook, and a `bot_safe` flag. Modules only use layout data, so they
work on clones and generated maps alike.

---

## 13. Bosses

Two sources, chosen by the arena's slot type and the run RNG:

1. **Curated stock bosses.** Original boss entries whose scripts don't depend on the original instance script
   (allow-listed via the curation harness). Bonus: playerbots may already have strategies for some of them
   (those are keyed by map in `Ai/Dungeon/*`, so verify whether they activate on a clone map ID; Q9).
2. **Library bosses.** A generic `BossAI` with 2–4 mechanics from the **mechanic library**, a stock model and
   name from the map's theme.

### Mechanic library
Each mechanic is data plus a small handler:

| Field | Meaning |
|---|---|
| `id`, `handler` | e.g. `GROUND_ZONE`, `CLEAVE`, `ADD_WAVE`, `INTERRUPT_CAST`, `SPREAD`, `STACK`, `SOFT_ENRAGE`, `PHASE` |
| `spell_ids`, timers | Existing Spell.dbc spells. |
| `telegraph` | Cast bar, emote, raid warning, ground visual. |
| `counter` | move / spread / stack / interrupt / kill add / soak |
| `bot_coverage` | `existing` (bots already handle it) / `hint` (needs the hint channel, section 16) / `none` |

**Rule:** the generator only picks mechanics with `bot_coverage = existing` while the group has bots, until
the hint channel exists.

---

## 14. Rewards and economy

### Item level ladder (existing WotLK loot)

| Tiers | Item level | Source loot |
|---|---|---|
| 1–5 | 200 | Heroic dungeon / Naxx-10 tables |
| 6–10 | 213–226 | Naxx-25 / Ulduar-10 tables |
| 11–15 | 232–245 | Ulduar-25 / ToC tables |
| 16 + pinnacle | 251–264 | ToC-25 HC / ICC tables |

Loot comes from module loot templates that reference existing loot tables (`reference_loot_template`), so
no new items are needed in phase 1. Generated affixed items are a later option (section 22, phase 4+):
runtime `item_template` rows in a reserved range, never reusing an entry with different stats (client WDB
item cache).

### Sustain
- Each map kill drops maps with base chance per pack and a guaranteed drop from the boss.
- Drop tier: same tier mostly, +1 with a chance, +2 rarely. Atlas passives move these numbers.
- First completion of a (source, tier) gives a bonus map.
- Target: a player who completes most maps sustains their tier and slowly climbs.

### Quantity and rarity
`quantity` and `rarity` percentages from affixes, atlas and difficulty multiply drop counts and the chance
for better item quality / currency. Clamp the totals (config) to control inflation.

### Bots and loot
Map items, currency and fragments are bind-on-pickup and only drop for real players (bots must not need-roll
them). Gear loot follows the group's normal loot rules.

---

## 15. Atlas progression

- **Account-wide** (`atlas_account_progress`), which suits a solo player with alts.
- Tracks completion per (source map, tier) and completion bonuses.
- **Atlas points** from completions feed a passive tree (`atlas_passive`, `atlas_account_passives`):
  more encounter modules, better sustain, more quantity, specialization in themes, etc.
- **UI:** a Lua addon (shipped inside the client patch) shows the atlas and passive tree. Communication uses
  addon messages (`CHAT_MSG_ADDON`) with a module prefix; the server intercepts them with
  `OnPlayerCanUseChat`. No binary client edits.
- GM/fallback: gossip menus on an atlas NPC for everything the addon does.

---

## 16. Playerbots integration

### What works today without changes
- **AoE avoidance:** `AutoAvoidAoe` exists, but is only enabled when the bots' master is a real game client
  (`AiFactory.cpp:291`). That fits "real player + bots". Mechanics with ground effects are the primary
  bot-safe tool, built only in the three shapes bots avoid, each ≤ 15 yd (section 24; AFFIXES appendix,
  section 2).
- **Raid target icons:** playerbots has an `rti` chat trigger/action. If marking a target makes bots focus it
  (verify, Q10), the module can set raid icons on priority targets for kill-priority mechanics.
- **Bot gear** scales with existing playerbots options.

### Registering bot strategies from another module (updated 2026-11-22)
- The shared strategy/action/trigger/value contexts are **private static** members of `AiObjectContext`
  (`AiObjectContext.h:89-92`), with no public registration API.
- But `mod-dungeon-clear`, already on this server, adds its own strategies without editing playerbots: it
  takes the lists' addresses with the explicit-instantiation access technique
  (`modules/mod-dungeon-clear/src/AiObjectContextAccess.h`) and appends to them after startup; every bot
  sees the additions because the lists are held by reference. If playerbots renames those members the build
  fails loudly, never silently at runtime.
- So an `atlas` bot strategy that reads the hint blackboard is possible without violating C2, at the price
  of a compile-time coupling to playerbots internals (re-check after each playerbots update, as for
  mod-dungeon-clear). Credit mod-dungeon-clear when mirroring the technique.

### Plan
1. **Phase 1–3:** only `bot_coverage = existing` mechanics and `bot_safe` affixes in bot groups.
2. **Hint blackboard:** the module publishes structured hints per instance anyway (spread 8 yd, leave this
   dynamic object, interrupt GUID, kill these first, stack here). With today's bots it drives raid icons and
   positioning of the mechanics themselves (e.g. place zones where bots already move away from).
3. **If a public extension point appears upstream** in mod-playerbots, add a generic `atlas` strategy that
   reads the blackboard. Until then, do not patch playerbots (C2). Re-check after each playerbots update.
4. **Bot movement into the run:** verify whether bots follow the master through the portal (Q11). If not, the
   module teleports the group's bots itself (`Player::TeleportTo`), which needs no playerbots change.
5. **Bot deaths:** verify how bots release/revive on a clone map with no graveyard (Q12). Fallback: the module
   revives dead bots at the entrance after combat, at a portal cost or free (config).

The hint blackboard is also groundwork for bot raid strategies later.

---

## 17. Client patch and compatibility

### Patch contents (MPQ, e.g. `patch-A.MPQ`; name TBD)

| File | Phase | Purpose |
|---|---|---|
| `Map.dbc` | 1 | Clone IDs (and pool IDs in phase 5). `InstanceType` = 1 (dungeon) for every clone, even of raids. |
| `MapDifficulty.dbc` | 1 | **Required** for every clone ID: the client dereferences a missing row for raid-type maps. |
| `Light.dbc` | 1 | Copies of the source map's rows with `ContinentID` = clone ID. Without them the clone uses Light 1 (Eastern Kingdoms default sky and fog). |
| `SoundEmitters.dbc` | optional | Copies of the source's ambient sound emitters. |
| `WorldMapArea`, `DungeonMap`, `DungeonMapChunk` | optional | World map inside the clone; without them the world map falls back to the continent. |
| `SpellItemEnchantment.dbc` | 1 | Affix tooltip lines. |
| `Spell.dbc` (+ `SpellIcon` if needed) | 1–2 | Map item use spell, currency spells, custom affix auras. |
| `LoadingScreens.dbc`, `AreaTable.dbc` | optional | Names, loading screens. |
| `GameObjectDisplayInfo.dbc` + kit models | 5 | Kit pieces. |
| `LFGDungeons.dbc` | optional | Dungeon Finder entry for the hub/maps. |
| FrameXML / addon files | 1 | Handshake and atlas UI. |

On the server, the matching rows go into the world DB's `map_dbc` / `mapdifficulty_dbc` tables as module SQL
(Q3). `Light` and the optional tables are client-only. DBC rows load at startup, so changes need a worldserver
restart.

### Handshake and gating
- Clone maps need the patched `Map.dbc` on the client: an unpatched client teleported to an unknown map ID
  fails. **So the whole module requires the patch.**
- The patch includes a FrameXML/addon file that sends `ATLAS_HELLO <version>` as an addon message after
  login. Ideally this file lives in the MPQ itself, so the handshake proves the MPQ is installed, not just an
  addon (verify, Q13).
- The server marks the session as patched (version-checked). Unpatched sessions: the device refuses with a
  message; map items and currency never drop for them; they never see custom IDs.
- Last line of defence: `OnPlayerCanEnterMap` refuses any clone or pool map ID for an unpatched session, so
  no code path (GM teleport, summon, group follow) can send an unpatched client to a map it doesn't know.
- Unpatched players can still play everything else on the server (C5).

### WDB cache rules
Never change what an existing ID means: item entries, spells, enchants and map IDs keep their meaning
forever. New meaning → new ID. Players' WDB caches otherwise show stale data.

---

## 18. Staying compatible with AC updates

- **CoreAdapter:** one file for every touch of AC internals (`MapCollisionData`, `MMapMgr` formats,
  `MMAP_VERSION`, Detour structs, `dtNavMeshParams`).
- **Startup self-check:** compare `MMAP_VERSION`, `DT_NAVMESH_VERSION`, `sizeof(dtNavMeshParams)` and the hook
  signatures the module needs with the values the kit was baked against. On mismatch: log clearly and disable
  the affected feature (kit maps) or the module. Never crash.
- **Frozen data:** harvested layouts are committed module SQL, not derived from the live world DB.
- **Kit bake tool** is built in-tree against `deps/recastnavigation`, so re-baking is one command.

### Installing, disabling, uninstalling (the play server)

- **Being in `modules/` is installing.** The play server's images are built from this checkout, and AC's DB
  updater applies every compiled-in module's `data/sql/db-*` files at startup (section 24). So mod-atlas is
  developed in a separate checkout (or listed in `DISABLED_AC_MODULES` for the play build) until the day it
  is meant to go live, and `Atlas.Enable` defaults to **off** in code, not only in the `.conf.dist`.
- All module data uses reserved ID ranges (section 19), so removing it is `DELETE … WHERE id BETWEEN …`.
- **Disable** = turn `Atlas.Enable` off but keep the module and its rows: the device stops working, and on
  login anyone saved inside a clone is moved to the hub.
- **Uninstall order:** (1) disable and let every character log in once or move them out, (2) delete instance
  binds and `instance_reset` rows for the reserved map IDs, (3) delete the `map_dbc` / `mapdifficulty_dbc`
  rows **together with** the `instance_template` rows, (4) restart. A map row without its template can abort
  the server (Q3); a character saved on a map ID that no longer exists is sent to their homebind by AC.
- Never try any of this on the play server first: phase 0 and every schema change are tested on a separate
  stack with its own database.

### Checklist after merging AC
1. Build; read the self-check log line on startup.
2. If `MMAP_VERSION` / Recast changed: re-extract mmaps (as for stock maps), re-bake the kit.
3. Clone symlinks: still valid if file names are unchanged; re-run the symlink tool after re-extracting.
4. Run the curation harness smoke set (a few entries per theme).
5. If hooks used by the module changed signature, fix the module; the build tells you.

---

## 19. Data model

### World DB (module SQL)

| Table | Content |
|---|---|
| `atlas_map_source` | clone map ID, source map ID, name, tier band, default theme, size class, enabled. |
| `atlas_layout` | layout ID, map ID, wing name, entrance, exit, metadata. |
| `atlas_layout_anchor` | layout ID, anchor ID, x/y/z/o, radius, pack budget, zone tag. |
| `atlas_layout_arena` | layout ID, arena ID, centre, radius, slot type. |
| `atlas_layout_patrol` | layout ID, patrol ID, point index, x/y/z. |
| `atlas_layout_struct_go` | layout ID, GO entry, position, rotation. |
| `atlas_theme`, `atlas_theme_creature` | themes and their creature entries with role weights. |
| `atlas_creature_curation` | entry, allow status, last result, notes. |
| `atlas_shadow_entry` | shadow entry → stock entry it copies, generator version (SCOURGE appendix). |
| `atlas_zone_strip` | source map → spell IDs to strip inside its clones (CLONES appendix, section 4). |
| `atlas_affix` | section 10. |
| `atlas_mechanic` | section 13. |
| `atlas_encounter` | encounter modules and parameters. |
| `atlas_boss_pool` | stock and library bosses per theme. |
| `atlas_loot_tier` | tier → loot template mapping. |
| `atlas_passive` | passive tree nodes. |

### Characters DB

| Table | Content |
|---|---|
| `atlas_account_progress` | account, source, completed tiers bitmask, bonus flags. |
| `atlas_account_passives` | account, passive node, rank. |
| `atlas_account_points` | account, points earned / spent. |
| `atlas_run_active` | run ID, instance ID, map ID, seed, affixes, tier, portals left, group/leader GUID, timestamps. |
| `atlas_run_log` | finished runs (debugging, stats, leaderboards). |

Map item affixes live in `item_instance` enchantments (no table).

### Footprint outside the module's own tables

The module's own data lives in `atlas_*` tables. Some things can only exist as rows in AC's own tables; they
always use the reserved ID ranges below, and install / uninstall SQL is generated from this list. Anything not
on this list is a bug.

**World DB, written by the module's install SQL (reserved IDs only):**

| Table | Rows |
|---|---|
| `map_dbc`, `mapdifficulty_dbc` | Clone and pool map IDs (Q3). |
| `instance_template` | One per clone / pool map ID (never removed separately from the map rows). |
| `spellitemenchantment_dbc` | Affix enchant IDs, so the server knows the enchants it stores on map items. |
| `spell_dbc` | Custom spells (map item use, currency, affix auras) when an existing spell doesn't fit. |
| `item_template` (+ `item_dbc` if needed) | Map items, currency, fragments. |
| `creature_template`, `creature_template_model` | Library bosses, encounter dummies. |
| `gameobject_template` | Map device, portals, caches; kit pieces in phase 5. |
| `creature_loot_template`, `gameobject_loot_template`, `reference_loot_template` | Module loot IDs. |
| `spell_script_names` | Bindings for the module's spell scripts. |
| `command`, `module_string` (+ `_locale`) | GM commands, texts. |

**Characters DB, written by AC itself when the module is used** (not by module SQL):

| Table | Why | Cleanup |
|---|---|---|
| `item_instance`, `character_inventory` | Map items, currency in bags. | Delete by item entry range on uninstall. |
| `character_instance`, `instance` | Binds to clone instances. | Module removes binds at run end; uninstall deletes by map ID range. |
| `characters` (position) | Logging out inside a run. | Module moves players to the hub on login; AC falls back to homebind. |
| `character_aura` | A player logging out with an affix aura on. | Module strips affix auras on leaving a run and on logout. |
| `corpse` | Dying inside a clone. | Expires normally; uninstall deletes by map ID range. |
| `character_queststatus`, `character_achievement*` | Quest/achievement credit from original creatures (Q2). | Avoid by design where it matters; not reverted. |

**Auth DB:** nothing (the handshake is per session, in memory).

**Files:** symlinks for clone IDs in the client-data volume (`maps/`, `vmaps/`, `mmaps/`); params-only `.mmap`
files for pool IDs (phase 5); `mod_atlas.conf`.

### Reserved ID ranges

Checked 2026-11-22 against `data/sql/base/db_world`, all world SQL under `data/sql/updates/`, every module's
SQL under `modules/*/`, and `my_custom_dbcs/` (holds only CharBaseInfo, CharStartOutfit, SkillRaceClassInfo).
**Not yet checked against the live play DB**: before install, a few read-only `SELECT COUNT(*) … WHERE id
BETWEEN …` queries, run only with the user's OK. Ranges are kept low where AC sizes a lookup vector by the
highest ID (`_itemTemplateStoreFast`, `_creatureTemplateStoreFast` in `ObjectMgr.h:1661,1685`; DBC index
tables on client and server).

| Kind | Range | Split | Stock / custom data nearby |
|---|---|---|---|
| Map IDs | 900–999 | 900–949 clones, 950–999 generated-map pool | Highest stock instance map 724. |
| Item entries | 90000–90999 | 90000–90499 map items, 90500–90599 currency, 90600–90699 fragments, 90700–90999 backlog items/currencies (BACKLOG R5) | Highest stock item 56806. |
| Spell IDs | 90000–90999 | 90000–90499 affix auras, currency, device (90400–90449: run fogs, aura 260, STAGE2 fogs); 90500–90999 boon library (BACKLOG R5). 91000–91099 belong to V0 (91001 = Timeways Resonance, the Endless Timeways buff). | Stock spells end below 80000 in `spell_dbc` rows here; base `spell_dbc` also has custom rows at 100001–100102, avoided. Verify the client `Spell.dbc` maximum. |
| ScreenEffect IDs | 900–949 | Run fogs (client patch only; stock rows end at 760) | Checked against the client's ScreenEffect.dbc. |
| Enchant IDs | 9000–9999 | Affix roll steps (~300 with the current list) | No stock overrides in `spellitemenchantment_dbc`; verify the client DBC maximum. |
| Creature entries | 900000–909999 | 900000–901999 shadow entries, 902000–902999 library bosses and dummies, 905000–909999 name bank (BACKLOG R5) | Stock rows stop at 182772, then two odd rows at 3460602–3460603 (already set the fast-lookup vector size, so no extra cost). |
| GO entries | 900000–900999 | Device, portals, caches; 900100–900199 kit pieces (type 14, spike 2026-11-24) | Highest stock GO 401007. |
| Loot template IDs | 900000–909999 | Creature, GO, item and reference loot of the module | Checked free in all four loot tables. |

---

## 20. Configuration (`mod_atlas.conf.dist`, keys indicative)

`Atlas.Enable`, `Atlas.RequireClientPatch`, `Atlas.ClientPatchVersion`, `Atlas.MapIdRange`,
`Atlas.PoolSize`, `Atlas.Portals`, `Atlas.RunTimeout`, `Atlas.BotSafeOnlyWithBots`,
`Atlas.Scaling.RoleHp.*`, `Atlas.Scaling.RoleDmg.*`, `Atlas.Scaling.TierGrowth`, `Atlas.Difficulty.*`,
`Atlas.Loot.QuantityCap`, `Atlas.Loot.RarityCap`, `Atlas.Sustain.*`, `Atlas.Bots.AutoRevive`,
`Atlas.Bots.TeleportWithMaster`, `Atlas.Debug`.

## 21. GM commands (indicative)

`.atlas map create <source> <tier> <rarity> [seed]`, `.atlas map affix add|remove <id>`,
`.atlas run info|fail|complete`, `.atlas run reseed`, `.atlas curate <entry|theme>`,
`.atlas layout show` (spawns markers on anchors/arenas/patrols), `.atlas reload`, `.atlas selfcheck`.

---

## 22. Roadmap

| Phase | Content | Done when |
|---|---|---|
| V0 (exists) | mod-atlas V0: echoes in stock instances (Keepers of Time), Endless Timeways, level scaling, loot, leaderboards, bot groups. IDs: creature 500000–500001, guids 8000001–8000011, npc_text 500000, spell 91001, tables `atlas_*` (characters). | Compiles, then plays on the test stack. |
| 0. Groundwork | Verify open questions Q1–Q5 on a test server; client patch skeleton with one clone; symlink tool; handshake. Optional shortcut: develop population, scaling, loot and affix code in a *stock* instance on the test stack ("prototype mode", which is what V0 is) while the client patch is built; never on the play server. | A patched client can teleport into an empty clone of Deadmines and walk it; an unpatched client is refused cleanly. |
| 1. Core loop | 5 clones (starter set in the CLONES appendix), harvest + frozen layouts, the curated Scourge pool (used on every map) plus native themes for those clones, corruption level by tier, level-80 scaling, map items, device, portals, ~30 affixes, stock loot ladder, run lifecycle. | A player + bots can open a map, clear it, kill the boss and get loot and new maps. |
| 2. Economy | Currency and crafting, sustain tuning, tiers 1–16, atlas progress, atlas addon UI. | Sustain at a tier is achievable; crafting works through the native interaction. |
| 3. Content | Encounter modules, mechanic library, library bosses, more clones and wings, 150+ affixes, rares. | Each source map plays differently across runs. |
| 4. Endgame | Atlas passives, fragments and pinnacle encounters, optional generated items. | A long-term goal exists past tier 16. |
| 5. Generated maps | Kit pieces (one theme first), bake tool, pool IDs, navmesh assembly, layout grammar, KitLayoutProvider. | A generated layout plays with bots like a clone does. |
| 6. Optional | Client-generated geometry (Wowdecomp2). | Only if kits look too repetitive. |
| Later version | World integration: World Memory, Director, nemeses, Scourge outbreaks and zone corruption linked to maps, Delves and contracts while levelling, Fated (roguelite) characters, gauntlet maps. Backlog in `atlas-endgame.BACKLOG.md`. | Not scheduled. Phases 1–4 only respect its rules R1–R6 (cheap, keeps the door open). |

---

## 23. Risks and open questions

| # | Question | Risk if wrong / fallback |
|---|---|---|
| Q1 | **Answered 2026-11-22 (static, from Wowdecomp2): yes.** The client loads a map only by its `Directory` string and always purges and reloads; the map ID only feeds tables keyed by map ID. Clone rows needed: `MapDifficulty` (required), `Light` (recommended). Details in section 24. | Still to confirm in game once a test server exists (not the play server). |
| Q2 | **Answered 2026-11-22 (static): no hard breakage.** The server never compares an area's map with the current map outside a GM command. Clones report the source's zone/area IDs, which leaks zone-keyed behaviour: graveyard choice (handled by hooks, section 7), `spell_area` auras (e.g. ICC's Chill of the Throne), area flags, zone-based quest/achievement credit. Audit per source map. Details in section 24. The `spell_area` audit for every instance zone is done (CLONES appendix, section 4). | Remaining per-source audit (area flags, quest/achievement credit) in phase 1. |
| Q3 | **Answered 2026-11-22 (static).** Each clone needs a `MapDifficulty` row (normal, max players, reset time 0) or entry is refused; and an `instance_template` row, or the server can abort. Server rows go in `map_dbc` / `mapdifficulty_dbc`. Details in section 24. | Confirm on a test stack. |
| Q4 | **Answered 2026-11-22 (static).** Grids are never unloaded one by one in this AC build; they unload only with the whole map. Spawns survive the whole run as long as the instance lives. The instance itself unloads after being empty for `Instance.UnloadDelay` (default 30 min), on reset, or on restart, and then every temporary spawn is gone. So run progress is persisted and population is rebuilt from the seed (section 7). Bots inside count as players and keep the instance alive. | none |
| Q5 | **Partly answered 2026-11-22 (static).** No hard object caps on the client: its object and model heaps grow by blocks. Phase 1 spawns come from the original spawn clusters, so load stays close to the stock dungeon unless pack-size affixes multiply it, hence a live-creature cap per map. Clones use the plain instance view distance (default 170 yd), not the 200–300 yd AC hardcodes for Ulduar, ICC, AQ40, Eye of Eternity…, so big open rooms show monsters later; bosses and large objects get a per-object visibility override. Frame rate itself, and gameobject counts for kit maps, need a test. | Test on a separate stack: busiest wing at max pack size with a full bot group. |
| Q6 | Map item encoding: entry per source+tier band, or source stored in an enchant slot? | Decide in phase 1. |
| Q7 | Loot fill order on creature death vs `OnUnitDeath` (can the module clear and refill loot reliably)? **Field evidence 2026-11-22:** mod-dungeon-master found loot set in `JustDied` is wiped by the core, while filling it from `OnUnitDeath` works (ANALYSIS appendix). Still to confirm on the test stack. | Use lootid 0 templates via a custom creature data override instead. |
| Q8 | Do item-targeting on-use spells work for currency with a spell script on a custom Spell.dbc entry? | Fallback: gossip-based crafting at an NPC. |
| Q9 | Do playerbots' per-dungeon strategies activate on clone map IDs for stock bosses? | Likely not (keyed by map); stock bosses then rely on generic bot behaviour. |
| Q10 | Does marking a target with a raid icon make bots focus it? | Without it, kill-priority mechanics are `bot_coverage = none`. |
| Q11 | Do bots follow the master through the portal? | Module teleports bots itself. |
| Q12 | Bot death/release on a map with no graveyard. | Module revives bots at the entrance. |
| Q13 | Can FrameXML/addon code inside a patch MPQ send the handshake (proving the MPQ is installed)? | Addon-only handshake; accept that a missing MPQ shows up as a failed teleport. |
| Q14 | Scaling numbers: are role multipliers fun with bots? | All values are config; tune in phase 1. |
| Q15 | Can a damage hook (`ModifyMeleeDamage`) safely deal a second hit of another school (affixes M02–M05) without re-entering itself? | Fallback: a proc aura with a custom triggered spell. |
| Q16 | Which stock spell gives a "Scourge-touched" visual with no gameplay effect? | Fallback: a module spell with only a visual. |
| Q17 | Is there a hook that sees absorb amounts (affix P13)? | Drop P13. |

---

## 24. Verified facts this design relies on

Checked on 2026-11-22 against the current tree. Re-check after AC / playerbots updates.

| Fact | Location |
|---|---|
| Instances share the base map's navmesh and static vmap tree via `parentMap`. | `src/server/game/Maps/MapCollisionData.cpp:30-49` |
| `GetMMapNavMeshSharedPtr()` is public and returns `shared_ptr<dtNavMesh>` (non-const navmesh). | `src/server/game/Maps/MapCollisionData.h:90` |
| `LoadMMapTile` loads tiles from files by map ID and grid coords; ignored if pathfinding disabled or no navmesh. | `src/server/game/Maps/MapCollisionData.cpp:63-69` |
| `MMapMgr::LoadNavMesh` reads only `dtNavMeshParams` from `{map}.mmap`; a missing `.mmtile` only logs at debug level. | `src/common/Collision/Management/MMapMgr.cpp` (loadMapData / LoadTile) |
| `MMapMgr::LoadTile` has no locking. | same file |
| `MmapTileHeader` contains no map ID (magic, versions, size, liquids, recast config). | `src/common/Collision/Maps/MapDefines.h:64` |
| vmap tile files are named `{map:03}_{y:02}_{x:02}.vmtile`; the map ID is only in the name. | `src/common/Collision/Maps/MapTree.cpp:77` |
| `.map` files are named `{map:03}{x:02}{y:02}.map`. | `src/server/game/Grids/GridTerrainLoader.cpp:31` |
| Stock mmaps use tile width `GRID_SIZE`. | `src/tools/mmaps_generator/MapBuilder.cpp:577` |
| `PathGenerator::HaveTile` checks `getTileAt(tx, ty, 0)` (layer 0 only). | `src/server/game/Movement/MovementGenerators/PathGenerator.cpp:839-852` |
| Floor height can come from gameobjects (`GetGameObjectFloor`). | `src/server/game/Maps/Map.h:408` |
| Recast/Detour is vendored. | `deps/recastnavigation` |
| Hooks: `OnBeforeCreatureSelectLevel`, `OnCreatureSelectLevel`. | `src/server/game/Scripting/ScriptDefines/AllCreatureScript.h:33,36` |
| Hooks: `OnHeal`, `OnDamage`, `ModifyPeriodicDamageAurasTick`, `ModifyMeleeDamage`, `ModifySpellDamageTaken`, `ModifyHealReceived`, `OnUnitDeath`. | `src/server/game/Scripting/ScriptDefines/UnitScript.h:61-111` |
| Hooks: `OnBeforeCreateInstanceScript`, `OnDestroyInstance`, `OnCreateMap`. | `src/server/game/Scripting/ScriptDefines/AllMapScript.h:68,76,83` |
| Hooks: `OnPlayerReleasedGhost`, `OnPlayerCanUseChat` (several overloads). | `src/server/game/Scripting/ScriptDefines/PlayerScript.h:255,693` |
| Items have 12 enchant slots; `PROP_ENCHANTMENT_SLOT_0..4` are slots 7–11. | `src/server/game/Entities/Item/Item.h:183` |
| `Item::SetEnchantment` is public. | `src/server/game/Entities/Item/Item.h:300` |
| playerbots `AutoAvoidAoe` is applied only when the master is a real game client. | `modules/mod-playerbots/src/Bot/Factory/AiFactory.cpp:291` |
| playerbots shared strategy/action/trigger/value contexts are private static. | `modules/mod-playerbots/src/Bot/Engine/AiObjectContext.h:89-92` |
| playerbots has an `rti` chat trigger and action. | `modules/mod-playerbots/src/Ai/Base/ChatTriggerContext.h:100`, `ChatActionContext.h:169` |
| playerbots dungeon strategies live per dungeon in `src/Ai/Dungeon/*`. | `modules/mod-playerbots/src/Ai/Dungeon/` |
| Bots avoid only three shapes: a negative dynamic-object aura on the bot (periodic damage, periodic damage %, periodic trigger or dummy), a trap GO (`trap.type` 0, negative spell), or a non-selectable trigger NPC with a periodic-trigger aura whose spell does school damage. All limited to `Playerbots.MaxAoeAvoidRadius` (default 15 yd), minus a spell whitelist. | `modules/mod-playerbots/src/Ai/Base/Actions/MovementActions.cpp:1872-2090`, `Ai/Base/Value/AoeValues.cpp:121-146`, `PlayerbotAIConfig.cpp:170-174` |
| Instance-factory creature AIs (`GetInstanceAI` → `InstanceHasScript`) return null when the instance's script name differs; `SelectAI` then falls back to `AIName` or the default AI. On a clone such trash fights as plain melee, no crash. | `src/server/game/AI/CreatureAIImpl.h:63-69`, `src/server/game/AI/CreatureAISelector.cpp:78-88` |
| ICC, Pit of Saron and Scholomance trash scripts in the Scourge pool all use the instance factory (e.g. `GetIcecrownCitadelAI<…>`). | `src/server/scripts/Northrend/IcecrownCitadel/icecrown_citadel.h:640`, the scripts' `GetAI` |
| `spell_area` auras are re-applied on every zone and area change (`UpdateZone`, `UpdateAreaDependentAuras`), so stripping a leaked zone aura once is not enough. | `src/server/game/Entities/Player/PlayerUpdates.cpp:1881,1890-1931` |
| Item and creature templates have lookup vectors sized by the highest entry. | `src/server/game/Globals/ObjectMgr.h:1661,1685` |
| All four Drak'Tharon Keep bosses, Mr. Smite and Baroness Anastari use the instance factory; the other Deadmines, Shadowfang and Stratholme bosses are SmartAI. | `Northrend/DraktharonKeep/boss_*.cpp` (`GetDraktharonKeepAI`), `boss_mr_smite.cpp:50`, `boss_baroness_anastari.cpp:142`; `smart_scripts` |
| The DB updater applies `modules/<mod>/data/sql/db-*` for every module in the compiled module list, at startup. | `src/server/database/Updater/UpdateFetcher.cpp:157-187`, `src/server/apps/worldserver/Main.cpp:448` |
| This fork runs dynamic spawn mode: despawning a DB-spawned creature writes its respawn time to the characters DB. | `src/server/game/Entities/Creature/Creature.cpp:458-473` |
| `Unit::SetAI` only swaps the pointer (the old AI is not deleted). | `src/server/game/Entities/Unit/Unit.h:700` |
| Halls of Lightning Titanium statues spawn frozen and wake on SmartAI `ACTION_DONE` 0, so `AI()->DoAction(0)` wakes them. | `smart_scripts` rows for 28961, 28965 |

### Server facts for clones (Q2, Q3; checked 2026-11-22)

| Fact | Location |
|---|---|
| World DB tables `map_dbc`, `mapdifficulty_dbc`, `light_dbc`, `areatable_dbc` are loaded after the DBC files; their rows are added or override by ID. | `src/server/game/DataStores/DBCStores.cpp:276-347`, `src/server/shared/DataStores/DBCDatabaseLoader.cpp:40-138` |
| The MapDifficulty lookup map is built from the store after DB rows are merged. | `src/server/game/DataStores/DBCStores.cpp:429-430` |
| Entering a dungeon without an `instance_template` row is refused (`CANNOT_ENTER_UNINSTANCED_DUNGEON`); without a MapDifficulty row for the (downscaled) difficulty it is refused with a transfer abort. Both checks run before the GM bypass. | `src/server/game/Maps/MapMgr.cpp:140-157` |
| Heroic requests downscale to normal when only a normal row exists. | `src/server/game/DataStores/DBCStores.cpp:784-806` |
| `MapInstanced::CreateInstance` calls `ABORT()` if the map has no `instance_template`. | `src/server/game/Maps/MapInstanced.cpp:192-196` |
| Instance binds are looked up through the downscaled MapDifficulty row; with none, no bind is found. | `src/server/game/Instances/InstanceSaveMgr.cpp:914-920` |
| `instance_reset` rows whose map/difficulty has no MapDifficulty row are deleted at startup. | `src/server/game/Instances/InstanceSaveMgr.cpp:310-327` |
| `AreaTableEntry::mapid` is used only by `.go zonexy`; no gameplay code compares an area's map with the player's map. | `src/server/scripts/Commands/cs_go.cpp:392-420` |
| Graveyards are chosen by area, then zone (`graveyard_zone`); `OnPlayerBeforeChooseGraveyard` can override the result. | `src/server/game/Misc/GameGraveyard.cpp:97-171` |
| `OnPlayerCanRepopAtGraveyard` runs before repop and can cancel it. | `src/server/game/Entities/Player/Player.cpp:5030-5038` |
| Corpse queries for dungeons use the Map row's entrance fields. | `src/server/game/Handlers/QueryHandler.cpp:249-254` |
| At login: invalid map/position → homebind; a dungeon with no valid instance and no entrance area trigger → homebind. | `src/server/game/Entities/Player/PlayerStorage.cpp:5243-5248`, `5377-5384` |
| Hooks for gating and death: `OnPlayerCanEnterMap`, `OnPlayerBeforeChooseGraveyard`, `OnPlayerCanRepopAtGraveyard`. | `src/server/game/Scripting/ScriptDefines/PlayerScript.h:558,660,681` |
| Example of zone-keyed leakage: ICC zone 4812 has `spell_area` rows (69127 autocast, 72293). | `data/sql/base/db_world/spell_area.sql` |
| `Map::UnloadGrid` is only called from `Map::UnloadAll`: grids unload only with the whole map (Q4). | `src/server/game/Maps/Map.cpp:1048-1086` |
| An instance's unload timer starts when the last player leaves (`Instance.UnloadDelay`, default 1800000 ms), stops when a player enters, and the map does not unload while it has pending events. | `src/server/game/Maps/Map.cpp:2142,2177-2182`, `Map.h:178-186`, `src/server/game/World/WorldConfig.cpp:276` |
| A new instance starts with the unload timer running, so an instance nobody enters unloads on its own. | `src/server/game/Maps/Map.cpp:1953-1956` |
| Instance resets set a short unload timer and mark the map to reset after unload. | `src/server/game/Maps/Map.cpp:2240-2268` |
| Instance view distance is `Visibility.Distance.Instances` (default 170), with hardcoded larger values by map ID (Dire Maul, The Eye, Oculus 175; ToC, CoS, PoS 150; OS, EoE, Ulduar, HoR, ICC, RS 200; AQ40 300). Clones match none of these IDs. No public setter for a map's view distance. | `src/server/game/Maps/Map.cpp:1972-2002`, `Map.h:200,614`, `worldserver.conf.dist:1303` |
| Per-object visibility override exists (`Normal`, `Tiny`, `Small`, `Large`, `Gigantic`, `Infinite`); hard maximum 250 yd. | `src/server/game/Entities/Object/Object.h:691`, `ObjectDefines.h:30,61-71` |

### Client facts (Wowdecomp2, `~/azerothcore-playerbots/wowdecomp2`, checked 2026-11-22)

The decompilation keeps the original 12340 behaviour in shared code, so these apply to the stock client too.

| Fact | Location |
|---|---|
| `SMSG_NEW_WORLD` looks up the Map record by ID and keeps only its `Directory` for loading. | `WoW/Source/Client.cpp:944-967` |
| `CMap::Load` builds `World\Maps\<dir>\<dir>.wdt` from the directory, purges loaded maps and WMO caches, then loads. Nothing is cached by directory, so two IDs sharing one directory load the same assets. | `WoW/Source/WorldClient/Map.cpp:3005-3046` |
| Lighting is picked from `Light.dbc` rows whose `ContinentID` equals the map ID; with none, record 1 (Eastern Kingdoms default) is used. | `WoW/Source/WorldClient/DayNight.cpp:369-401` |
| Far clip: map IDs < 530 (except 543, 575) get the lower far-clip cap. Clone IDs ≥ 530 get the higher cap (harmless). | `WoW/Source/WorldClient/World.cpp:1319-1339` |
| Raid-difficulty-change message dereferences the `MapDifficulty` row without a null check when the map's instance type is 2 (raid). Other uses null-check. | `WoW/Source/Ui/GameUI.cpp:7074-7079` |
| `GetInstanceInfo` returns an empty name and 0 max players when no `MapDifficulty` row exists. | `WoW/Source/Ui/GameUI_Scripts.cpp:2407-2419` |
| Party members out of visibility range are drawn on the minimap only if their zone's `AreaTable.ContinentID` matches the current map. In a clone the zone still belongs to the source map, so those dots are hidden (cosmetic). | `WoW/Source/Ui/MinimapFrame.cpp:2411-2420` |
| Area triggers are keyed by map ID in `AreaTrigger.dbc`; a clone has none, so the client never sends area triggers inside a clone (stock trigger scripts can't fire). | `WoW/Source/Object/ObjectClient/Player_C.cpp:10862` |
| The world map null-checks missing `WorldMapArea` rows and falls back to the map's corpse location / continent. | `WoW/Source/Ui/WorldMap.cpp:712-800` |
| The minimap finds its tiles by directory name, so clones get the source's minimap. | `WoW/Source/Ui/MinimapFrame.cpp:1222-1227` |
| Client object, M2 model and map-object heaps are block allocators that add blocks on demand; the "count" argument is the block size, not a cap. | `WoW/Source/ObjectAlloc/ObjectAlloc.cpp:300-390`, `WoW/Source/ObjectMgrClient/ObjectMgrClient.cpp:55,849`, `Engine/Source/Model2/M2Cache.cpp:385` |
| Gameobjects can carry WMO models and act as world objects with collision bounds (as transports and destructible buildings do); relevant to kit maps. | `WoW/Source/Object/ObjectClient/GameObject_C.cpp:762-768,2197,3570-3603` |

### Echo atmosphere: fog, light, screen effects (checked 2026-11-22, user request)

Goal: an echo must look unlike the stock dungeon, and the look should come from the modifiers (theme, affixes). All of it is server-side, with no core edits and no client patch.

| Fact | Source |
|---|---|
| `Map::SetZoneOverrideLight(zoneId, lightId, fade)` stores the override in that Map's `_zoneDynamicInfo`, so per instance, and `SendZoneDynamicInfo` replays it (with music and weather) to anyone who enters later. Stock users: Malygos, the Lich King. | `src/server/game/Maps/Map.cpp:3245-3264, 3351-3362` |
| The client handles `SMSG_OVERRIDE_LIGHT` by copying the override Light.dbc row's params (sky, fog, colours) onto the map's light **at that light's position and falloff**, so a light from any map can recolour any dungeon. | `WoW/Source/WorldClient/DayNight.cpp:3219-3253` (`AddLightOverride`) |
| `SPELL_AURA_SCREEN_EFFECT` (260) on the active player sets a ScreenEffect.dbc entry: types include netherworld fog (`SetOverrideFog`), swirling-fog shader and glow, plus a LightParams override and ambience/music. The client reads the aura from **its own** Spell.dbc, so it must be an existing client spell (a server-only `spell_dbc` row does nothing), and it must be a sent (non-passive) aura. | `WoW/Source/Ui/WorldFrame.cpp:1294-1403` |
| Weather (`SetZoneWeather`) is hidden inside interior WMOs, so it's of little use in most dungeons; zone music (`SetZoneMusic`) is per instance too. | `Map.cpp:3316-3349` |

Open, to test in game: how strongly an area light override shows in interior WMO areas (`m_interiorFogBlend` mixes in the WMO's own fog), per dungeon.

**Catalogue and corrected mechanism: `atlas-endgame.ATMOSPHERE.md`.** The stock `SetZoneOverrideLight` reaches only 2 of the 44 dungeons; the module sends its own `SMSG_OVERRIDE_LIGHT` with an anchor light covering any map. V1 is light overrides; screen effects wait for the client patch (every safe carrier shows a buff icon).

Plan: each theme and affix carries a "visual signature" `{lightId, screenEffectSpell, creatureVisualAura, musicId}`; the echo applies the theme's at populate time and stacks the affix ones. Next step: catalogue candidate Light.dbc rows (Naxxramas, Icecrown, Shadowmoon, Scholomance...) and existing hidden aura-260 spells from the client-data volume (read-only).
