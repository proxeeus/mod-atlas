# atlas-endgame: backlog for a later version

Status: backlog only, not scheduled. Written 2026-11-22.
Scope: features that extend mod-atlas past roadmap phase 6 (DESIGN section 22). Nothing here is part of
phases 0–6, and nothing here may delay them. Section 2 lists the few cheap rules phases 1–4 must respect
so these features stay possible later.

Everything below follows the design document: its pillars (DESIGN section 1), its hard constraints C1–C8
(section 2) and its architecture rules (section 5). When an item here conflicts with the design document,
the design document wins until this backlog item is promoted into it.

---

## 1. Vision for the later version

Phases 1–6 make the Atlas a strong endgame **room**: you go to the map device, run maps, come back.
The later version makes the Atlas the endgame **of the whole world**:

- The **Scourge corruption** frame (DESIGN section 8) starts while levelling, not at level 80. Corruption
  lives in world zones as well as in maps, and the two affect each other.
- The world **remembers** what happened to the character (who killed it, what it saved, what it ignored),
  and the Atlas later serves that history back as content.
- Levelling gets a small taste of the Atlas loop (affixes, short runs, map shards), so arriving at 80 is a
  continuation, not a new game.

### Extra pillar for this version

5. **Emergence from feedback, not from one smart generator.** Each feature is small and writes what happened
   into one shared record (World Memory, B1). Other features read it. Example chain: a mob kills you → it
   becomes a nemesis (B3) → the zone's corruption rises (B6) → the Director starts an outbreak there (B2) →
   the linked Atlas map rolls Scourge affixes more often (B7) → the unavenged nemesis appears there as a
   Scourge-raised rare (B8).

### Packaging

All items are optional subsystems **inside mod-atlas**, each behind its own config switch
(`Atlas.World.*`, `Atlas.Levelling.*`), off by default. Turning one off must leave the core map loop intact.
Links to other modules (mod-lore-codex, mod_weather_vibe) are soft and read-only: if the other module is
absent, the feature still works without that link.

---

## 2. Rules phases 1–4 must respect (cheap, keep the door open)

These cost almost nothing now and avoid a rewrite later.

| # | Rule | Why |
|---|---|---|
| R1 | Scaling code takes the **level as a run parameter**. The level-80 base (`base80*`, DESIGN section 9) is the default value, not a constant baked into the formulas. | B5 (Delves) runs the same engines at levels 15–79. |
| R2 | Population, affix handlers and the run RNG don't assume level 80 or the map device. A run can be created from code with any source map, level and affix list. | B5, B9, B13 start runs from other entry points. |
| R3 | The rare-modifier handler accepts an **arbitrary creature** (not only one the population spawned), and stores its modifiers as data. | B3/B8: a world mob gets promoted to a nemesis with the same modifiers. |
| R4 | Finished runs are logged with seed, affixes and result (`atlas_run_log` already planned). Keep the seed and enough fields to replay a run. | B12 (seeded weekly map) compares against past runs. |
| R5 | Reserve ID ranges now for: name-bank creature entries (B1.3), boon spells (B1.4), backlog items and currencies. | Avoid renumbering custom DBC rows later (client caches). |
| R6 | `atlas_map_source` gets a nullable `world_zone_id` column. | B7 links a map to a world zone. |

---

## 3. Foundations (build before any feature below)

### B1.1 World Memory
A persistent event log plus small derived state tables in the characters DB.
- Events: player death (killer entry, position, level), named kill, outbreak result, contract result, run
  result (already `atlas_run_log`).
- Derived state: corruption per world zone (B6), active nemeses (B3), town states (B6).
- Write API inside the module; every feature writes through it. Retention and pruning rules per event type.

### B1.2 Director
A storyteller in the style of RimWorld: per-zone **tension** fed by player activity (kills, deaths, time in
zone, group size), time of day, weather (mod_weather_vibe if present), active game events and zone
corruption. It draws events from a weighted deck with cooldowns.
- Presets: Calm / Standard / Chaotic.
- Only runs in zones where a real player is present. Bots alone never trigger events.
- All draws use a seeded RNG (C8).

### B1.3 Name bank
Thousands of `creature_template` rows generated offline, each with a **fixed** generated name and title
("Gruk the Flayer"). At spawn the module sets the model, level, stats and modifiers.
- Each entry keeps one name forever, so the client name cache (WDB) never goes stale (DESIGN section 17).
- Entries are allocated and never reused for a different name. Name grammar per theme (Defias, Scourge,
  troll, nerubian…).
- Also usable by Atlas rares.

