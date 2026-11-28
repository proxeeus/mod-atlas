# atlas-endgame: affix list

Appendix to `atlas-endgame.DESIGN.md` (section 10). First full affix list: what each affix does, how it is
built, its values per tier band, and whether bots can handle it. Values are starting points for tuning.

---

## 1. Building blocks (how an affix is implemented)

Each affix uses one **mechanism**. Cheapest first:

| Mechanism | What it is | Visible to the player | Client patch needed |
|---|---|---|---|
| `HOOK` | Pure server math in an existing hook (`ModifyMeleeDamage`, `ModifySpellDamageTaken`, `ModifyPeriodicDamageAurasTick`, `ModifyHealReceived`, stats at spawn in `OnCreatureSelectLevel`). | Only through the map tooltip and the numbers. | No |
| `AURA` | A spell with the right aura type, cast with custom base points (`CastCustomSpell`). | Aura icon and tooltip. | Only if no stock spell fits and a custom `Spell.dbc` row is used. |
| `GROUND` | A ground effect built in one of the three shapes bots dodge (section 2). | Yes. | Same as `AURA`. |
| `SPAWN` | Extra creatures, packs, objects. | Yes. | No (creatures/GOs are server-sent). |
| `EVENT` | Death, timer or HP-threshold trigger running one of the above. | Yes. | No. |

**Rule:** numeric modifiers on monsters use `HOOK` (no spell data, no aura spam, no stacking rules to fight).
Player-side modifiers use `AURA`, so players see what is hitting them. Aura types referenced below exist in
`SpellAuraDefines.h` (checked 2026-11-22); concrete spell IDs are picked in phase 1 from `Spell.dbc` (one per
aura type, base points overridden), or added as module spells in the reserved range.

## 2. Bot-safety rule for ground effects (verified in playerbots source)

`AvoidAoeAction` (`mod-playerbots/src/Ai/Base/Actions/MovementActions.cpp:1882-2090`) moves a bot out of
exactly three things, all limited to `Playerbots.MaxAoeAvoidRadius` (default **15 yd**) and skipping
`Playerbots.AoeAvoidSpellWhitelist`:

1. **Dynamic-object aura** (`DYNOBJ_AURA_TYPE`, negative spell) whose aura on the bot is periodic damage,
   periodic damage %, periodic trigger, or dummy (`AoeValues.cpp:121-146`). The bot reacts only once it is
   standing in it and has the aura.
2. **Trap gameobject** (`GAMEOBJECT_TYPE_TRAP`, `trap.type` 0, negative spell), radius = diameter / 2 +
   combat reach.
3. **Non-selectable trigger creature** with a periodic-trigger aura whose triggered spell does school damage
   within its radius.

And only when the bots' master is a real client (`AiFactory.cpp:291`, design section 16).

Consequences for affixes:
- Every `GROUND` affix uses shape 1 or 3 with radius ≤ 15 yd and damage over time, never a one-shot blast.
- **One-shot telegraphed blasts** (on-death explosions, "meteor in 3 s") are *not* dodged by bots. They are
  either weak enough to eat (`bot_safe = 1`) or `bot_safe = 0`.
- Bigger zones are built from several ≤ 15 yd pieces.

## 3. Columns

- **Group**: affixes in the same exclusive group never roll together.
- **Values**: three roll steps, one per tier band T1–5 / T6–10 / T11–16 (design section 10: one enchant row per
  step).
- **Min T**: lowest map tier where it can roll.
- **Bot**: `1` bot-safe; `0` excluded when "bot-safe only" is on; `?` needs a bot test first (treated as 0).
- **Qty**: item-quantity bonus the affix adds to the map (PoE-style; rarity bonus = half of it unless noted).

## 4. Monster affixes (prefixes)

