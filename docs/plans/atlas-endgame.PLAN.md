# Atlas: implementation plan (from V0 to the design)

Written 2026-11-22. Builds `atlas-endgame.DESIGN.md` on top of mod-atlas V0 (`modules/mod-atlas`, commit `652c01b`). The design is the reference for *what*; this file is *in what order, in which files, and how we know each step is done*. Update both in the same change when they disagree.

## 0. Strategy

**Atlas on echoes first, clones second.** The design's roadmap starts with the client patch and clone maps (phase 0). But nearly everything in its phase 1 is server-side: map items, the device, seeded runs, affixes, role scaling, Scourge corruption, loot tiers and atmosphere. V0's stock-instance runner can carry all of it today. So:

| Stage | What | Client patch | Where it's tested |
|---|---|---|---|
| **1. Atlas on echoes** | The whole core loop, run in stock instances through V0's runner | none (the AtlasChronicle addon grows) | your setup, as V0 is now |
| **2. Clones** | Design phase 0 plus the clone half of phase 1: patch, clone IDs, frozen layouts, handshake | required | **a separate test stack first** (section 3), then your server |
| **3. Economy** | Design phase 2: currency, crafting, sustain, tiers 1-16, atlas progress and UI | stage 2's | your server |
| **4. Content** | Design phase 3: encounter modules, mechanic library, library bosses, more maps, 150+ affixes | | |
| **5. Endgame** | Design phase 4: passives, fragments, pinnacles | | |
| **6. Generated maps** | Design phase 5 (kits, pool IDs, navmesh assembly) | extended | test stack |

Why this order:
- You play a real Atlas loop weeks earlier, with no patch to build or install.
- Every engine (RNG, affixes, scaling, population, rewards) is proven before the riskiest work (map rows that can abort the server, symlinks in the data volume).
- The design already allows it: "prototype mode" (DESIGN §22 phase 0, ANALYSIS §3).

The cost: stage 1 lives with stock instances' compromises: doors opened, stock encounters reset, stock scripts on bosses. V0 already handles these (binds released, instances closed on restart, AI swapped safely). Stage 2 removes them.

**V0 features keep their place.**
- **Echoes** (the Chronicler's free runs) become the entry tier: unaffixed, bracketed by level, no map item needed. They are also what levelling characters use (BACKLOG B5 groundwork).
- **The Endless Timeways** stays as a mode. It uses the same engines (affix rolls by depth, atmosphere by depth), and later moves to generated maps (BACKLOG B15).

## 1. Rules for every work package

- **C1/C2:** no core edits, no playerbots edits; only hooks and public API. Before using a hook or accessor, check it exists in this tree.
- **C8, the run RNG:** all run randomness goes through `Atlas::RunRng`, seeded and persisted. Never `urand` in run logic. Note that `std::uniform_*_distribution` is implementation-defined, so RunRng does its own range mapping on a fixed engine (mt19937_64) to stay reproducible across compilers.
- **R1-R6 (BACKLOG):** level is a run parameter; runs can start from code; rare modifiers work on any creature; every run is logged with its seed; ID ranges reserved; `world_zone_id` column.
- **IDs:** only from DESIGN §19's reserved ranges (V0's own: creature 500000-500001, spell 91000-91099).
- **SQL:** module `data/sql/` only; base files stay idempotent (they are re-applied when they change).
- **Tests:** unit tests for pure logic go in `modules/mod-atlas/tests/` (already wired via `ACORE_MODULE_TEST_SOURCES`): RNG determinism, affix rolling, scaling math, the addon protocol parser. No live-stack e2e tests (AGENTS.md). In-game checks are listed per package for you to run.
- **Builds:** you build. Each package ends with the codestyle checker passing and a commit. I push when you say so.
- **Player-facing text:** lore-friendly (Keepers of Time, bronze dragonflight), plain gossip options, colours only where they mean something.

## 2. Stage 1: Atlas on echoes