### B1.4 Boon library
Passive buffs as custom spells (`spell_dbc` + client `Spell.dbc`), with rarity, tags and synergy groups.
Used by B4 (Fated characters) and B9 (Gauntlet maps). Data model like `atlas_affix`: handler, parameters,
weight, exclusive group, `bot_safe`.

### B1.5 Levelling-range scaling
R1 applied: a `baseL*` table from `creature_classlevelstats` for every level, role multipliers unchanged.
Loot for levelling content comes from existing loot tables by level band, not from the Atlas item-level
ladder.

---

## 4. Levelling features

### B2. Scourge outbreaks (zone events)
The Director starts public events in levelling zones: plague cauldron, village siege, necropolis shard,
infected caravan, plagued wildlife.
- Scaled to the zone's level (B1.5). Built from Atlas pack spawning and affix handlers.
- Assets: Scourge Invasion creatures, objects and visuals already in the 3.3.5 data.
- Results go into World Memory: success lowers zone corruption, ignoring or failing raises it (B6).
- Bot-safe: open-world combat only.
- Hooks: `OnPlayerUpdateZone`, `OnPlayerUpdateArea`, `OnPlayerCreatureKill`, `GameEventScript`.

### B3. Nemesis
When a world mob kills a player or one of their bots (`OnPlayerKilledByCreature`, verified), it may be
promoted:
- replaced by a name-bank entry (B1.3) with the same model, plus rare modifiers (R3);
- stronger with each further kill, taunts on re-encounter, roams the zone;
- shows up in contracts (B10) and on the addon map.
Killing it gives a trophy and a codex-style record. Not killing it feeds B8. Stored per character.
Caps: maximum active nemeses per character and per zone; promotion chance configurable.

### B4. Fated characters (opt-in roguelite levelling)
A flag set at creation or on a fresh level-1 character.
- **Boon draft:** at each level-up (`OnPlayerLevelChanged`, verified) pick 1 of 3 boons from B1.4,
  through the addon.
- **Optional rules:** permadeath, ironman (no auction house, no trading), random talents.
- **Legacy:** a permadeath character's death grants account-wide Legacy points that unlock better starting
  boons. Its **echo** (gear snapshot, class, name) is stored for B11.
- Reaching 80 converts unused Legacy into Atlas points or currency (to tune).

### B5. Delves (a small Atlas while levelling)
The Director opens portals in caves and ruins to 10–15 minute runs:
- cloned dungeons from the right era (the clone pipeline of DESIGN section 6.1), level from B1.5,
  1–2 affixes, short layout (one wing);
- rewards: levelling gear, and **map shards** that are account-bound and usable only at 80 (Atlas sustain
  head start);
- teaches the Atlas vocabulary (affixes, portals, run rules) early.
Depends on R1, R2 and the phase 1 clone pipeline.

### B6. Zone corruption and town states
Each world zone has a corruption value (World Memory), raised by ignored outbreaks and unavenged nemeses,
lowered by events, contracts and linked Atlas maps (B7).
- High corruption: more outbreaks, Scourge-touched natives in the open world.
- A curated list of towns can fall (spawns swapped with `phaseMask`, flight master and vendors gone) and be
  retaken by an event. Only curated towns, so stock quests don't break.

### B10. Contracts board (procedural quests)
A board object per town with generated contracts:
*verb* (hunt, collect, rescue, escort, investigate, deliver) × *target* (zone creature pool by level, type
and faction) × *place* (sub-zones, spawn clusters) × *giver*.
- Server-side tracking with `OnPlayerCreatureKill` and `OnPlayerLootItem`; UI in the Atlas addon; reward by
  mail or at the board.
- **Not real quests:** `ServerScript::CanPacketSend` takes a `const` packet, so outgoing quest text can't be
  rewritten, and the client caches quest text by ID. Blocking the stock packet and sending a custom one is
  possible but fights the cache. The addon route avoids it. A Wowdecomp2 patch that skips the cache for one
  quest-ID range is the fallback if real quest-log entries are wanted.
- **Hunt contracts:** track a rare or a nemesis through clue objects (tracks, carcasses).
- **Lore link (soft):** flavour text drawn from mod-lore-codex's people, factions and places when that module
  is present.

### B14. Bots as people in the world (investigate first)
- **Adventurers in distress:** a random bot party wiped near the player → rescue event (revive, escort out).
- **Rivals:** a bot party competing for the same contract.
Both need a way to direct random bots without editing mod-playerbots (C2). Investigate the public interface
first; drop the rivals idea if it needs playerbots changes.

---

## 5. Atlas features fed by the world

### B7. The world map is the Atlas
Each Atlas source map links to a world zone (R6; Deadmines ↔ Westfall).
- Higher zone corruption: the linked map rolls Scourge affixes and packs more often, with matching loot
  bonuses (a world-driven version of the corruption level by tier, DESIGN section 8).
