# atlas-endgame: Scourge creature pool

Appendix to `atlas-endgame.DESIGN.md` (section 8, "Scourge corruption"). The first curated theme: it is used
on every map as corruption rises, so it is built before any native theme.

**Source:** static parse of `data/sql/base/db_world` (creature_template, creature, creature_template_spell,
smart_scripts, factiontemplate_dbc), 2026-11-22, scripts in `tools/`. No database or server was used.
`tools/pool.csv` holds the full 647-row candidate list with columns this page leaves out.

---

## 1. How the candidates were found

1. Faction template whose `Faction` is 20 (Scourge), or creature type 6 (undead) with a hostile faction.
2. Spawned at least once in a Scourge-native instance (Naxxramas, ICC, Forge of Souls, Pit of Saron, Halls of
   Reflection, Drak'Tharon Keep, Azjol-Nerub, Ahn'kahet, Culling of Stratholme, Utgarde Keep/Pinnacle,
   Stratholme, Scholomance, Razorfen Downs) or in Northrend (map 571).
3. Then a second pass over every hostile spawn in those instances, whatever the faction, because the Cult of
   the Damned and many Naxx trash use faction 312/16 (Monster), not Scourge.
4. Dropped: heroic difficulty entries (stats come from the module anyway), vendors/NPC flags, non-selectable or
   PC/NPC-immune units, triggers (`flags_extra` 0x80), vehicles, names like trigger/bunny/credit/stalker.

Level ≥ 70 except the classic Scourge-native maps (Stratholme, Scholomance, RFD), which are kept as **native
themes** for those maps, not for the shared pool.

## 2. Verified behaviour that shapes the pool

**Scripted trash on a clone fights with default AI, it doesn't crash.** All C++ trash scripts in the pool
(ICC, Pit of Saron, Scholomance) create their AI through the instance factory, e.g.
`GetIcecrownCitadelAI<…>(creature)`. That factory returns `nullptr` when the instance's script name differs
(`CreatureAIImpl.h:66-68`, `GetInstanceAI` → `InstanceHasScript`), and `SelectAI` then falls back to the
`AIName` (SmartAI) or the default AI by permit (`CreatureAISelector.cpp:85-88`). On a clone the instance runs
the module's script, so these entries become plain melee: harmless, but they lose their abilities.

Consequences:
- SmartAI entries keep their spells on clones (SmartAI is keyed by entry, not by map). They are the core.
- Factory-scripted entries are "safe but dumb". Two ways to use them, decided per entry during curation:
  - spawn them as-is in the `trash` role (no abilities), or
  - **shadow entries** (below) with the module's generic AI and their spell list.
- C++ scripts registered without the instance factory (plain `CreatureScript`/`RegisterCreatureAI`) do run on
  clones. If they call `instance->…` they talk to the module's InstanceScript, which returns 0 / empty GUIDs
  for unknown data; a script that dereferences `GetCreature(…)` without a null check would crash. These are
  the real curation risks. In the pool below only open-world entries have such scripts (Boneguard Footman,
  Wretched Ghoul, Mindless Ghoul, quest attackers); they are marked and left out of phase 1.