Packages in order; each leaves V0 fully playable. Sizes: S < 1 day of work, M a few, L a week or more.

### 1.0 Foundations (M; no behaviour change)
- **Split `AtlasMgr.cpp`** (3,975 lines) by responsibility, keeping the public API so callers don't change:
  - `RunService`: sessions, lifecycle, binds, teleports, cooldowns.
  - `Population`: themes, pools, spawning, boss pick.
  - `Scaling`: levels, stats, damage scales.
  - `Rewards`: gold, items, loot fill.
  - `Records`: stats, leaderboards.
- **`RunRng`** (seeded, reproducible) plus `Run` gets a `seed`. V0's population and affix picks move onto it.
- **Run records:** `atlas_run_active` (resume after a crash or restart) and `atlas_run_log` (seed, source, tier, affixes, result, duration, party; R4). They replace `atlas_open_instance` (migrated in the same SQL).
- **LayoutProvider seam:** V0's `BuildSpawnPointsForMap` and `QueryDungeonEntrance` become a `StockLayoutProvider` that outputs the design's `Layout` (entrance, anchors clustered from stock spawns, an arena at the boss position). Everything downstream consumes `Layout`. Stage 2 adds `CloneLayoutProvider` behind the same interface.
- **Done when:** V0 plays exactly as before; `.atlas run` shows the seed; `.atlas seed <seed>` then starting the same echo (dungeon, difficulty, timeline, attunement) reproduces the same population; unit tests for RunRng pass.
- **Status (2026-11-22):** code written (split into AtlasMgr / AtlasPopulation / AtlasScaling / AtlasRewards / AtlasRecords, `RunRng.h`, `atlas_run_active` + `atlas_run_log`, `AtlasLayout.h` + `StockLayoutProvider`, `.atlas run` / `.atlas seed`, `tests/AtlasRunRngTest.cpp`); waiting for a build and the in-game check. Loot and reward rolls still use Random.h (they happen at kill time, outside the layout).

