# Atlas: one difficulty curve (CURVE)

Written 2026-11-28 from AUDIT §3.1-3.3. Plan only; no code yet. The user asked: "Plan the curves".

## 1. Goal

One number, the **tier**, means the same difficulty in every mode, and it has no top:

| Mode | Tier comes from |
|---|---|
| Echoes (Chronicler, any level) | the difficulty the party picks (section 4) |
| Echo Sigils (maps) | the sigil's tier, 1-16 |
| Infinite Rifts (the Greater Rift mode, RIFTS.md to come) | the key's level, uncapped |
| Endless Timeways | start tier + floors cleared |

Today these are three unrelated curves (AUDIT §3.1). After this plan, "tier 10" is the same fight whichever
door you came through; only the rules around it (timer, portals, floors) and the rewards differ.

## 2. Reference points (stock creature_template, read 2026-11-28)

Stock modifiers multiply the same `creature_classlevelstats` base Atlas uses (level 80: 12,600 health for a
warrior-class creature).

| Stock content | Elite trash health / damage | Boss health / damage |
|---|---|---|
| Normal Utgarde Keep | x4 / x7.5 | x12.5-17 / x7.5 |
| Heroic Utgarde Keep | x5 / x13 | x19-32 / x13 |
| Normal Forge of Souls / Pit of Saron | x6-6.5 / x7.5 | x37-48 / x7.5 |
| Heroic Forge of Souls / Pit of Saron | x10.5 / x13 | x48-67 / x13 |
| **Atlas tier 1 today** | **x2.5 / x1.5** (role Elite) | **x30 / x3** |

Note: in a dungeon nearly every creature is rank 1, so it gets the **Elite** role (AtlasPopulation.cpp:714);
the Trash role is for rank 0 creatures only. The WC echo was mostly Elite-role mobs at x2.5 / x1.5.

## 3. The curve

```
health = base(level, class) x role health x H^(tier - 1) x party x affixes
damage = base(level, class) x role damage x D^(tier - 1) x party x affixes
```

- **Tier 1 = a stock normal WotLK dungeon.** The role table is retuned to the Utgarde Keep row:

  | Role | Health (today) | Damage (today) |
  |---|---|---|
  | Trash (rank 0) | 1.5 (1.0) | 2.0 (1.0) |
  | Elite | 4.0 (2.5) | 7.5 (1.5) |
  | Rare | 10 (6) | 9 (2) |
  | Miniboss | 12 (15) | 7.5 (2.5) |
  | Boss | 20 (30) | 7.5 (3) |
  | Add | 1.0 (0.5) | 3.0 (0.7) |

  Boss health goes down from 30 to 20 because echo masters have no mechanics yet (melee-only AI). A boss at
  x30 is a long melee fight with nothing to do.
- **Separate growth for health and damage**, compounding: `H = 1.08`, `D = 1.06` per tier (config
  `Atlas.Curve.HealthGrowth`, `Atlas.Curve.DamageGrowth`; they replace `Atlas.Scaling.TierGrowth` and the
  three `Atlas.Roguelike.*ScalingPerTier` keys). Damage grows slower because a death in a party with bots
  costs much more than a longer fight.

| Tier | Health x | Damage x | About like |
|---|---|---|---|
| 1 | 1.0 | 1.0 | normal Utgarde Keep |
| 5 | 1.36 | 1.26 | normal Forge of Souls trash |
| 10 | 2.0 | 1.69 | heroic ICC 5-man trash |
| 16 | 3.2 | 2.4 | top sigil: past every stock 5-man |
| 25 | 6.3 | 4.0 | Infinite Rifts territory |
| 50 | 43 | 17 | needs power past the gear cap |
| 100 | 2,000 | 320 | |

  The gear ladder ends at ilvl 264 (tier 16), so past about tier 20-25 only power from outside gear moves
  players up (AUDIT §3.3: the paragon-like axis). That's intended: the wall moves when the account grows.
- **Health overflow:** `MaxHealth` is uint32. A boss at 12,600 x 20 x 1.08^(t-1) overflows near tier 126,
  and affixes and party factors bring that lower. Clamp the computed health (float, then `min(UINT32_MAX / 2)`)
  and test the clamp. D3 also stops at 150.
- **Spells:** `SpellScale` already follows the damage multiplier; `Atlas.Scaling.SpellScaleMax` (20) caps it,
  which would flatten spells from about tier 40. Raise the cap or derive it from the curve.

## 4. Each mode on the curve

- **Echo Sigils:** tier = the sigil's tier, as today. Rarity adds affixes, not tiers. Loot ladder unchanged
  (`atlas_loot_tier`, clamped at 16).
- **Echoes:** the Chronicler offers a difficulty, D3-style, on top of the party's level:
  Normal (tier 1), Hard (3), Expert (6), Master (10), plus higher picks unlocked by the account's best sigil
  clears. The rewards scale with the pick. Below level 80 the base stats come from the party's level (R1), so a
  level-30 alt on Expert faces Expert relative to a level-30 dungeon. The level brackets
  (`Atlas.Difficulty.*`) stay as level ranges only; their multipliers stay 1.0.
- **Endless Timeways:** tier = start tier + floors cleared. Its own scaling functions
  (`RoguelikeMgr::GetTier*Multiplier`) go away. Its +10% all-stats buff per floor (up to 20 stacks, +200%)
  works against the curve: replace it by run boons later (BACKLOG B1.4), or shrink it to a few percent. That
  decision goes with the Endless vs Rifts split.
- **Infinite Rifts:** tier = key level; RIFTS.md.

## 5. Code changes (when approved)

1. `AtlasScalingMath.h`: `CurveMultiplier(growth, tier)` (replaces `TierMultiplier`), the new role defaults,
   a health clamp. Unit tests: the table in section 3, the clamp, tier 0 and 1.
2. `AtlasScaling.cpp`: `SessionHealthFactor` / `SessionDamageFactor` use the curve for every mode; the
   RoguelikeRunId branch goes away.
3. `AtlasConfig`: `Atlas.Curve.*`; old keys read once with a deprecation log line, then dropped.
4. `RoguelikeMgr`: drop the three tier multipliers; `CurrentTier` already holds start + depth.
5. Echo difficulty picker in the Chronicler's gossip; the session's tier comes from it (it's 1 today).
6. `.atlas scaling` prints the curve for any tier (it already takes a tier argument).
7. `mod_atlas.conf.dist`: the new keys and role table.

No SQL, no client patch, no core edit.

## 6. Tuning session (with the user, after the build)

Your party (4K-4.5K gearscore, bots) runs the same clone at tiers 1, 6, 10 and 14. The run log already holds
duration, deaths and kills per run. Targets:

| Tier | Target for that party |
|---|---|
| 1 | Quick, no deaths (a normal dungeon) |
| 6 | Steady, careful pulls |
| 10 | Hard: deaths possible, a full clear around 15-20 min |
| 14 | Too hard without better gear |

Tune `H`, `D` and the Elite/Boss rows from those runs, nothing else.

## 7. Open decisions

- C1: start at H 1.08 / D 1.06, or steeper (D3's Greater Rifts climb faster per level)?
- C2: Echo difficulty picks: which unlocks (best sigil tier cleared per account?).
- C3: Endless buff: remove, shrink, or replace by boons.