### Damage
| ID | Name | Text | Mech. | Values | Min T | Bot | Qty | Group |
|---|---|---|---|---|---|---|---|---|
| M01 | Brutal | Monsters deal X% increased physical damage | HOOK melee | 15 / 25 / 40 | 1 | 1 | 8 | dmg_phys |
| M02 | Searing | Monsters deal X% of damage as extra Fire | HOOK melee → extra Fire hit | 10 / 20 / 30 | 1 | 1 | 8 | dmg_extra |
| M03 | Frostbitten | Monsters deal X% of damage as extra Frost | same, Frost | 10 / 20 / 30 | 1 | 1 | 8 | dmg_extra |
| M04 | Necrotic | Monsters deal X% of damage as extra Shadow | same, Shadow | 10 / 20 / 30 | 1 | 1 | 8 | dmg_extra |
| M05 | Blighted | Monsters deal X% of damage as extra Nature | same, Nature | 10 / 20 / 30 | 1 | 1 | 8 | dmg_extra |
| M06 | Empowered | Monster spells deal X% increased damage | HOOK spell | 15 / 25 / 40 | 3 | 1 | 8 | dmg_spell |
| M07 | Savage | Monsters have +X% critical strike chance and Y% increased crit damage | HOOK | 5,20 / 10,35 / 15,50 | 4 | 1 | 10 | crit |
| M08 | Frenzied | Monsters have X% increased attack and casting speed | AURA 192 + 216 (or HOOK on swing timers) | 10 / 18 / 25 | 2 | 1 | 10 | speed |
| M09 | Fleet | Monsters have X% increased movement speed | AURA 31 | 15 / 25 / 35 | 1 | 1 | 5 | move |
| M10 | Enraging | Monsters enrage at 30% life: +X% damage, +20% attack speed | EVENT HP → AURA 79 + 192 | 25 / 40 / 60 | 5 | 1 | 8 | enrage |

### Defence
| ID | Name | Text | Mech. | Values | Min T | Bot | Qty | Group |
|---|---|---|---|---|---|---|---|---|
| M11 | Fecund | Monsters have X% more life | HOOK spawn | 25 / 45 / 70 | 1 | 1 | 10 | life |
| M12 | Armoured | Monsters take X% reduced physical damage | HOOK | 10 / 18 / 25 | 2 | 1 | 8 | def_phys |
| M13 | Warded | Monsters take X% reduced spell damage | HOOK | 10 / 18 / 25 | 2 | 1 | 8 | def_spell |
| M14 | Ice-shielded | Monsters gain an absorb shield of X% of max life when combat starts | AURA 69 | 10 / 20 / 30 | 4 | 1 | 8 | shield |
| M15 | Regenerating | Monsters regenerate X% of life per second | HOOK periodic heal (map tick) | 0.5 / 1 / 1.5 | 3 | 1 | 8 | regen |
| M16 | Vampiric | Monsters heal for X% of damage dealt | HOOK damage dealt → heal | 10 / 20 / 30 | 4 | 1 | 8 | regen |
| M17 | Unwavering | Monsters cannot be stunned, feared or polymorphed | AURA 77 (mechanic immunity) | – | 6 | 1 | 6 | cc |
| M18 | Unyielding | Monsters have X% reduced duration of control effects | AURA 117 / HOOK | 30 / 50 / 70 | 3 | 1 | 4 | cc |
| M19 | Bulwark | Rare monsters and bosses take X% less damage while above 50% life | HOOK | 15 / 25 / 35 | 7 | 1 | 8 | — |

### Retaliation
| ID | Name | Text | Mech. | Values | Min T | Bot | Qty | Group |
|---|---|---|---|---|---|---|---|---|
| M20 | Thorned | Monsters deal X Shadow damage to melee attackers | AURA 15 (damage shield) | 150 / 300 / 500 | 3 | 1 | 6 | reflect |
| M21 | Mirrored | Monsters reflect X% of spell damage taken | HOOK | 8 / 12 / 18 | 8 | 0 | 15 | reflect |
| M22 | Blood-reflecting | Monsters reflect X% of physical damage taken | HOOK | 8 / 12 / 18 | 8 | 0 | 15 | reflect |
| M23 | Spiteful | Monsters cast a short-lived curse on their killer: −X% damage for 6 s | EVENT death → AURA 79 | 10 / 15 / 20 | 6 | 1 | 6 | — |

### On death
| ID | Name | Text | Mech. | Values | Min T | Bot | Qty | Group |
|---|---|---|---|---|---|---|---|---|
| M24 | Bursting | Monsters explode on death for X% of their life as Shadow damage (5 yd) | EVENT death, 1.5 s delay | 3 / 5 / 8 | 5 | 1 (weak by design) | 10 | death |
| M25 | Desecrating | Monsters leave a pool of Death and Decay on death for 6 s | GROUND shape 1 (8 yd) | dmg scaled | 4 | 1 | 10 | death |
| M26 | Splitting | Abominations and heavies split into 2–3 maggots on death | SPAWN (16056/16057) | 2 / 2 / 3 | 3 | 1 | 10 | death |
| M27 | Risen | X% of slain monsters rise again as a ghoul after 4 s | SPAWN | 15 / 25 / 35 | 6 | 1 | 12 | death |
| M28 | Empowering | Slain monsters grant nearby allies +X% damage (stacks) | EVENT death → AURA 79 | 5 / 8 / 12 | 7 | 1 | 10 | death |

