# mod-atlas — V0

The **V0 backbone of Atlas**, a level-80 endgame loop for **AzerothCore** (WotLK 3.3.5a, playerbots-friendly).

V0 runs inside stock dungeon instances (no client patch needed). Later versions move runs to cloned map IDs with map items, affixes and tiers; see the Atlas design notes in the server repo (`.agents/plans/atlas-endgame/`).

**Origin:** V0 is derived from [mod-dungeon-master](https://github.com/trauntrow/mod-dungeon-master) by trauntrow and contributors (AGPL-3.0), reworked for safety, performance, AzerothCore code style and a Keepers of Time lore frame. It stays under AGPL-3.0.

**In game:** Chronicler Esmadormu of the **Keepers of Time** (bronze dragonflight) opens *echoes* of known dungeons from fractured timelines, where the halls belong to other masters: beasts, demons, the restless dead. Players pick a difficulty, a timeline (creature theme) and a dungeon, then walk the echo: an emptied instance repopulated with level-scaled themed enemies and a master (boss). Mending the echo earns rewards. **The Endless Timeways** (roguelike mode) chains echoes with escalating difficulty, instabilities (affixes) and a growing stat buff, Timeways Resonance, until the party falls.

> ⚠️ **Early development** — expect bugs and rough edges. Bug reports welcome via Issues.

---

## Features

### Core System
- **44 dungeons** — Classic, TBC, and WotLK 5-man instances (The Oculus is left out: it needs drake vehicles)
- **10 timelines (creature themes)** — the dungeon as it was (its own inhabitants, pack by pack), a timeline the Scourge swallowed (a curated Scourge pool), or one of eight creature types: beasts, dragonkin, the Legion, elementals, giants, mortal warbands, rogue machines, or a shattered mix
- **Scourge corruption** — the higher the tier, the more packs the plague has touched or replaced: 10% touched at tiers 1-3, mostly Scourge from tier 10, all Scourge at 16
- **Packs and rares** — creatures stand in packs where the dungeon's own creatures stood; a pack's leader may be a rare from another timeline, with 2-3 modifiers of its own
- **6 difficulty tiers** — Novice through Grandmaster with tuned HP/damage/reward multipliers
- **Level scaling** — Scale creatures to your party's level or use the tier's natural range
- **Any dungeon, any level** — A level 80 can walk an echo of a classic dungeon scaled to 80, or at its original difficulty
- **Real dungeon bosses** — Final bosses are pulled from a global pool of all dungeon bosses across Classic, TBC, and WotLK instances, matched to the session's theme.
- **Party support** — Solo or groups up to 5 (Normal dungeon difficulty only; each run starts in a fresh instance and the instance is released afterwards)
- **Group Loot support** — Items dropped by enemies support the Group Loot game mechanic where party members roll Need or Greed on qualifying items
- **Per-player difficulty** — HP and damage scale with party size; solo players get a reduction
- **Auto-resurrect** — Dead players revive at the entrance when combat ends
- **Environmental damage scaling** — Native dungeon hazards are scaled down for level-mismatched parties, hard-capped at 3% max HP per tick (adds summoned during a run are level-scaled instead of capped)
- **Cooldown system** — Configurable per-character cooldown between runs
- **Persistent stats** — Tracks runs, kills, deaths, fastest clear times per character
- **Statistics & Leaderboards** — Separate tracking for normal runs and roguelike mode. Normal stats track win rate, kills, deaths, K/D ratio, and fastest clear. Roguelike stats track highest tier, most floors, total floors cleared, and longest run. Leaderboards include Normal Fastest Clears, Roguelike Highest Tier, and Roguelike Most Floors — with your own entries highlighted
- **The Chronicle of Echoes** — With the `AtlasChronicle` client addon, the records open in a window styled as the Keepers of Time's chronicle: the swiftest mendings (filterable by dungeon), the deepest Endless descents, and your own thread. Without the addon, the Chronicler reads the records out in chat
- **Level brackets** — A tier opens once everyone in the party reaches it and stays open afterwards; no dungeon may start above the party's lowest level
- **GM commands** — `.atlas reload`, `.atlas status`, `.atlas list`, `.atlas end`, `.atlas clearcooldown`, `.atlas run`, `.atlas seed`, `.atlas affix`, `.atlas curate`, `.atlas scaling`

### The Endless Timeways (roguelike mode)
- **Infinite progression** — Mend an echo, get pulled into the next one, repeat until the party falls
- **Escalating difficulty** — Enemy HP and damage scale per tier (linear, then exponential)
- **Timeways Resonance** — +10% all stats per echo mended (the module's own server-side passive aura, spell 91001; no class spell is modified; not shown as a buff icon, the total is announced in chat)
- **Affixes (instabilities)** — 30 affixes from the `atlas_affix` table: monster damage, extra elemental hits, speed, defence, absorb shields, on-death bursts, desecrated ground and splitting, player debuffs, Blizzards and plague clouds, larger packs, more elites and rares, a sturdier boss that calls servants. The Endless Timeways rolls 1 to 3 by depth
- **Random dungeon rotation** — Each floor picks a new dungeon appropriate for your level band
- **Tier-scaled rewards** — Better loot at higher tiers, with increasing epic drop chance
- **Timeways Quartermaster** — Sell items, repair gear and buy ammo/reagents at the start of each echo


---

## Installation

1. **Clone into modules:**
   ```bash
   cd azerothcore-wotlk/modules
   git clone https://github.com/proxeeus/mod-atlas.git
   ```

2. **Re-run CMake and rebuild** the server.

3. **Run the world database SQL:**
   ```sql
   SOURCE data/sql/db-world/base/atlas_world_setup.sql
   ```

4. **Run the characters database SQL:**
   ```sql
   SOURCE data/sql/db-characters/base/atlas_characters_setup.sql
   ```

5. **Copy the config:**
   ```bash
   cp modules/mod-atlas/conf/mod_atlas.conf.dist etc/mod_atlas.conf
   ```

6. **Enable it:** set `Atlas.Enable = 1` (the default is 0, also when no config file exists).

7. **Restart the server.** Chronicler Esmadormu appears in the major cities and at the Caverns of Time (she stays hidden while the module is disabled).

8. **Optional, per player: the Chronicle window.** Copy `client_addon/AtlasChronicle` into the WoW client's `Interface/AddOns` folder. Open it with `/chronicle`, or from the Chronicler's records menu (**Open the Chronicle of Echoes**). Unmodified clients still play normally and get the chat records.

---

## NPC Spawn Locations

Chronicler Esmadormu (entry `500000`) has 11 spawns (guids 8000001–8000011). You can also spawn one manually with `.npc add 500000`.

On this server, eight of them stand 3 yards to the left of **Chronomancer Noriol** (mod-playerbot-reset-instances), facing the same way, so each city has one "bronze corner". Noriol's positions are this server's own `.npc add` spawns, so check these in game and adjust with `.npc move` if one clips a wall.

| City | Placement | Map | Coordinates |
|------|-----------|-----|-------------|
| Stormwind | beside Noriol | 0 | -8998.05, 871.70, 148.60 |
| Ironforge | beside Noriol | 0 | -4620.79, -905.76, 501.07 |
| Darnassus | beside Noriol | 1 | 9970.96, 2292.60, 1341.39 |
| The Exodar | beside Noriol | 530 | -3972.38, -11639.35, -138.89 |
| Orgrimmar | beside Noriol | 1 | 1653.78, -4384.47, 22.22 |
| Thunder Bluff | beside Noriol | 1 | -1242.15, 116.15, 130.19 |
| Undercity | beside Noriol | 0 | 1593.25, 279.66, -43.10 |
| Dalaran | beside Noriol | 571 | 5808.61, 799.05, 662.14 |
| Silvermoon | Walk of Elders | 530 | 9738.0, -7454.0, 13.56 |
| Shattrath | Terrace of Light | 530 | -1850.0, 5436.0, -12.1 |
| Tanaris | Caverns of Time entrance | 1 | -8201.88, -4495.20, 9.01 |

---

## How It Works

### Walking an echo

1. Talk to Chronicler Esmadormu
2. Choose **difficulty tier** (Novice → Grandmaster)
3. Choose **attunement**:
   - **Attune to my party** — creatures match your group's average level
   - **Leave it as it was** — creatures stay at the tier's natural level range
4. Choose a **timeline** (who rules the halls: the Scourge, the Legion, beasts…)
5. Choose a **dungeon**, or let the timeways choose
6. Confirm → enter the echo → defeat its master → collect rewards → return

**Brackets.** Tiers and dungeons follow the lowest level in the party. A tier opens at its minimum level, or at the first dungeon it can roll if that starts higher (Novice opens at 13 with Ragefire Chasm), and stays open afterwards. A dungeon is only offered when it doesn't start above the party's lowest level, and only when everyone meets its own entry requirements (attunement quests, item level). The menu lists only those dungeons.

Echoes run on **Normal** dungeon difficulty only (a heroic run could lock the group out of the real heroic). Each run starts in a fresh instance: a temporary Normal save to that dungeon is reset first, and the run is refused if someone is still inside that saved instance.

### The Endless Timeways

1. Talk to the Chronicler → **I will descend the Endless Timeways**
2. Choose difficulty, scaling, and theme (or random theme each floor)
3. Mend echo 1 → Timeways Resonance grows by +10% all stats → the next echo forms
4. Each depth is harder; instabilities (affixes) start at depth 3
5. Keep going until the party falls — depth reached, echoes mended and kills go to the Endless Timeways chronicle (separate from normal runs)

### Level Scaling

Creatures are force-scaled to the session's target level regardless of their original template. A level 80 Icecrown ghoul becomes a proper level 14 mob with correct HP, damage, armor, and nameplate display.

- **Effective Level** = party average (Scale to Party) or tier midpoint (Use Dungeon Difficulty)
- **Level Band** = EffectiveLevel ± `LevelBand` (default ±3), clamped to the tier's range
- **Role stats** (DESIGN §9): every creature has a role (trash, elite, rare, miniboss, boss, add). Health, melee and armor come from the stock base stats of the run's level, in the column of that level's expansion (WotLK numbers at 71+), times the role's multipliers, the tier, the difficulty and the party factor (1.0 for a party of five). The template's own health and damage modifiers are ignored, so a boss with a huge DamageModifier hits like any other boss of the run
- **Spells and heals** scale by the same ratio as the creature's melee and health moved from its template, so a classic caster's Fireball hits like a level-80 one, and a creature's heals keep their share of its health
- Stats are set as the stat system's base values and re-applied if anything resets the creature (respawn, another module's `SetLevel`)
- `.atlas scaling [level] [tier] [party] [difficulty]` prints the table

---

## Configuration

All settings live in `mod_atlas.conf`. Key options:

### Scaling

| Setting | Default | Description |
|---------|---------|-------------|
| `Scaling.LevelBand` | 3 | Creature level window: ±N levels from party |
| `Scaling.Role.<Role>` | see conf | Health, damage, armor per role: Trash 1/1/1, Elite 2.5/1.5/1, Rare 6/2/1, Miniboss 15/2.5/1, Boss 30/3/1, Add 0.5/0.7/1 |
| `Scaling.TierGrowth` | 1.075 | Health and damage growth per map tier above 1 |
| `Scaling.SpellScaleMax` | 20 | Highest factor creature spells are scaled up by |
| `Scaling.SoloMultiplier` | 0.5 | Party factor input for one player (relative to a party of five) |
| `Scaling.PerPlayerHealth` | 0.25 | Health added per extra party member, relative to five |
| `Scaling.PerPlayerDamage` | 0.1 | Damage added per extra party member, relative to five |

### Dungeon

| Setting | Default | Description |
|---------|---------|-------------|
| `Dungeon.BossCount` | 1 | Number of bosses per run |
| `Dungeon.EliteChance` | 20 | % chance a trash mob spawns as elite |
| `Dungeon.AggroRadius` | 15.0 | Detection range in yards |
| `Dungeon.Whitelist` | (empty) | Comma-separated map IDs to allow (empty = all) |
| `Dungeon.Blacklist` | (empty) | Comma-separated map IDs to exclude |

### Roguelike

| Setting | Default | Description |
|---------|---------|-------------|
| `Roguelike.Enable` | 1 | Master switch for Roguelike Mode |
| `Roguelike.TransitionDelay` | 30 | Seconds between floors |
| `Roguelike.HpScalingPerTier` | 0.10 | +10% enemy HP per tier |
| `Roguelike.DmgScalingPerTier` | 0.08 | +8% enemy damage per tier |
| `Roguelike.ExponentialThreshold` | 5 | Tier where scaling goes exponential |
| `Roguelike.ExponentialFactor` | 1.15 | Exponential growth factor |
| `Roguelike.AffixStartTier` | 3 | First tier with an affix |
| `Roguelike.SecondAffixTier` | 7 | Second simultaneous affix |
| `Roguelike.ThirdAffixTier` | 10 | Third simultaneous affix |

### Rewards

Gold settings are copper at level 80; a lower effective level earns its share (level 40 gets half), then the tier's reward multiplier applies.

| Setting | Default | Description |
|---------|---------|-------------|
| `Rewards.BaseGold` | 40000 | Gold per echo mended (split across the party); the Endless payout is this x depth, per player |
| `Rewards.GoldPerMob` | 800 | Added per mob killed |
| `Rewards.GoldPerBoss` | 40000 | Added per boss killed |
| `Rewards.XPMultiplier` | 1.0 | Kill XP multiplier (below max level) |
| `Rewards.ItemChance` | 80 | % chance each player gets an item when an echo is mended |
| `Rewards.RareChance` | 40 | % chance a non-epic reward is blue |
| `Rewards.EpicChance` | 15 | % chance the reward is epic |
| `Rewards.MaxItemLevel` | 219 | Item level ceiling for rewards and drops (keeps raid and arena gear out); 0 = none |

See `mod_atlas.conf.dist` for the full list with descriptions.

---

## Population and corruption

A run's creatures stand where the dungeon's own did: its stock spawns are clustered into packs (spots within `Population.PackRadius` yards on one floor, up to `Population.MaxPackSize`), nearest the entrance first, and its bosses' spots are the arenas.

Each pack is native, Scourge-touched or Scourge, by the run's tier:

| Tiers | Native | Touched | Scourge |
|---|---|---|---|
| 1-3 | 90 | 10 | 0 |
| 4-6 | 60 | 25 | 15 |
| 7-9 | 35 | 30 | 35 |
| 10-12 | 15 | 25 | 60 |
| 13-15 | 0 | 15 | 85 |
| 16+ | 0 | 0 | 100 |

- **Native** follows the timeline: "as it was" puts the dungeon's own creature back on each spot; the Scourge timeline and other pool themes draw from their pool by pack role (a leader, then melee, casters, the odd heavy and swarm); the creature-type themes pick by type as before.
- **Touched** is the same pack marked by the plague: Shroud of Death (aura 10848) and the Necrotic affix (M04).
- **Scourge** draws from the Scourge pool (theme 51, 147 creatures with their roles, `atlas_theme_creature`).
- A pack's leader is rare at `Dungeon.RareSpawnChance` (times Rare-infested), at most `Dungeon.MaxRares` per run: larger, Rare role stats, 2-3 creature affixes of its own, announced to the party.
- Bosses follow the same table: the arena's own master, the same touched, or a Scourge champion.

Themes live in the world table `atlas_theme` (module SQL `atlas_themes.sql`) beside the config's `Themes.N`; database themes use ids below 100. `atlas_creature_curation` records which creatures behave in a run: blocked ones never spawn, untested and allowed ones do. Creatures with SmartAI and no C++ script keep their own AI and spells (`Population.KeepSmartAI`); everything else, and every boss, gets Atlas's melee AI.

| Setting | Default | Description |
|---------|---------|-------------|
| `Population.PackRadius` | 8 | Spots this close (yards, same floor) form one pack |
| `Population.MaxPackSize` | 6 | Most creatures in a pack |
| `Population.KeepSmartAI` | 1 | SmartAI creatures keep their own AI |
| `Corruption.ScourgeTheme` | 51 | The Scourge pool's theme |
| `Corruption.TouchedAura` | 10848 | The mark of a touched creature (0 = none) |
| `Corruption.TouchedAffix` | M04 | Affix codes a touched creature carries |
| `Corruption.Shares` | (table above) | `native,touched,scourge;...` for corruption levels 0-5 |
| `Dungeon.MaxRares` | 2 | Most rares per run |

## Affixes

Affixes (instabilities, in the Keepers of Time's words) are data: the world table `atlas_affix` (module SQL `atlas_affixes.sql`), one row per affix with its handler, values per tier band (1-5, 6-10, 11-16), the lowest tier it rolls at, a weight, an exclusive group (two affixes of one group never roll together) and whether playerbots handle it. The Endless Timeways rolls them by depth: 1 from `Roguelike.AffixStartTier`, 2 and 3 from the second and third affix tiers. Echoes roll none; maps (PLAN 1.4) will carry their own.

| Kind | Affixes (code) |
|---|---|
| Monster damage | Brutal (M01), Searing (M02), Frostbitten (M03), Necrotic (M04), Blighted (M05), Frenzied (M08), Fleet (M09) |
| Monster defence | Fecund (M11), Armoured (M12), Warded (M13), Ice-shielded (M14) |
| On death | Bursting (M24), Desecrating (M25), Splitting (M26) |
| Players | of Grief (P01), of Frailty (P02), of Enfeeblement (P03), of Elemental Weakness (P04), of Lethargy (P06), of Drought (P08), of the Throne (P11) |
| Area | Blizzard-swept (A02), Plague-cloud (A03): every 12 or 15 s, during combat, under a random player |
| Packs | Teeming (C01), Swarming (C02), Elite (C03), Rare-infested (C04) |
| The master | Colossal (B01), Overpowering (B02), Summoner (B05) |

How they are built:
- Monster numbers are math in the module's hooks; each creature gets its share of the run's affixes when it spawns.
- Visible effects use stock spells (Fiery Weapon, Frozen Blows, Shadow Wrath, Fast Acting Poison for the extra hits; Blizzard, Plague Contamination, Desecration; Collapse for the burst; a creature Ice Barrier). Ground zones are at most 10 yards and last a few seconds, the shapes playerbots step out of.
- Player affixes use the module's own passive spells (90001-90007): the server applies them, unpatched clients see no unknown aura, and they are never saved; they come off when a player leaves the run.
- `.atlas affix list` shows them; `.atlas affix next <codes>` forces some on a character's next run, for testing.

-------|--------|
| **Fortified** | Trash mobs: +30% HP, +15% damage |
| **Tyrannical** | Bosses: +40% HP, +20% damage |
| **Raging** | All enemies: +25% damage |
| **Bolstering** | All enemies: +20% HP |
| **Savage** | Double elite spawn chance |

Affixes stack — at tier 10+ you might face Fortified + Tyrannical + Raging simultaneously.

---

## GM Commands

| Command | Access | Description |
|---------|--------|-------------|
| `.atlas status` | GM | Show module status and active session count |
| `.atlas list` | GM | List all active sessions with details |
| `.atlas end [id]` | Admin | Force-end a session (defaults to your own) |
| `.atlas clearcooldown` | GM | Clear cooldown for target's whole group |
| `.atlas reload` | Admin | Hot-reload configuration |
| `.atlas run` | GM | The target's (or your) current run: state, map, progress and the seed it was built from; for an Endless floor also the descent's seed, depth and affixes |
| `.atlas affix list` | GM | Every loaded affix with its tiers and values |
| `.atlas affix next <codes>\|clear` | GM | The target's (or your) next run carries these affixes, e.g. `M01 P02 A02` (an echo, or every depth of a descent) |
| `.atlas curate <entry> [allowed\|blocked\|untested] [note]` | GM | Shows a creature's curation, AI and pools, or sets it (stored in `atlas_creature_curation`) |
| `.atlas curate spawn <entry> [role]` | GM | Spawns a sample at your level as a run would (role stats, AI policy), to test a creature; role trash, elite (default), rare, miniboss, boss or add |
| `.atlas scaling [level] [tier] [party] [difficulty]` | GM | Health, melee and armor of every role at those settings (defaults: 80, tier 1, five, the last difficulty) |
| `.atlas seed <hex>\|clear` | GM | The target's (or your) next run, an echo or a whole Endless descent, is built from this seed. Same seed, dungeon and settings: same creatures, elites, rare and boss |

Every run is logged in `atlas_run_log` (characters DB) with its seed and result (completed, failed, abandoned, or interrupted by a restart).

---

## Technical Notes

- **AzerothCore compatibility** — Built against the official [AzerothCore](https://github.com/azerothcore/azerothcore-wotlk) repository. No core modifications required.
- **Thread safety** — Session maps and cooldowns are mutex-guarded for multi-player safety.
- **Async teleport handling** — 30-second grace period after teleports to prevent false "abandoned" detection.
- **InstanceScript neutralization** — All boss encounters are marked DONE on populate to prevent native scripts from interfering.
- **Stock instance hygiene** — Runs happen in a real dungeon instance, so the module resets temporary Normal saves before a run, releases every bind to the run's instance afterwards (AC then deletes its saved state), and records open run instances in `atlas_open_instance` so a restart mid-run releases them at startup.
- **Hot-path gating** — Damage, death and creature-add hooks check a lock-free "any run active?" flag and the map type before touching the session mutex.
- **Cached lookups** — Dungeon entrances, layouts (packs, arenas), themes, pools and leaderboards are loaded once (startup, `.atlas reload`) instead of queried during play.
- **Debuff purging** — Lingering debuffs from despawned creatures are removed before each floor.
- **Custom creature AI** — Creatures without SmartAI (and every boss) use `AtlasCreatureAI` which patrols a 5 yd radius around spawn points, actively scans for players within aggro range (with a 1-second fallback timer for grid edge cases), and hooks `JustDied` for proper loot timing. SmartAI creatures keep their own. Affix events (engage, damage taken, death) run from the unit script, whatever the AI. Bosses use the same AI (auto-attack only): their native scripted mechanics do not run.
- **Creature spell and heal scaling** — Each run creature gets a spell scale and a heal scale at spawn (how far its melee and health moved from its template). The unit script applies them to its spells, damage over time and heals; its melee is already set at spawn and passes through.
- **Multi-phase boss detection** — When a boss dies, the system waits 5 seconds and scans for new elite/boss creatures near the death location. If a phase-2 creature is detected, it is automatically promoted to boss status and the original death does not count as a kill.
- **Group Loot support** — Creature loot triggers the group's Need/Greed roll system for items above the group's loot quality threshold.
- **Standard AzerothCore** — All queries use standard AzerothCore column names. No fork-specific dependencies.
- **Reserved IDs** — creature entries 500000–500001, creature guids 8000001–8000011, npc_text 500000, spells 91000–91099 (91001 = Timeways Resonance). The world SQL only deletes rows it created; a clash fails the install loudly.

---

## File Structure

```
mod-atlas/
├── CMakeLists.txt
├── client_addon/
│   └── AtlasChronicle/               # The Chronicle window (copy to Interface/AddOns)
├── conf/
│   └── mod_atlas.conf.dist
├── data/sql/
│   ├── db-world/base/atlas_world_setup.sql
│   ├── db-world/base/atlas_affixes.sql   # Affix table and the player affixes' passive spells
│   ├── db-world/base/atlas_themes.sql    # Themes, the Scourge pool, creature curation
│   └── db-characters/base/atlas_characters_setup.sql
├── tests/                            # Unit tests (AC unit_tests target)
└── src/
    ├── AtlasConfig.cpp / .h          # Config loader
    ├── AtlasTypes.h                  # Shared data structures, chat prefixes
    ├── AtlasMgr.cpp / .h             # Run (session) manager: lifecycle, binds, deaths, update loop
    ├── AtlasPopulation.cpp           # AtlasMgr: themes, pools, curation, clearing and populating a run
    ├── AtlasScaling.cpp              # AtlasMgr: base stats, level ownership, damage scales
    ├── AtlasRewards.cpp              # AtlasMgr: reward and loot pools, run rewards, creature loot
    ├── AtlasRecords.cpp              # AtlasMgr: stats, leaderboard, run records and run log
    ├── AtlasInternal.h               # Helpers shared by the AtlasMgr files
    ├── AtlasLayout.h                 # Layout (entrance, anchors, arenas) and the LayoutProvider interface
    ├── StockLayoutProvider.cpp / .h  # Layouts of the stock dungeons echoes run in
    ├── RunRng.h                      # Seeded, reproducible RNG for run layout choices
    ├── AtlasTheme.h                  # Packs, corruption and spawn clustering (pure)
    ├── AtlasAffix.h                  # Affix data, rolling and aggregation (pure)
    ├── AtlasAffixes.cpp / .h         # Affix registry: loading, rolling, descriptions, player auras
    ├── AtlasAffixEffects.cpp         # AtlasMgr: affixes in play (shields, death effects, waves, zones)
    ├── AtlasBossSpawnQuery.h         # Boss spawn-point query
    ├── AtlasChronicle.h              # The Chronicle window's server API
    ├── RoguelikeMgr.cpp / .h         # Endless Timeways run manager
    ├── RoguelikeTypes.h              # Endless Timeways data structures
    ├── mod_atlas_loader.cpp          # Module entry point
    └── scripts/
        ├── npc_atlas_chronicler.cpp  # Chronicler Esmadormu gossip menus
        ├── atlas_allcreature_script.cpp  # Creature level ownership, add scaling, NPC hiding
        ├── atlas_allmap_script.cpp   # Map entry trigger
        ├── atlas_chronicle_script.cpp  # Chronicle addon protocol
        ├── atlas_command_script.cpp  # GM commands (.atlas)
        ├── atlas_player_script.cpp   # Player death handling
        ├── atlas_unit_script.cpp     # Damage scaling
        └── atlas_world_script.cpp    # Server lifecycle hooks
```

---

## Credits

- **[mod-dungeon-master](https://github.com/trauntrow/mod-dungeon-master)** — the module V0 is derived from, by trauntrow and its contributors. Support the original author: https://ko-fi.com/trauntrow
- **[AzerothCore](https://github.com/azerothcore/azerothcore-wotlk)** — Open-source WoW 3.3.5a server emulator

## License

AGPL 3.0 — see [LICENSE](LICENSE).