- Completing the map at tier ≥ N lowers the zone's corruption and can retake fallen towns (B6).

### B8. Nemesis promotion
Unavenged nemeses (B3) are **raised by the Scourge** and appear as level-80 rares in Atlas maps whose linked
zone (B7) is where they were made, keeping their name and modifiers.
- **Nemesis Council** pinnacle: with enough raised nemeses, a fragment set opens a boss rush of them.

### B9. Gauntlet maps (boon runs)
A map type: a chain of arenas; between rooms choose a boon or a curse from B1.4, kept for the run only.
Arena-only, so it suits phase 1 clones and bots (no navigation). Uses the boss mechanic library
(DESIGN section 13).

### B11. Fallen heroes
Echoes of dead Fated characters (B4) and gear snapshots of the player's bots become Atlas rares using the
class's real spells. They drop Legacy points or that character's signature item.

### B12. Weekly seeded map
One fixed seed and affix list per week. Results are compared against the player's own past runs in
`atlas_run_log` (R4). Cheap because runs are already reproducible from a seed (C8).

### B13. World-event sextants
A variant of the Atlas Compass (DESIGN section 11) that attaches a **live** outbreak type from the world to
a map as its encounter module.

### B15. Endless descent (needs phase 5)
An infinite dive (Azjol-Nerub theme) on kit maps: depth-scaled darkness and difficulty, supply caches,
checkpoints. A flagship mode for the generated maps, using the kit grid and per-run navmesh assembly
(DESIGN section 6.2).

---

## 6. Suggested order (when the later version starts)

| Step | Items | Needs |
|---|---|---|
| L1 | B1.1 World Memory, B1.5 levelling-range scaling, B1.3 name bank | R1, R3, R5 respected in phases 1–4 |
| L2 | B3 Nemesis, B8 Nemesis promotion | L1 |
| L3 | B1.2 Director, B2 Outbreaks, B6 Zone corruption, B7 World map is the Atlas | L1, R6 |
| L4 | B5 Delves, B10 Contracts | L1, L3 for Director-spawned portals |
| L5 | B1.4 Boon library, B4 Fated characters, B9 Gauntlet maps, B11 Fallen heroes | L1 |
| L6 | B12, B13 | L3 |
| Later | B15 Endless descent (after phase 5), B14 bots in the world (after investigation) | |

Not in this backlog on purpose (high cost, payoff depends on everything above): caravans and a supply
economy, factions taking territory from each other.

---

## 7. Open questions

| # | Question |
|---|---|
| BQ1 | Can random bots be directed (move to a point, fight, be "rescued") through mod-playerbots' public interface only? Decides B14. |
| BQ2 | `phaseMask` town states: which stock quests and scripts in the curated towns break when spawns are phased out? Audit per town. |
| BQ3 | How many name-bank entries are needed, and does a large `creature_template` range slow startup noticeably? |
| BQ4 | Fated permadeath: lock the character (playable as a ghost-only "museum") or delete it? Deleting is irreversible; prefer locking. |
| BQ5 | Does the Director need group awareness (one event deck per group, not per player) to avoid duplicate events for a player with bots? |
| BQ6 | Map shards from Delves: how many before they distort Atlas sustain at tier 1? |

## 8. Facts checked for this backlog (2026-11-22)

| Fact | Location |
|---|---|
| Hooks exist: `OnPlayerKilledByCreature`, `OnPlayerJustDied`, `OnPlayerReleasedGhost`, `OnPlayerLevelChanged`, `OnPlayerUpdateZone`, `OnPlayerUpdateArea`, `OnPlayerCreatureKill`, `OnPlayerCreatureKillCredit`, `OnPlayerLootItem`, `OnPlayerGiveXP`, `OnPlayerQuestComputeXP`, `OnPlayerCompleteQuest`, `OnPlayerBeforeChooseGraveyard`. | `src/server/game/Scripting/ScriptDefines/PlayerScript.h` |
| `OnCreatureAddWorld` exists. | `src/server/game/Scripting/ScriptDefines/AllCreatureScript.h` |
| `OnGameObjectLootStateChanged` exists. | `src/server/game/Scripting/ScriptDefines/AllGameObjectScript.h` |
| `GameEventScript` exists; `OnWeatherChange` exists in `ALEScript`. | `GameEventScript.h:32`, `ALEScript.h:36` |
| `CanPacketSend(WorldSession*, WorldPacket const&)`: outgoing packets can be blocked but not modified. | `src/server/game/Scripting/ScriptDefines/ServerScript.h` |