### Packs and leaders
| ID | Name | Text | Mech. | Values | Min T | Bot | Qty | Group |
|---|---|---|---|---|---|---|---|---|
| M29 | Commanded | Pack leaders grant their pack X% reduced damage taken while alive | AURA (area aura on leader) 87 | 15 / 25 / 35 | 5 | ? (needs kill-priority, Q10) | 10 | leader |
| M30 | Necromantic | Pack leaders raise dead pack members every 10 s | SPAWN | 1 / 1 / 2 | 7 | ? (Q10) | 12 | leader |
| M31 | Plague-bearing | Monster hits apply a stacking disease: −X% healing received per stack (max 10) | AURA 118 negative | 2 / 3 / 4 | 5 | 1 | 10 | plague |
| M32 | Chilling | Monster hits reduce attack and cast speed by X% for 4 s | AURA 192/216 negative | 10 / 15 / 20 | 4 | 1 | 8 | plague |
| M33 | Lurking | X% of packs start stealthed and ambush | AURA stealth | 20 / 30 / 40 | 4 | 1 | 6 | — |

## 5. Player affixes (suffixes, "of …")

All are `AURA` on players on entering the run (stripped on leave/logout, design section 19), so they show as
debuffs. The run aura is a dummy-carrier spell with up to 3 effects.

| ID | Name | Text | Aura type | Values | Min T | Bot | Qty | Group |
|---|---|---|---|---|---|---|---|---|
| P01 | of Grief | Players have X% reduced healing received | 118 (MOD_HEALING_PCT) | 15 / 30 / 45 | 2 | 1 | 10 | heal |
| P02 | of Frailty | Players take X% increased damage | 87 (MOD_DAMAGE_PERCENT_TAKEN) | 8 / 15 / 22 | 3 | 1 | 10 | taken |
| P03 | of Enfeeblement | Players deal X% less damage | 79 (MOD_DAMAGE_PERCENT_DONE) negative | 8 / 15 / 22 | 3 | 1 | 10 | done |
| P04 | of Elemental Weakness | Players have −X resistance to all schools | 101 / 143 | 50 / 100 / 150 | 4 | 1 | 8 | resist |
| P05 | of Exposure | Monsters have +X% crit chance against players | 187 + 179 (attacker crit) | 5 / 8 / 12 | 5 | 1 | 10 | crit_taken |
| P06 | of Lethargy | Players have X% reduced movement speed | 33 | 10 / 15 / 20 | 2 | 1 | 6 | move_p |
| P07 | of Temporal Chains | Players have X% reduced attack and casting speed | 192 / 216 negative | 8 / 12 / 18 | 6 | 1 | 12 | speed_p |
| P08 | of Drought | Players have X% reduced mana regeneration | 110 negative | 20 / 35 / 50 | 4 | 1 (bot healers drink after combat) | 10 | mana |
| P09 | of Ceaseless Cold | Players cannot regenerate life in combat | 116 / HOOK | – | 8 | 1 | 10 | regen_p |
| P10 | of Starvation | Players and their pets cannot regenerate mana or life at all | 294 + HOOK | – | 11 | 0 (bots drink forever) | 20 | regen_p |
| P11 | of the Throne | Players have −X% dodge and parry (Chill of the Throne) | 49 / 47 negative | 10 / 15 / 20 | 5 | 1 | 8 | avoid |
| P12 | of Sapping | Potions, healthstones and bandages are X% less effective | HOOK heal by spell family | 30 / 50 / 70 | 4 | 1 | 6 | — |
| P13 | of Vulnerability | Players' absorb shields are X% weaker | 284? / HOOK on absorb | 20 / 35 / 50 | 7 | 1 | 8 | — |
| P14 | of Brittle Bones | Players take X% increased damage from critical strikes | HOOK | 15 / 25 / 40 | 8 | 1 | 10 | crit_taken |

## 6. Area and environment affixes