### 1.1 Role scaling at level 80 (M; DESIGN §9)
- Replace V0's multiplier stack with role normalization: `HP = base(level, unit_class) × roleHp × tierMult × affix`. Same shape for damage and armour.
- Level is a parameter (R1): echoes keep using the party level, maps use 80.
- Roles: trash, elite, rare, miniboss, boss, add. Values in config (`Atlas.Scaling.Role.*`, `TierGrowth`).
- Spell and heal scaling per creature (`expectedDmg80 / expectedDmgOriginal`) through the existing unit-script hooks. V0's `ComputeLevelDamageScale` is the starting point.
- **Done when:** `.atlas scaling` lists per-role health and damage that match DESIGN §9 at tiers 1 and 16; a level-80 party at tier 1 finds trash quick, elites a fight, and the boss about a heroic boss.
- **Status (2026-11-22):** code written (`AtlasScalingMath.h`, role stats in `AtlasScaling.cpp`, `Atlas.Scaling.Role.*` / `TierGrowth` / `SpellScaleMax`, `.atlas scaling`, heal hook, re-apply on `SelectLevel`, `tests/AtlasScalingMathTest.cpp`); waiting for a build and play. Findings fixed on the way: V0 read only the Classic columns of `creature_classlevelstats` (level-80 base health 5,342 instead of 12,600), double-counted attack time and kept each template's DamageModifier in melee, and scaled bosses' melee twice. Difficulty multipliers now default to 1.0. Bosses are still melee-only (V0's AI); spells matter from 1.3, when curated creatures keep their own AI.

### 1.2 Affix engine (L; DESIGN §10, AFFIXES)
- World table `atlas_affix` (DESIGN §10 columns) with module SQL for the **phase-1 subset** (~30: M01-M05, M08, M09, M11-M14, M24-M26, P01-P04, P06, P08, P11, A02, A03, C01-C04, B01, B02, B05).
- Handlers first: `STAT_MULT`, `AURA_MONSTER`, `AURA_PLAYER`, `ON_DEATH`, `PERIODIC_AREA` (ground shape 1 only, ≤ 15 yd), `PACK_COMPOSITION`, `LOOT`, `BOSS_MOD` (subset).
- Rolling: rarity (normal, magic, rare), exclusive groups, `bot_safe` filter when bots are in the group (on by default).
- The Endless Timeways switches from its 5 hard-coded affixes to engine rolls by depth.
- Open points to settle while building: Q15 (second hit of another school from the melee hook, without re-entry), Q16 (a "Scourge-touched" visual spell), Q17 (absorb hook; else drop P13).
- **Done when:** each subset affix can be forced with `.atlas affix next <codes>` and is visibly right in game; Endless rolls them by depth; unit tests cover rolling rules (counts, exclusive groups, bot-safe filter, determinism).
- **Status (2026-11-22):** code written. `atlas_affix` (world, `atlas_affixes.sql`) with the 30 subset affixes; `AtlasAffix.h` (pure: rolling, aggregation, text), `AtlasAffixes.cpp` (registry, player auras), `AtlasAffixEffects.cpp` (shields, death effects, Summoner waves, ground zones, extra hits); per-creature mods baked at spawn; `.atlas affix list|next`; `tests/AtlasAffixTest.cpp`. Spells picked from the client data: extra hits 13897 Fiery Weapon / 62867 Frozen Blows / 27286 Shadow Wrath / 6647 Fast Acting Poison; zones 19099 Blizzard, 55876 Plague Contamination, 36473 Desecration; burst 34234 Collapse; shield 33245 creature Ice Barrier (no script, unlike the mage ranks); casters are 12999 World Invisible Trigger. Player affixes are module passive spells 90001-90007. Splitting splits elites into lesser copies of themselves (maggots wait for the Scourge pool in 1.3). Q15 answered by design: the extra hit is a separate spell cast just after the melee hit, from the attacker's event queue, so no hook re-entry. Found and closed a latent V0 deadlock: summoning under the session lock re-entered it through the add-to-world hook (`OwnSpawnScope`). Waiting for a build and play.

### 1.3 Population as data, with Scourge corruption (L; DESIGN §8, SCOURGE, THEMES)
- World tables `atlas_theme`, `atlas_theme_creature` (role weights), `atlas_creature_curation` (allowed, blocked, untested). Module SQL for:
  - the **Scourge pool** (127 entries, SCOURGE.md);
  - **native themes** for the dungeons the echoes use most, starting with THEMES.md's five;
  - V0's five config themes, moved into the tables.
- Packs: a leader plus members by role weights, sized from the anchor's budget; per-map and per-anchor live caps (Q5).
- **Corruption by tier:** the share of anchors whose pack comes from the Scourge pool grows with tier; Scourge-touched natives in between.
- Rares: chance per pack, name prefix, 2-3 rare modifiers from the affix handlers; works on any creature (R3).
- `.atlas curate <entry|theme>`: spawns entries one by one and records the result. The allow list grows as you play.
- **Done when:** a tier-1 Deadmines echo is mostly Defias with a few Scourge packs, and a high-tier one is mostly Scourge; rares appear with their modifiers; nothing invisible-but-fighting spawns (model filter).
- **Status (2026-11-22):** code written. `atlas_themes.sql` (generated by `tools/themes_sql.py` from SCOURGE.md): `atlas_theme` with 50 *The timeline as it was* (native: each spot's own creature) and 51 *A timeline the Scourge swallowed* (pool), 147 Scourge pool rows by pack role, 137 curation rows (all untested; re-applying keeps what `.atlas curate` set). `AtlasTheme.h` (pure: corruption table, pack kinds, member roles, clustering); `StockLayoutProvider` clusters each dungeon's stock spawns into packs (`Population.PackRadius` 8 yd, same floor, up to 6) with the native entry per spot, and checks every entry with `IsFitToFight` (models, faction, flags). Population rolls a kind per pack (tier 1-3: 90/10/0 native/touched/Scourge; matches the appendix, not "a few Scourge packs" at tier 1, which has none), touched = aura 10848 + M04, rares are pack leaders (2-3 creature affixes, `Dungeon.MaxRares`), bosses follow the same table. AI policy: SmartAI without ScriptID keeps its own AI, everything else and every boss gets AtlasCreatureAI; the affix events moved to the unit script so they work for both, and kills without a player killer fall back to the instance's run. `.atlas curate <entry> [status] [note]` and `.atlas curate spawn <entry> [role]`. V0's config themes renumbered 1-8 ("Scourge swallowed" moved to the DB). `tests/AtlasThemeTest.cpp`. Waiting for a build and play; per-dungeon native themes from THEMES.md wait for curation results.

### 1.4 Map items, the device and portals (L; DESIGN §4, §7, §11)
- **Map items** (item entries 90000+, existing icons and displays, so no client change): one entry per tier band. The source and affixes are stored in a characters table keyed by item GUID (`atlas_map_item`; decision D2).
- **The device:** a Keepers of Time artefact in Dalaran beside the Chronicler (working name: *the Timeways Lens*). Its gossip lists the map items in your bags; you place one, and it opens portals. This avoids item on-use spells, which would need a client-known spell (decision D3).
- **Portals:** gameobjects with an existing portal display, a portal budget (default 6), a despawn timer. Re-entry spends a portal; out of portals means the run fails.
- **Completion:** boss death, then a reward chest at the exit, map drops for sustain, and a portal home.
- **Map tooltips:** the AtlasChronicle addon hooks bag tooltips and asks the server for that item's tier, source and affixes (item links don't carry them).
- **Starter maps:** the Chronicler gives a few tier-1 maps once per character at level 80 (lore: echoes she has sealed for you).
- **Done when:** a level-80 character gets starter maps, places one in the Lens, walks in with bots, clears, kills the boss, opens the chest and gets gear plus new maps; leaving and re-entering spends a portal; tooltips show the affixes.
- **Status (2026-11-22):** code written. Names (D6): *Echo Sigil*, *the Timeways Lens*, *Rift in Time* / *Rift to Dalaran*, *Cache of the Keepers*. Items 90001-90216 (one per tier and rarity: white normal, green magic 1-2 affixes, blue rare 3-4), so tier and rarity show without the addon; `atlas_map_item` (characters) holds dungeon, theme, seed and affixes, pruned of vanished items at startup; `atlas_map_grant` for the starter sigils. Found on the way: the 3.3.5 client takes bag icons from its own Item.dbc (`CGItem_C::GetInventoryArt`), every Item.dbc entry is already used by AC, so unpatched clients show a question mark on sigil buttons; the addon's `Sigils.lua` puts a pocket watch there (not INV_Misc_Rune_01, the Hearthstone's icon) and adds the tooltip (`M` request). Lens (display 7150 BE_ScryingOrb_Epic, type questgiver; the first pick, 7133 COT_HourGlass, is the Caverns' 150-yd landmark and engulfed the player in sand: check GameObjectDisplayInfo bounding boxes before picking a model, guid 8000101 beside the Dalaran Chronicler) lists sigils; placing one creates a map session (tier, seed, affixes from the sigil; difficulty `Atlas.Map.Difficulty`) and a rift (8475 mage portal to the Caverns of Time; home rift 8111, Dalaran's). First crossing free per member, bots cross with the user; later crossings and wipes spend one of 6. Gameobjects inside a run are summoned from OnMapUpdate (map thread). Completion: cache (1387) and a rift home where the master fell, bots paid at once, unclaimed shares follow at close; closes when no real player is inside or after 10 min. No cooldown for maps. `.atlas map give|list`. `tests/AtlasMapItemTest.cpp`. Waiting for a build and play; the Lens and rift positions need a look in game.

