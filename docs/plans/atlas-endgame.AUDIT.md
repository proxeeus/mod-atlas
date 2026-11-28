# Atlas: where we stand (AUDIT)

Written 2026-11-28, after the first clone runs (Echo of the Wailing Caverns, run log #7). Facts come from the
module code (mod-atlas 1e5b919), the play config and the live DB; the docs are cited only for what is planned.

## 1. The vision (user, 2026-11-28)

- Levelling, quests, dungeons and raids are the **story mode**. Atlas is the **grind mode**: for the Nth alt,
  mindless dungeon grinding with modifiers that keep it fresh, plus procedural quests and world events.
- Models: **Diablo 3 Adventure Mode** (bounties, Rifts, Greater Rifts), **Diablo IV** (Nightmare Dungeons, and
  "**Scourgetides**", the Helltide analog: phased events anywhere in the world, scaled to the zone's level,
  where the Scourge swarms the player), **Path of Exile maps**.
- At max level it **scales up indefinitely**: there is always something to push.

## 2. What exists today

| Feature | State | Vision analog |
|---|---|---|
| **Echoes** (the Chronicler's free runs) | Live. Stock instances (or clones, opt-in), level brackets 10-80 (`Atlas.Difficulty.1-6`), forces bar then the master is summoned beside the party (75% of forces, cap 150), 5 min cooldown | D3 Rift (forces bar, rift guardian), at any level |
| **Echo Sigils + Timeways Lens** | Live. Tiers 1-16 (`MAP_MAX_TIER`), normal / magic / rare, affixes rolled on the sigil, 6 portals, cache at the end, sigils drop sigils (sustain about 2 runs in 3) | PoE maps, D4 Nightmare sigils |
| **Endless Timeways** (RoguelikeMgr) | Live. Chained dungeons until a wipe, depth-scaled affixes (1 from depth 3, 2 from 7, 3 from 10), a buff per floor, vendor, leaderboard | Greater Rift (uncapped), but on its own scaling |
| **Affixes** | 30 in `atlas_affix`: 14 monster, 7 player, 4 corruption, 3 boss, 2 atmosphere | PoE map mods |
| **Scourge corruption / themes** | Live. 147-creature Scourge pool, native/touched/Scourge packs by tier, rares with 2-3 creature affixes | D4 dungeon themes, the seed of Scourgetides |
| **Role scaling** | Live. One formula for every creature, level as a parameter (BACKLOG R1) | One power curve (only partly, section 3.1) |
| **Loot ladder** | Live. Item level by tier: 200 at tiers 1-3, 213 at 6, up to 251-264 at 16; quantity and rarity from affixes | Gear progression |
| **Account progress** | Table only: first clear per (dungeon, tier) gives a bonus sigil | Atlas completion |
| **Clones** | 6 live, opt-in (`.atlas clones`): Deadmines, Stratholme, RFK, Scholomance, WC, Blackrock Spire, each with its own fog | Map catalog |
| **HUD addon** | Live (AtlasChronicle): title, master bar, foes slain, rifts, affix icons | PoE/D3 run UI |
| **Run log** | Every run with seed, party, level, result, duration, kills | Leaderboards, weekly seeds (B12) |

Not in the code at all: crafting currency, atlas passives, fragments and pinnacles, encounter modules
(Breach-like), library bosses and the mechanic library, bounties/contracts, world events (outbreaks, nemeses,
Director), a timer on any run.

## 3. Problems that matter for the vision

### 3.1 Three scaling curves, not one
- **Echoes:** role table x level bracket (all brackets 1.0).
- **Sigils:** role table x `TierGrowth` 1.075 per tier: tier 16 is only x2.96.
- **Endless:** its own curve (`Roguelike.HpScalingPerTier` 0.10 linear to depth 5, then x1.15 compounding).
None knows about the others, so "tier 10 sigil" and "Endless depth 10" mean unrelated things.

### 3.2 Tier 1 is far below a normal dungeon (measured 2026-11-28)
Stats come from the plain base stats of `creature_classlevelstats` x the role multiplier, ignoring the
template's own modifiers. At level 80 that gives:

| | Atlas tier 1 | Stock normal (Utgarde Keep) | Stock heroic |
|---|---|---|---|
| Trash health / damage | x1 / x1 | x4 / x7.5 | |
| Boss health / damage | x30 / x3 | x12.5-17 / x7.5 | x19-32 / x13 |

Bosses have heroic health but hit 2.5x softer than normal bosses; trash hits about 7x softer. Even tier 16
trash (x2.96) hits softer than stock normal-mode trash. The run log agrees: a 4K-4.5K gearscore party cleared
the WC echo in 4 min 48 s with 0 deaths.

### 3.3 Nothing is infinite yet
- Sigils stop at tier 16 at x2.96, and the gear ladder stops at ilvl 264.
- Endless is uncapped, but it has no timer and no reward ladder of its own, so pushing deeper isn't a goal
  the way a Greater Rift level is.
- Infinite scaling needs a **player power axis that keeps growing past the gear cap** (D3 paragon and legendary
  gems, D4 paragon glyphs, PoE atlas passives). Without one, an uncapped tier is just a wall.

### 3.4 Small catalog
- 6 clones, plus stock-instance echoes for the rest.
- The modifier pool (30 affixes) is the main source of variety and is still small; CRAZY_CLONES M3 targets it.

## 4. Gap per vision pillar

| Pillar | Have | Missing |
|---|---|---|
| D3 Rifts | Echoes: forces bar, guardian summon, any level | XP/levelling value not checked (do echoes beat questing for an alt?), a rift-key style reward |
| D3 Greater Rifts | Endless depth, leaderboard, run log | One curve with sigils, a timer, rewards that scale with depth, a power axis past gear |
| D3 Bounties | (nothing; Contracts shelved 2026-11-22) | Zone task boards with a cache; addon-tracked (quest text is cached by ID, BACKLOG B10) |
| D4 Nightmare dungeons | Sigils with affixes on clones | Objectives beyond "kill forces, kill master"; a glyph-like reward to level |
| D4 Helltide -> Scourgetides | Scourge pool, pack spawning, affix handlers, level-parameter scaling, light overrides (outdoors) | The event itself: zone pick, timed overlay, swarm spawner around players, event currency and chests, zone-level loot |
| PoE maps | Sigils, tiers, rarity, sustain, first-clear bonus | Crafting currency, atlas passives, encounter modules, fragments and pinnacles, far more affixes |
| Infinite at 80 | Uncapped Endless only | Section 3.3 |

## 5. Recommended order

1. **One power curve, retuned** (small: config plus one shared function). A single "difficulty level" every
   mode maps onto: echo bracket -> level 0, sigil tier N -> level N, Endless depth D -> level D. Tier 1 at about
   a stock normal dungeon (trash x4 health / x7.5 damage), compounding per level like a Greater Rift (the rate
   is a tuning choice), uncapped.
2. **Greater Rift analog:** Endless (or a "Timeways key" sigil) on that curve, with a timer, a leaderboard per
   class or party, and rewards by level.
3. **Power past the gear cap:** an account-wide paragon-like axis (Atlas passives, DESIGN section 15), fed by
   runs, giving small percent stats through custom spells. The client patch pipeline exists now, so Spell.dbc
   rows are possible.
4. **Crafting currency** (DESIGN section 11): a sink and a chase for sigils.
5. **Scourgetides** (BACKLOG B2/B6 reshaped): open-world events at the zone's level, built from what population
   and affixes already do. Every alt meets them, not only level-80 characters.
6. **Bounties**: task boards per zone, sharing Scourgetides' spawners and rewards.
7. **Encounter modules, more affixes, more clones** in parallel with all of the above (CRAZY_CLONES M2-M5).

Not recommended: generated maps (scrapped 2026-11-27, b446a35).

## 6. Open questions

- Do echoes give kill and quest XP, and how does an echo's XP per hour compare with questing? This decides
  whether echoes are an alt-levelling path.
- Should Endless become the Greater Rift, or should a new "key" sigil replace it, with Endless kept as a fun
  mode?
- Scourgetides scope: a whole-zone overlay (phaseMask; BACKLOG BQ2 says which towns and quests break) or local
  swarms around players (no phasing)?