| ID | Name | Text | Mech. | Values | Min T | Bot | Qty | Group |
|---|---|---|---|---|---|---|---|---|
| A01 | Defiled Ground | Patches of unholy ground heal monsters and harm players | GROUND shape 3 (trigger NPC) at anchors | 3–6 patches | 3 | 1 | 8 | ground |
| A02 | Blizzard-swept | Every 12 s a Blizzard falls on a random player | GROUND shape 1 (≤ 15 yd) | dmg scaled | 4 | 1 | 10 | storm |
| A03 | Plague-cloud | Every 15 s a plague cloud forms under a random player | GROUND shape 1 | dmg scaled | 4 | 1 | 10 | storm |
| A04 | Shadow-fissured | Shadow fissures open under players during combat | GROUND shape 3 (Kel'Thuzad-style trigger) | dmg scaled | 7 | 1 | 12 | storm |
| A05 | Bone-strewn | Bone spikes impale a random player, who must be freed | SPAWN (spike = add) | every 20 s | 8 | ? (Q10) | 12 | — |
| A06 | Haunted | Vengeful spirits spawn near players who stand still for 6 s | SPAWN | 1 / 2 / 3 | 5 | ? (bots stand still while casting) | 10 | — |
| A07 | Frozen | All water and ground is slippery: players have −X% movement speed out of combat | AURA 33 | 15 / 20 / 25 | 1 | 1 | 4 | move_p |
| A08 | Shrouded | Reduced view distance (fog) | client: Light override or a screen-effect spell | – | 6 | 1 | 6 | — |
| A09 | Saronite Veins | Saronite deposits pulse damage nearby unless destroyed | SPAWN destructible + GROUND shape 3 | 3–5 | 9 | ? (Q10) | 12 | — |
| A10 | Unhallowed | Monsters slain on unholy ground rise again (combines with A01) | EVENT | – | 10 | 1 | 10 | — |

## 7. Pack composition and loot

| ID | Name | Text | Handler | Values | Min T | Bot | Qty |
|---|---|---|---|---|---|---|---|
| C01 | Teeming | X% increased pack size | PACK_COMPOSITION | 20 / 35 / 50 | 1 | 1 | 15 |
| C02 | Swarming | Packs contain X% more swarm monsters | PACK_COMPOSITION | 30 / 50 / 80 | 2 | 1 | 10 |
| C03 | Elite | X% of swarm monsters become elite | PACK_COMPOSITION | 15 / 25 / 40 | 4 | 1 | 10 |
| C04 | Rare-infested | X% increased chance for rare packs | PACK_COMPOSITION | 25 / 50 / 100 | 2 | 1 | 8 (rarity +20) |
| C05 | Magic packs | Every pack leader has one rare modifier | PACK_COMPOSITION | – | 5 | 1 | 10 |
| C06 | Overrun | The map is fully Scourge (corruption set to 5) | PACK_COMPOSITION | – | 6 | 1 | 10 |
| C07 | Cult-infested | Extra packs of the Cult of the Damned | PACK_COMPOSITION | +2 / +3 / +4 | 3 | 1 | 6 |
| C08 | Nerubian-infested | Extra Nerubian packs from burrows | PACK_COMPOSITION | +2 / +3 / +4 | 3 | 1 | 6 |
| C09 | Patrolled | +X roaming patrols | PACK_COMPOSITION | 1 / 2 / 3 | 2 | 1 | 4 |
| C10 | Abundant | X% increased item quantity (corrupted / crafted only) | LOOT | 10 / 15 / 20 | 1 | 1 | – |
| C11 | Gilded | X% increased item rarity (corrupted / crafted only) | LOOT | 20 / 30 / 40 | 1 | 1 | – |
| C12 | Cartographer's | X% increased map drop chance | LOOT | 10 / 20 / 30 | 4 | 1 | – |

## 8. Boss affixes

| ID | Name | Text | Handler | Values | Min T | Bot | Qty | Group |
|---|---|---|---|---|---|---|---|---|
| B01 | Colossal | Map boss has X% more life | BOSS_MOD | 20 / 35 / 50 | 1 | 1 | 6 | boss_life |
| B02 | Overpowering | Map boss deals X% more damage | BOSS_MOD | 15 / 25 / 35 | 2 | 1 | 6 | boss_dmg |
| B03 | Empowered | Map boss gains X extra library mechanics (only `bot_coverage = existing` with bots) | BOSS_MOD | 1 / 1 / 2 | 4 | 1 | 10 | boss_mech |
| B04 | Twinned | Map boss is accompanied by a second boss | BOSS_MOD | – | 9 | 1 | 20 | boss_twin |
| B05 | Summoner | Map boss calls waves of adds at 75/50/25% life | BOSS_MOD | 3 / 4 / 6 adds | 3 | 1 | 8 | boss_adds |
| B06 | Relentless | Map boss hard-enrages after X minutes | BOSS_MOD | 6 / 5 / 4 | 6 | 1 | 6 | boss_enrage |
| B07 | Lieutenant's | A Scourge lieutenant (library boss) guards the boss arena | SPAWN | – | 10 | 1 | 10 | boss_twin |
| B08 | Unflinching | Map boss can't be taunted after the first 10 s | BOSS_MOD | – | 12 | 0 (bot tanks rely on taunt) | 10 | — |

## 9. Encounter affixes (one per map; design section 12)

| ID | Name | Adds module | Min T | Bot | Qty |
|---|---|---|---|---|---|
| E01 | Rift-torn | Scourge Rift | 2 | 1 | 10 |
| E02 | Titan-touched | Titan Cache | 1 | 1 | 6 |
| E03 | Burrowed | Nerubian Burrow | 4 | 1 | 10 |
| E04 | Whispering | Whispers of the Old God | 8 | ? | 15 |
| E05 | Crusader's | Argent Trial | 5 | 1 | 10 |
| E06 | Imprisoned | Bound Elemental | 3 | 1 | 6 |

Group `encounter`: at most one rolled; Atlas Compass (currency) can add a second.

## 10. Corrupted implicits (Saronite Corruptor)

One of these is added on corruption, in addition to the normal affixes. Group `implicit`.

| ID | Name | Text | Bot |
|---|---|---|---|
| K01 | Saronite-veined | +1 affix slot; the map can't be modified further | 1 |
| K02 | Throne-gazed | The map boss drops one additional item | 1 |
| K03 | Lich-touched | Monsters have +1 random monster affix from the top tier band | 1 |
| K04 | Frost-bound | Players have −15% movement speed, +25% item quantity | 1 |
| K05 | Plague-ridden | All `plague` group affixes are at their top value, +30% item rarity | 1 |
| K06 | Fractured | The map holds 1 extra encounter module | 1 |
| K07 | Reaving | Rare monsters drop an extra map | 1 |
| K08 | Hollow | The map has no boss; pack size +50% | 1 |

## 11. Rarity, counts and totals

- Magic: 1 prefix and/or 1 suffix. Rare: up to 3 prefixes and 3 suffixes. Corrupted: up to 8 + 1 implicit.
  "Prefix" = monster, area, pack, boss affixes; "suffix" = player affixes.
- At most one affix per exclusive group.
- With bots in the group (default "bot-safe only"): `Bot = 0` and `?` affixes don't roll; a crafted map that
  already has them shows a warning at the device. Opt-in: they roll, with +50% of their quantity bonus.

Total in this list: 33 monster, 14 player, 10 area, 12 composition/loot, 8 boss, 6 encounter, 8 implicit = 91.
Of those, 81 are bot-safe now, 6 wait on bot tests (mostly Q10 kill priority), 4 are unsafe for bots
(M21, M22, P10, B08).

**Phase-1 subset (~30, design roadmap):** M01–M05, M08, M09, M11–M14, M24–M26, P01–P04, P06, P08, P11, A02,
A03, C01–C04, B01, B02, B05. All `HOOK`, simple `AURA` or ground shape 1, all bot-safe.

## 12. Open points

- Pick one stock spell per aura type (or add module spells) and record the IDs in `atlas_affix.spell_id`.
- Damage numbers for `GROUND` and `EVENT` affixes come from the level-80 role table (design section 9), not
  fixed values.
- M02–M05 "extra damage as X": implement as a second, separate hit of the other school from the melee hook,
  so resistances and absorbs apply naturally. Check hook re-entry rules (a hook-triggered `DealDamage` must not
  re-trigger the same hook).
- P13 aura type 284 (`SPELL_AURA_LINKED`) is not right for absorb weakening; likely a `HOOK` on absorb
  calculation. Check which hook sees absorbs before committing to P13.
- A08 needs a client-side solution (per-map Light rows are global, not per instance). Possibly a screen-effect
  spell; otherwise drop it.