### 1.5 Rewards ladder (M; DESIGN §14)
- `atlas_loot_tier`: tier → module loot templates that reference stock tables (`reference_loot_template`), following the ilvl ladder: 200 at tiers 1-5, up to 264 at 16. This replaces V0's random item pool for maps; echoes keep the V0 pool with its ceiling.
- Map drops: base chance per pack, guaranteed from the boss, the +1 and +2 tier chances, and a first-completion bonus. Map items and currency drop only for real players.
- Quantity and rarity from affixes, clamped by config.
- **Done when:** tier-1 maps drop item level 200, tier-6 maps 213 or more; clearing tier-N maps sustains tier N about two times out of three.
- **Status (2026-11-23):** code written. Deviation: no loot templates over `reference_loot_template`; the ladder filters item_template by item level instead (`atlas_loot_tier`: tier → band, epic share, gear count; PvP gear out by its resilience stat), which adds no rows to AC's loot tables and keeps the class filters (armour type, primary stat) the raid tables lack. `AtlasLoot.h` (pure: ladder defaults, quantity, chance scaling, sigil tier shift, a run's sigils) with `tests/AtlasLootTest.cpp`, whose simulation checks the sustain target (0.60-0.73 at the defaults, higher with quantity). Map gear: cache share (1 piece, 2 from tier 11, more with quantity) and creature drops (boss 2, rare 1, elite 5%, trash 1%, scaled). Sigils: master's (shift 45/45/9/1 lower/same/+1/+2), cache extra 20%, a cleared pack's chance per force point it held (0.13%; first a flat 1% per pack, which Maraudon's many lone spawns made drop 2-3 sigils a run; changed 2026-11-23), first clear of a (dungeon, tier) per account (+1 of the next tier; `atlas_account_progress`, the ground for §15). Quantity and rarity from the affixes' bonuses, capped (`Atlas.Loot.MaxQuantity`/`MaxRarity`). `.atlas loot [tier]`. Echoes keep the V0 pool. Waiting for a build and play.

### 1.6 Atmosphere, first version (S-M; ATMOSPHERE)
- `atlas_atmosphere`: theme × corruption band → look light (plus an optional affix look in a second anchor slot).
- Sent as our own `SMSG_OVERRIDE_LIGHT` with an anchor light (1773 or 1826) on map entry and when the look changes. The client clears it when leaving the map.
- Per-instance zone music where a theme has a fitting track.
- **First, the in-game test:** heavy-fog looks (40, 1673) in Forge of Souls (indoor, no lights), Culling of Stratholme and Shadow Labyrinth. If indoor WMO fog swallows the override, fall back to outdoor-ish maps plus music, and keep screen effects for stage 2's patch.
- **Done when:** echoes and maps visibly change with theme and tier, or the test result is recorded in ATMOSPHERE.md and the fallback is in place.
- **Status (2026-11-23):** code written. `atlas_atmosphere` (world): theme looks (1374 beasts, 1769 dragonkin, 214 Legion, 670 elements, 61 giants, 1607 warbands, 2538 machines, 189 shattered, 2508/1817 Scourge), default 84 (Caverns bronze, also the native theme), corruption 3/4/5 → 27/333/1673 over any theme, affix looks A02 → 56, A03 → 43. `AtlasAtmosphere.h` (pure pick: theme+level > any+level > theme+any > default) with tests. Sent as SMSG_OVERRIDE_LIGHT over anchors 1826 (theme) and 2488 (affix, cleared when none) on OnPlayerEnterAll and again 3 s later. Zone music: column present, no tracks picked (PlayMusic plays a sound kit once). `.atlas light [id] [anchor] [fade]` is the in-game test tool. All light ids verified in the client's Light.dbc. **The in-game test is still to do** (interiors: does WMO fog swallow the override?); record the result in ATMOSPHERE.md.
- **Affix visibility (added 2026-11-22, user ask):** affixes work as numbers today and show only through their own spells (extra elemental hits, Blizzard and plague zones, the ice barrier, death bursts). Proposed:
  - creatures: one visual-only marker aura per creature affix family (fire, frost, shadow, nature, armour, speed), stock client spells whose aura is a persistent visual with no effect, picked from Spell.dbc like the 1.2 carriers; rares get a stronger one;
  - players and bots: the module's affix spells (90001-90007) are unknown to the client, so no icon; until the stage 2 patch adds Spell.dbc rows, the 1.7 HUD lists them and the run start says them in chat;
  - optional affix look (lights) as already planned above.
  - **Status (2026-11-23):** creature markers done. Client DBCs read (read-only) from the client's locale MPQs with a scratch MPQ reader, since Docker was off; candidates = spells whose only effect is a dummy aura with a looping state visual, ranked by use in AC's creature addon auras, checked for scripts (Stoneform 70733 has one, dropped). `atlas_affix_marker`: Brutal/Frenzied 44427 Enrage, Searing 47705 / Frostbitten 47706 / Warded 47704 (Grand Magus Telestra's fire, frost, arcane bodies), Necrotic 56572 Dark Aura, Blighted 41290 Disease Cloud; rares 48019 Arcaneform (`Atlas.Affix.RareMarker`). Max 2 per creature, baked into the owned creature's mods, re-applied by the creature update hook after evades. `.atlas marker <spell>` auditions. Not seen in game yet: the looks are guesses from names and need a look.

### 1.7 Run HUD in the addon (S)
- AtlasChronicle shows a small bronze frame during a run: map name, tier, affixes (hover for details), portals left, packs and boss progress. Same addon protocol, a new `RUN` message family.
- **Done when:** the frame appears on entering a run and updates on kills and portal use.
- **Status (2026-11-23):** code written, together with the user's boss redesign: masters are chosen at populate but summoned beside the party once a share of the run's weighted forces fall (`AtlasForces.h`, `Atlas.Boss.SummonAtForces` 75, weights 1/3/10, a 5 s warning, a decorative rift GO 900004, summoned from OnMapUpdate). HUD protocol UH/UA/UZ/UX (server pushes: arrival, hello, header on change, end); `RunHud.lua` (title by kind and rarity, the master's bar, foes slain, rifts, affix icons from SpellIcon.dbc with names and values). Also: SmartAI written for quests (actions 18/42/47/117, e.g. Servant of Razelikh's invincibility) no longer kept in runs; atmosphere sends are logged. Waiting for a build and play.

### Stage 1 done when
DESIGN phase 1's goal is met on stock instances: a player plus bots open a map at the device, clear it, kill the boss, and get loot and new maps, with ~30 affixes, Scourge corruption by tier and level-80 scaling.

## 3. Stage 2: clones (DESIGN phase 0 plus the clone half of phase 1)

> **2026-11-24: superseded by `atlas-endgame.STAGE2.md`** (the implementation brief for a fresh session). Decisions
> since this section was written: no second Docker stack, clones are developed on the play setup (user's call,
> overriding D5), additive and off by default; the client patch is one MPQ, `Data\patch-T.MPQ`, copied by the user;
> Wowdecomp2 stays read-only. Stage 1 is done (all of 1.0-1.7; 1.6's indoor fog test still open).

Never developed on the play server. A clone map row without its `instance_template` can abort the server (Q3), and symlinks go into the client-data volume.

### 2.0 Test stack (S, with you)
- A second compose project: images tagged `DOCKER_IMAGE_TAG=atlas-test`, its own database volume and its own client-data copy, other ports. The play stack's `:master` images, database and data volume are never touched.
- One-time copy of your characters and accounts if you want your own characters there (read-only on the play side).

### 2.1 Client patch skeleton (M)
- A tool that builds `patch-A.MPQ` from module-owned DBC rows, for the 5 starter clones (900-904, CLONES §2):
  - `Map.dbc`, `MapDifficulty.dbc` (required) and `Light.dbc` copies;
  - later `SpellItemEnchantment.dbc` and `Spell.dbc` rows (affix tooltips, no-icon screen-effect spells, ATMOSPHERE §4).
- Server: `map_dbc`, `mapdifficulty_dbc` and `instance_template` rows as one module SQL unit (never one without the others).
- Symlink tool for `maps/`, `vmaps/` and `mmaps/` under clone IDs, driven by `atlas_map_source`.
- **Done when:** on the test stack, a patched client teleports into an empty Deadmines clone and walks it; an unpatched client is refused cleanly.

### 2.2 Handshake and gating (S)
- The addon (or the MPQ's FrameXML; Q13) says hello with the patch version.
- The server marks the session; `OnPlayerCanEnterMap` refuses clone IDs to unpatched sessions; the Lens refuses them with a message.

### 2.3 Harvest and frozen layouts (L)
- Offline tool: stock `creature` rows → clustered anchors, arenas, patrols, structural gameobjects, then exported as frozen `atlas_layout_*` SQL (DESIGN §6.1).
- A manual pass per map: wings, entrances and exits.
- `CloneLayoutProvider` implements the interface from 1.0.
- **Done when:** `.atlas layout show` marks anchors, arenas and patrols on all 5 clones, and they make sense on the ground.

### 2.4 Runs on clones (M)
- Maps switch from stock instances to clones; echoes and the Endless Timeways can stay on stock instances or follow (decide then).
- Graveyards: `OnPlayerBeforeChooseGraveyard` (hub). Zone auras stripped (`atlas_zone_strip`: the Wintergrasp buff in DTK and HoL).
- Verify Q9-Q12 with bots on clones (strategies, raid icons, following through portals, death and release).
- **Done when:** DESIGN phase 1's "done when" holds on clones, on the test stack, then on your server.

## 4. Stages 3-6

Planned in detail when stage 2 ends; scope is DESIGN §22 phases 2-5.
- **Economy:** currency items and crafting (Lens gossip first if item-targeting spells fail, Q8), sustain tuning, tiers 1-16, `atlas_account_progress`, and the atlas window in AtlasChronicle.
- **Content:** encounter modules (Scourge Rift first), the mechanic library, library bosses, more clones and wings, 150+ affixes.
- **Endgame:** passives, fragments, pinnacle encounters.
- **Generated maps:** kits, the bake tool, pool IDs, navmesh assembly, `KitLayoutProvider`; the Endless Timeways moves there (BACKLOG B15).

## 5. Decisions for you

| # | Question | Recommendation |
|---|---|---|
| D1 | Stage order: Atlas on echoes first, or the client patch and clones first as the design says? | **Echoes first** (section 0). |
| D2 | Map item data: a table keyed by item GUID, or enchant slots on the item (DESIGN Q6)? | **Table for now.** Enchant slots without the patched DBC show nothing, and unknown enchant IDs on the client are unverified. Revisit in stage 2. |
| D3 | Opening a map: device gossip that lists maps in bags, or using the item? | **Device gossip.** Item use needs a spell the client knows; gossip needs nothing. |
| D4 | Keep echoes and the Endless Timeways as modes beside maps? | **Yes** (section 0). |
| D5 | Set up the test stack now, or only before stage 2? | ~~Before stage 2~~ **No test stack** (2026-11-24, user): stage 2 runs on the play setup, additive and off by default (STAGE2 §6). |
| D6 | Lore names: the map item, the device, portals, currencies. | Draft a naming pass with 1.4 (for example: map item "Echo Sigil", device "the Timeways Lens", portals "rifts in time"). |

## 6. First steps once D1-D4 are settled
1. 1.0 Foundations: split AtlasMgr, add RunRng and run records, add the StockLayoutProvider seam. Unit tests for RunRng.
2. 1.6's in-game atmosphere test, run early because it's cheap: a GM command that sends one light override, so you can try looks before the table exists.
3. 1.1, then 1.2 and 1.3 in parallel-friendly slices, then 1.4, 1.5 and 1.7.