- SmartAI actions that set instance data (`SET_INST_DATA`) reach the module's InstanceScript. It must ignore
  unknown data IDs (rule for the module's InstanceScript, design section 7).

### Shadow entries (new, proposed)
A **shadow entry** is a module-owned copy of a stock `creature_template` row in the module's creature range
(design section 19): same model, name, faction, type, flags; `ScriptName` set to the module's generic AI
(`npc_atlas_generic`); stats left to the scaling code. A tool generates them from the curated list, copying
`creature_template_model`, `creature_template_spell` and, for SmartAI entries that need fixing, their
`smart_scripts` rows under the new entry.

Why: the module fully controls their AI (no stock script, no instance factory, no map-keyed surprises); the
generic AI can read `creature_template_spell` and the mechanic library, so ICC-style trash keeps an identity;
stock rows stay untouched (no core data edits). The client needs nothing: creature templates are sent by the
server on query. Cost: rows in the module range, regenerated on purpose only (same freezing rule as layouts).

Default: SmartAI entries are spawned as stock entries; factory-scripted ones as shadow entries.

## 3. Archetypes and pack roles

Every pool entry has an archetype (look and behaviour) and one or more **pack roles**:

| Pack role | Meaning | Default share of a pack budget |
|---|---|---|
| `leader` | Elite caster or commander; the rare candidate. | 1 per pack |
| `heavy` | Abomination / construct / giant: slow, high HP share. | 0–2 |
| `melee` | Standard elite melee. | most of the rest |
| `caster` | Ranged elite. | 0–2 |
| `swarm` | Non-elite fodder, spawned in groups of 3–6 (`trash` scaling role). | fills budget |
| `add` | Spawned by mechanics (on-death split, raise dead, rifts). | not in packs |

Scaling roles (design section 9) follow from rank: rank 0 → `trash`, rank 1 → `elite`, leaders that roll rare
→ `rare`. Pack roles only choose *who* goes into a pack.

Status column: `S` = SmartAI (keeps abilities on a clone), `P` = plain (no script, melee only),
`F` = factory-scripted C++ (falls back to default AI on a clone → shadow entry recommended). All start as
`untested` in `atlas_creature_curation`; the harness moves them to `allowed` or `blocked`.

## 4. Core pool (instance trash, level 80-ish)

### Ghouls and geists (melee, swarm)
| Entry | Name | Source | Rank | Pack role | Status |
|---|---|---|---|---|---|
| 27737 | Risen Zombie | CoS | 0 | swarm | P |
| 26536 | Mindless Servant | UP | 0 | swarm | S |
| 30287 | Plundering Geist | AK | 0 | swarm | S |
| 28419 | Frenzied Geist | UK | 0 | swarm | S |
| 24084 | Tunneling Ghoul | UK | 0 | swarm | S |
| 16447 | Plagued Ghoul | Naxx | 1 | melee | S |
| 16244 | Infectious Ghoul | Naxx | 1 | melee | S |
| 16154 | Risen Squire | Naxx | 1 | melee | S |
| 27729 | Enraging Ghoul | CoS | 1 | melee | S |
| 28249 | Devouring Ghoul | CoS | 1 | melee | S |
| 37711 | Hungering Ghoul | PoS | 1 | melee | S |
| 26621 | Ghoul Tormentor | DTK | 1 | melee | S |
| 27871 | Flesheating Ghoul | DTK | 1 | melee | S |
| 30083 | Marauding Geist | Naxx | 1 | melee | S |
| 36886 | Geist Ambusher | PoS | 1 | melee (ambush) | S |

### Risen Drakkari (Scourge-touched trolls, DTK)
| Entry | Name | Rank | Pack role | Status |
|---|---|---|---|---|
| 26635 | Risen Drakkari Warrior | 1 | melee | S |
| 26830 | Risen Drakkari Death Knight | 1 | leader | S |
| 26636 | Risen Drakkari Soulmage | 1 | caster | S |
| 26637 | Risen Drakkari Handler | 1 | melee | S |
| 26638 | Risen Drakkari Bat Rider | 1 | melee | S |
| 26626 | Scourge Reanimator | 1 | leader (raises dead) | S |

### Abominations, constructs, oozes (heavy)
| Entry | Name | Source | Rank | Pack role | Status |
|---|---|---|---|---|---|
| 16017 | Patchwork Golem | Naxx | 1 | heavy | S |
| 16018 | Bile Retcher | Naxx | 1 | heavy | S |
| 16021 | Living Monstrosity | Naxx | 1 | heavy | S |
| 16025 | Stitched Giant | Naxx | 1 | heavy | S |
| 16029 | Sludge Belcher | Naxx | 1 | heavy | S |
| 30071 | Stitched Colossus | Naxx | 1 | heavy | S |
| 16034 | Plague Beast | Naxx | 1 | heavy | S |
| 15979 | Tomb Horror | Naxx | 1 | heavy | S |
| 16167 | Bony Construct | Naxx | 1 | heavy | S |
| 27736 | Patchwork Construct | CoS | 1 | heavy | S |
| 28201 | Bile Golem | CoS | 1 | heavy | S |
| 36879 | Plagueborn Horror | PoS | 1 | heavy | S |
| 36522 | Soul Horror | FoS | 1 | heavy | S |
| 37022 | Blighted Abomination | ICC | 1 | heavy | S |
| 10404 | Pustulating Horror | ICC | 1 | heavy | S |
| 26555 | Scourge Hulk | UP | 1 | heavy | S |
| 26623 | Scourge Brute | DTK | 1 | heavy | S |
| 37038 | Vengeful Fleshreaper | ICC | 1 | melee (leaps) | F |
| 16024 | Embalming Slime | Naxx | 1 | melee | S |
| 16243 | Plague Slime | Naxx | 1 | melee | S |

### Skeletons and Ymirjar (melee, casters)
| Entry | Name | Source | Rank | Pack role | Status |
|---|---|---|---|---|---|
| 36881 | Skeletal Slave | PoS | 0 | swarm | P |
| 16193 | Skeletal Smith | Naxx | 1 | melee | S |
| 16194 | Unholy Axe | Naxx | 1 | melee | S |
| 16215 | Unholy Staff | Naxx | 1 | caster | S |
| 16216 | Unholy Swords | Naxx | 1 | melee | S |
| 37012 | Ancient Skeletal Soldier | ICC | 1 | melee | S |
| 36830 | Wrathbone Laborer | PoS | 1 | melee | S |
| 36877 | Wrathbone Skeleton | PoS | 1 | melee | P |
| 37728 | Wrathbone Sorcerer | PoS | 1 | caster | S |
| 36564 | Soulguard Bonecaster | FoS | 1 | caster | S |
| 30284 | Bonegrinder | AK | 1 | heavy | S |
| 26624 | Wretched Belcher | DTK | 1 | melee | S |
| 36841 | Fallen Warrior | PoS | 1 | melee | S |
| 26696 | Ymirjar Berserker | UP | 1 | melee | S |
| 26669 | Ymirjar Savage | UP | 1 | melee | S |
| 26670 | Ymirjar Flesh Hunter | UP | 1 | melee | S |
| 26694 | Ymirjar Dusk Shaman | UP | 1 | caster | S |
| 28368 | Ymirjar Necromancer | UP | 1 | leader | S |
| 37132 | Ymirjar Battle-Maiden | ICC | 1 | melee | F |
| 37133 | Ymirjar Warlord | ICC | 1 | leader | F |
| 37134 | Ymirjar Huntress | ICC | 1 | caster (ranged) | F |
| 37127 | Ymirjar Frostbinder | ICC | 1 | caster | F |
| 38125 | Ymirjar Deathbringer | ICC | 1 | caster | F |

### Cult of the Damned (casters, leaders)
| Entry | Name | Source | Rank | Pack role | Status |
|---|---|---|---|---|---|
| 27731 | Acolyte | CoS | 0 | swarm (caster) | S |
| 15980 | Naxxramas Cultist | Naxx | 1 | caster | S |
| 15981 | Naxxramas Acolyte | Naxx | 1 | caster | S |
| 16165 | Necro Knight | Naxx | 1 | leader | S |
| 36805 | Deathspeaker Servant | ICC | 1 | caster | S |
| 36808 | Deathspeaker Zealot | ICC | 1 | melee | S |
| 36811 | Deathspeaker Attendant | ICC | 1 | caster | S |
| 36807 | Deathspeaker Disciple | ICC | 1 | caster (healer) | S |
| 36829 | Deathspeaker High Priest | ICC | 1 | leader | S |
| 37712 | Deathwhisper Shadowcaster | PoS | 1 | caster | S |
| 37713 | Deathwhisper Torturer | PoS | 1 | caster | S |
| 36788 | Deathwhisper Necrolyte | PoS | 1 | caster | F |
| 36478 | Soulguard Watchman | FoS | 1 | caster | S |
| 36499 | Soulguard Reaper | FoS | 1 | melee | S |
| 36516 | Soulguard Animator | FoS | 1 | leader | S |
| 36620 | Soulguard Adept | FoS | 1 | caster | S |
| 37023 | Plague Scientist | ICC | 1 | caster | S |
| 16020 | Mad Scientist | Naxx | 1 | caster | S |
| 16022 | Surgical Assistant | Naxx | 1 | caster | S |

### Death knights and San'layn (leaders)
| Entry | Name | Source | Rank | Pack role | Status |
|---|---|---|---|---|---|
| 16146 | Death Knight | Naxx | 1 | melee | S |
| 16145 | Death Knight Captain | Naxx | 1 | leader | S |
| 16163 | Death Knight Cavalier | Naxx | 1 | leader | S |
| 16067 | Deathcharger Steed | Naxx | 1 | melee | S |
| 37595 | Darkfallen Blood Knight | ICC | 1 | leader | S |
| 37663 | Darkfallen Noble | ICC | 1 | caster | S |
| 37571 | Darkfallen Advisor | ICC | 1 | caster | S |
| 37662 | Darkfallen Commander | ICC | 1 | leader | S |
| 37665 | Darkfallen Lieutenant | ICC | 1 | melee | S |
| 37666 | Darkfallen Tactician | ICC | 1 | melee | S |
| 37664 | Darkfallen Archmage | ICC | 1 | caster | S |

### Nerubians and crypt fiends
| Entry | Name | Source | Rank | Pack role | Status |
|---|---|---|---|---|---|
| 30338 | Ahn'kahar Swarmer | AK | 0 | swarm | S |
| 32593 | Skittering Swarmer | AzjN | 5* | swarm | P |
| 15977 | Poisonous Skitterer | Naxx | 1 | swarm | P |
| 15974 | Dread Creeper | Naxx | 1 | melee | S |
| 15975 | Carrion Spinner | Naxx | 1 | caster | S |
| 15978 | Crypt Reaver | Naxx | 1 | heavy | S |
| 27734 | Crypt Fiend | CoS | 1 | melee | S |
| 28732 | Anub'ar Warrior | AzjN | 1 | melee | S |
| 28734 | Anub'ar Skirmisher | AzjN | 1 | melee | S |
| 28733 | Anub'ar Shadowcaster | AzjN | 1 | caster | S |
| 29335 | Anub'ar Webspinner | AzjN | 1 | caster | S |
| 29128 | Anub'ar Prime Guard | AzjN | 1 | heavy | S |
| 30277 | Ahn'kahar Slasher | AK | 1 | melee | S |
| 30278 | Ahn'kahar Spell Flinger | AK | 1 | caster | S |
| 30276 | Ahn'kahar Web Winder | AK | 1 | caster | S |
| 31104 | Ahn'kahar Watcher | AK | 1 | leader | S |
| 36725 | Nerub'ar Broodkeeper | ICC | 1 | caster | F |

\* rank 5 is an unusual value; check in curation.

### Spirits
| Entry | Name | Source | Rank | Pack role | Status |
|---|---|---|---|---|---|
| 36551 | Spiteful Apparition | FoS | 0 | swarm | S |
| 16164 | Shade of Naxxramas | Naxx | 1 | caster | S |
| 30085 | Vigilant Shade | Naxx | 1 | melee | S |
| 36666 | Spectral Warden | FoS | 1 | melee | S |
| 37011 | The Damned | ICC | 1 | melee | S |
| 39190 | Wicked Spirit | ICC | 1 | add | F |
| 37098 | Val'kyr Herald | ICC | 1 | leader | F |

### Gargoyles, bats, beasts
| Entry | Name | Source | Rank | Pack role | Status |
|---|---|---|---|---|---|
| 16168 | Stoneskin Gargoyle | Naxx | 1 | melee | S |
| 37544 | Spire Gargoyle | ICC | 1 | caster | S |
| 37532 | Frostwing Whelp | ICC | 1 | caster | F |
| 16036 | Frenzied Bat | Naxx | 1 | swarm | S |
| 16037 | Plagued Bat | Naxx | 1 | swarm | S |
| 26672 | Bloodthirsty Tundra Wolf | UP | 1 | melee | S |
| 16056 | Diseased Maggot | Naxx | 1 | add (on-death split) | P |
| 16057 | Rotting Maggot | Naxx | 1 | add (on-death split) | P |
| 16297 | Mutated Grub | Naxx | 1 | add | S |

Excluded from packs: 16236 Eye Stalk (stationary tentacle, Naxx Plague wing; maybe a mechanic later).

## 5. Rares and minibosses (`rare` / `miniboss` scaling roles)

Single-spawn named or unusually strong units. Not in normal packs; picked by the rare roll, by anchors marked
for a miniboss, or by encounter modules.

| Entry | Name | Source | Status | Notes |
|---|---|---|---|---|
| 37007 | Deathbound Ward | ICC | S | Gate guardian; good miniboss. |
| 36724 | Servant of the Throne | ICC | S | |
| 36880 | Decaying Colossus | ICC | S | Big model; heavy miniboss. |
| 37546 | Frenzied Abomination | ICC | S | |
| 37545 | Spire Minion | ICC | S | |
| 36723 | Frostsworn General | HoR | C++ (check registration) | Stock mini-boss; curated as a boss candidate. |
| 37126 | Sister Svalna | ICC | F | Shadow entry + library mechanics. |
| 37025 | Stinky, 37217 Precious | ICC | F | Named plague dogs/abominations; shadow entries. |
| 26632 | The Prophet Tharon'ja | DTK | F | Instance factory (`GetDraktharonKeepAI`, `boss_tharon_ja.cpp:73`): library boss with his model. |

Open-world rares later (Northrend elites such as Pustulent Colossus, Coprous the Defiled, Thexal Deathchill):
check quest links first.

## 6. Bosses
Stock Scourge bosses (Naxxramas, ICC, Northrend 5-man bosses) are all C++ with instance scripts, mostly through
the instance factory, so on a clone they would be plain melee. They are **not** in the pool. Their models and
names come back as **library bosses** (design section 13) with mechanics from the mechanic library, e.g. a
"Patchwerk-like" hateful-strike construct, a "Loatheb-like" spore caster. Stock-boss curation stays a
phase-3 task.

## 7. Open-world supplement (swarm filler, later)

Common Northrend Scourge with many spawns, mostly rank 0. Useful as `swarm` filler and for variety in
Scourge Rifts. Many are quest mobs (kill credit, phases, quest-item interactions), so curate before use.
Examples (spawn count in brackets): 31812 Decomposed Ghoul (307), 32255 Converted Hero (235), 29897
Reanimated Corpse (128), 28220 Frostbitten Corpse (127), 28218 Snowblind Ghoul (120), 28108 Bonescythe Ravager
(93), 31813 Frostskull Magus (51), 32467 Skeletal Reaver (51, elite), 32175 Chosen Zealot (44, elite), 30842
Wandering Shadow (44), 32154 Scourge War Machine (41), 31738 Cultist Corrupter (37), 29893 Banshee
Soulclaimer (35), 30687 Skeletal Constructor (34), 31900 Scourge Banner-Bearer (34), 30696 Corpulent Horror
(30), 29570 Nascent Val'kyr (27), 31140 Hulking Abomination (22), 31731 Wyrm Reanimator (21), 34734 Dark
Ritualist (20), 32284 Scourge Soulbinder (17), 31783 Vrykul Necrolord (15).

Avoid for now: entries with C++ quest scripts (33438 Boneguard Footman, 37881 Wretched Ghoul, 27712 Mindless
Ghoul, 27531/27686 Dragonblight attackers).

## 8. Scourge-touched natives

The cheap middle step between native and Scourge (design section 8): a native entry spawned with
- an undead visual aura (a stock spell with a green/plague visual and no gameplay effect; pick in phase 1 by
  browsing Spell.dbc visuals in the client data),
- one forced monster affix from the `plague` tag (affix appendix),
- name unchanged (creature names are client-cached per entry; renaming needs a shadow entry).

Share by corruption level (starting values):

| Corruption | Native | Scourge-touched native | Scourge |
|---|---|---|---|
| 0 (tiers 1–3) | 90% | 10% | 0% |
| 1 (4–6) | 60% | 25% | 15% |
| 2 (7–9) | 35% | 30% | 35% |
| 3 (10–12) | 15% | 25% | 60% |
| 4 (13–15) | 0% | 15% | 85% |
| 5 (16, "overrun") | 0% | 0% | 100% |

Bosses follow the same table: native boss → Scourge-touched native boss → Scourge library boss.

## 9. Counts and next steps

- Core pool (section 4): 127 entries (109 SmartAI, 7 plain, 11 factory-scripted), plus 10 rares/minibosses.
- Enough variety for phase 1: every archetype has swarm, melee and leader options except spirits (no strong
  leader without a shadow entry).
- Next: curation harness pass on a test stack; generate shadow entries for the `F` rows; pick the
  Scourge-touched aura spell.
