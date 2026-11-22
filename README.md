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
- **9 timelines (creature themes)** — beasts, dragonkin, the Legion, elementals, giants, the Scourge, mortal warbands, rogue machines, or a shattered mix
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
- **GM commands** — `.atlas reload`, `.atlas status`, `.atlas list`, `.atlas end`, `.atlas clearcooldown`

### The Endless Timeways (roguelike mode)
- **Infinite progression** — Mend an echo, get pulled into the next one, repeat until the party falls
- **Escalating difficulty** — Enemy HP and damage scale per tier (linear, then exponential)
- **Timeways Resonance** — +10% all stats per echo mended (the module's own server-side passive aura, spell 91001; no class spell is modified; not shown as a buff icon, the total is announced in chat)
- **Mythic+ style affixes** — Fortified, Tyrannical, Raging, Bolstering, Savage; layered at configurable tier thresholds
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
- Stats are recalculated from `creature_classlevelstats` for the target level

---

## Configuration

All settings live in `mod_atlas.conf`. Key options:

### Scaling

| Setting | Default | Description |
|---------|---------|-------------|
| `Scaling.LevelBand` | 3 | Creature level window: ±N levels from party |
| `Scaling.SoloMultiplier` | 0.5 | HP/damage reduction for solo players |
| `Scaling.PerPlayerHealth` | 0.25 | HP added per extra party member (25%) |
| `Scaling.BossHealthMult` | 8.0 | Boss HP multiplier (on top of party scaling) |
| `Scaling.BossDamageMult` | 1.5 | Boss damage multiplier (party scaling only, not stacked with tier) |
| `Scaling.EliteHealthMult` | 2.0 | Elite trash HP multiplier |

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

| Setting | Default | Description |
|---------|---------|-------------|
| `Rewards.BaseGold` | 50000 | Completion gold reward (copper) |
| `Rewards.XPMultiplier` | 1.0 | Kill XP multiplier |
| `Rewards.ItemChance` | 80 | % chance of item reward on completion |
| `Rewards.RareChance` | 40 | % chance reward is rare quality |
| `Rewards.EpicChance` | 15 | % chance reward is epic quality |

See `mod_atlas.conf.dist` for the full list with descriptions.

---

## Roguelike Affixes

Affixes are Mythic+ style modifiers applied at higher tiers:

| Affix | Effect |
|-------|--------|
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

---

## Technical Notes

- **AzerothCore compatibility** — Built against the official [AzerothCore](https://github.com/azerothcore/azerothcore-wotlk) repository. No core modifications required.
- **Thread safety** — Session maps and cooldowns are mutex-guarded for multi-player safety.
- **Async teleport handling** — 30-second grace period after teleports to prevent false "abandoned" detection.
- **InstanceScript neutralization** — All boss encounters are marked DONE on populate to prevent native scripts from interfering.
- **Stock instance hygiene** — Runs happen in a real dungeon instance, so the module resets temporary Normal saves before a run, releases every bind to the run's instance afterwards (AC then deletes its saved state), and records open run instances in `atlas_open_instance` so a restart mid-run releases them at startup.
- **Hot-path gating** — Damage, death and creature-add hooks check a lock-free "any run active?" flag and the map type before touching the session mutex.
- **Cached lookups** — Dungeon entrances, spawn points and leaderboards are loaded once (startup, `.atlas reload`) instead of queried during play.
- **Debuff purging** — Lingering debuffs from despawned creatures are removed before each floor.
- **Custom creature AI** — Trash creatures use `AtlasCreatureAI` which patrols a 5 yd radius around spawn points, actively scans for players within aggro range (with a 1-second fallback timer for grid edge cases), and hooks `JustDied` for proper loot timing. Bosses use the same AI (auto-attack only): their native scripted mechanics do not run.
- **Boss spell damage scaling** — Boss abilities have hard-coded damage values designed for their original level range. The unit script intercepts all incoming damage from session bosses (spells, periodic ticks, and melee) and scales it using `creature_classlevelstats` base damage ratios between the boss's template level and the session's effective level. This ensures a level-70 boss spell deals proportionally correct damage to a level-25 party.
- **Multi-phase boss detection** — When a boss dies, the system waits 5 seconds and scans for new elite/boss creatures near the death location. If a phase-2 creature is detected, it is automatically promoted to boss status and the original death does not count as a kill.
- **Group Loot support** — Creature loot triggers the group's Need/Greed roll system for items above the group's loot quality threshold.
- **Boss damage scaling** — Boss damage uses only party-size scaling (not stacked with the difficulty tier's DamageMultiplier) to prevent excessive damage when combined with level scaling.
- **Standard AzerothCore** — All queries use standard AzerothCore column names. No fork-specific dependencies.
- **Reserved IDs** — creature entries 500000–500001, creature guids 8000001–8000011, npc_text 500000, spells 91000–91099 (91001 = Timeways Resonance). The world SQL only deletes rows it created; a clash fails the install loudly.

---

## File Structure

```
mod-atlas/
├── CMakeLists.txt
├── conf/
│   └── mod_atlas.conf.dist
├── data/sql/
│   ├── db-world/base/atlas_world_setup.sql
│   └── db-characters/base/atlas_characters_setup.sql
└── src/
    ├── AtlasConfig.cpp / .h          # Config loader
    ├── AtlasTypes.h                  # Shared data structures, chat prefixes
    ├── AtlasMgr.cpp / .h             # Core run (session) manager
    ├── AtlasBossSpawnQuery.h         # Boss spawn-point query
    ├── RoguelikeMgr.cpp / .h         # Endless Timeways run manager
    ├── RoguelikeTypes.h              # Endless Timeways data structures
    ├── mod_atlas_loader.cpp          # Module entry point
    └── scripts/
        ├── npc_atlas_chronicler.cpp  # Chronicler Esmadormu gossip menus
        ├── atlas_allcreature_script.cpp  # Creature level ownership, add scaling, NPC hiding
        ├── atlas_allmap_script.cpp   # Map entry trigger
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
