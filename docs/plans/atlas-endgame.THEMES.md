# atlas-endgame: native themes for the phase-1 maps

Appendix to `atlas-endgame.DESIGN.md` (section 8) and `atlas-endgame.CLONES.md` (starter set). For each of
the five starter maps: who lives there, which pack role each creature plays, which bosses can stay stock,
what is excluded, and what each map adds that no other map has.

**Source:** `tools/natives.py` (static parse of `data/sql/base/db_world`, plus a grep of each C++ script's
registration), 2026-11-22. Full per-entry data with spell lists: `tools/natives.json`. No server or DB used.

---

## 1. Rules that apply to every native theme

**Status letters** (as in the Scourge appendix): `S` SmartAI, keeps its abilities on a clone; `P` plain,
melee only; `F` C++ through the instance factory, falls back to default AI on a clone → shadow entry or
library boss.

**Bosses.** A boss whose AI is SmartAI stays a **stock boss** candidate: its abilities are keyed by entry,
so they work on a clone (curation still tests it). A boss with an `F` script becomes a **library boss** with
the stock model, name and the map's flavour mechanics. Result for the starter set:

| Map | Stock-boss candidates (SmartAI) | Library bosses (factory C++) |
|---|---|---|
| Deadmines | 6 of 7 | Mr. Smite |
| Shadowfang Keep | 8 of 8 | – |
| Stratholme | 11 of 12 | Baroness Anastari |
| Drak'Tharon Keep | 0 of 4 | Trollgore, Novos, King Dred, Tharon'ja |
| Halls of Lightning | 0 of 4 | Bjarngrim, Volkhan, Ionar, Loken |

So the classic maps bring ready-made bosses, and the WotLK maps need the mechanic library first. Phase 1
should therefore lean on Deadmines, Shadowfang and Stratholme bosses, and give DTK and HoL one simple
library boss each.

**Summons get scaled too.** Many native abilities summon creatures (VanCleef's allies, Rivendare's Raise
Dead, the Eye of Naxxramas' gargoyles, the Unforgiven's phantoms). `OnBeforeCreatureSelectLevel` /
`OnCreatureSelectLevel` run for every creature created on the map, so the module scales any creature it
didn't spawn itself as role `add` (design section 9). Without that, summons would arrive at level 20.

**Instance data from SmartAI.** Some native bosses send `SET_INST_DATA` (Rhahk'Zor, Wolf Master Nandos,
Fenrus, Barthilas, Nerub'enkan, Maleki). On a clone that reaches the module's InstanceScript, which ignores
unknown IDs (design section 8). No change needed, but curation should confirm nothing waits on a reply.

**Classic spell numbers.** Classic natives (Deadmines, Shadowfang, Stratholme) cast level-20/60 spells. The
per-creature `spellScale` (design section 9) lifts their damage to level-80 values; CC durations are
unchanged and fine. WotLK natives (DTK, HoL) cast normal-mode spells on a clone (the clone's difficulty is
normal), so heroic spell variants in their SmartAI never fire.

**Always excluded:** critters (rats, beetles, spiders of level 1), friendly NPCs and prisoners, triggers and
`[DND]`/`[PH]` units, holiday event units (Love is in the Air in Shadowfang), and anything non-selectable
unless noted.

---

## 2. The Deadmines (36) — theme `defias`

Defias Brotherhood and their goblin crew. Linear route: mine → goblin foundry → mast room → cove → ship.

| Entry | Name | Spawns | Rank | Pack role | Status | Notes |
|---|---|---|---|---|---|---|
| 598 | Defias Miner | 46 | 0 | swarm | S | |
| 4416 | Defias Strip Miner | 27 | 0 | swarm | S | |
| 1725 | Defias Watchman | 2 | 0 | swarm | S | |
| 657 | Defias Pirate | 21 | 1 | melee | S | Cove and ship. |
| 634 | Defias Overseer | 12 | 1 | leader | S | |
| 4417 | Defias Taskmaster | 3 | 1 | leader | S | |
| 636 | Defias Blackguard | 4 | 1 | melee | S | Stealth ambusher; also VanCleef's summon. |
| 1732 | Defias Squallshaper | 19 | 1 | caster | S | |
| 1729 | Defias Evoker | 10 | 1 | caster | S | |
| 4418 | Defias Wizard | 2 | 1 | caster | S | |
| 3947 | Goblin Shipbuilder | 16 | 1 | melee | S | No spells: pure melee. |
| 641 | Goblin Woodcarver | 13 | 1 | melee | S | |
| 1731 | Goblin Craftsman | 13 | 1 | melee | S | |
| 622 | Goblin Engineer | 6 | 1 | caster | S | Throws bombs. |

Rares: 3586 Miner Johnson (rank 2, S), 642 Sneed's Shredder (mechanical, S; in stock it drops Sneed on
death — check in curation).

Bosses (stock candidates, all S): 644 Rhahk'Zor, 1763 Gilnid, 647 Captain Greenskin, 645 Cookie,
639 Edwin VanCleef (summons allies), plus Sneed's Shredder as a miniboss. 646 Mr. Smite is `F` → library boss.
Final arena: the ship deck (VanCleef).

Flavour hooks: goblin bombs and cannons for `GROUND` affixes; the ship deck as a natural arena.

Scourge-touched natives (corruption 1–3): Defias Miner, Defias Pirate, Defias Overseer.

---

## 3. Shadowfang Keep (33) — theme `shadowfang`

Arugal's worgen, the keep's ghosts, worgs and fel creatures. Vertical keep: courtyard → stables → dining hall
→ ramparts → Arugal's tower.

| Entry | Name | Spawns | Rank | Pack role | Status | Notes |
|---|---|---|---|---|---|---|
| 3875 | Haunted Servitor | 19 | 1 | melee | S | Undead (ghost). |
| 3877 | Wailing Guardsman | 10 | 1 | melee | S | Undead. |
| 3873 | Tormented Officer | 6 | 1 | leader | S | Undead. |
| 3853 | Shadowfang Moonwalker | 14 | 1 | caster | S | |
| 3855 | Shadowfang Darksoul | 12 | 1 | caster | S | |
| 3851 | Shadowfang Whitescalp | 7 | 1 | caster | S | |
| 3859 | Shadowfang Ragetooth | 8 | 1 | melee | S | |
| 3857 | Shadowfang Glutton | 4 | 1 | melee | S | |
| 3854 | Shadowfang Wolfguard | 1 | 1 | leader | S | |
| 2529 | Son of Arugal | 8 | 1 | melee | S | |
| 3863 | Lupine Horror | 7 | 1 | heavy | S | Undead worgen. |
| 3862 | Slavering Worg | 9 | 1 | melee | S | No spells. |
| 3861 | Bleak Worg | 8 | 1 | melee | S | |
| 5058 | Wolfguard Worg | 1 | 0 | swarm | S | |
| 3864 | Fel Steed | 3 | 1 | melee | S | |
| 3866 | Vile Bat | 1 | 1 | swarm | S | |
| 3868 | Blood Seeker | 1 | 1 | swarm | S | |

Rares: 3872 Deathsworn Captain (rank 2, undead, S), 14682 Sever (undead, S; a Scourge Invasion boss, so
it is a natural Scourge-corruption rare here).

Bosses (all S, stock candidates): 3914 Rethilgore, 3886 Razorclaw the Butcher, 3887 Baron Silverlaine,
4278 Commander Springvale, 4279 Odo the Blindwatcher, 3927 Wolf Master Nandos, 4274 Fenrus the Devourer,
4275 Archmage Arugal. Final arena: Arugal's tower room.

Excluded: 36885 Crown Apothecary, 36296 Apothecary Hummel, 36530/36212 `[DND]` (Love is in the Air event);
prisoners 3849, 3850, 4444; 10000 Arugal (cinematic, non-hostile); 17822, 38208 (friendly).

Flavour hooks: ghosts (already undead) are the corruption-0 bridge to the Scourge; worg packs suit the
`Fleet` and `Teeming` affixes.

Scourge-touched natives: Shadowfang Ragetooth, Son of Arugal, Slavering Worg. The ghosts count as
Scourge-adjacent: at corruption ≥ 2 they swap for Scourge spirits first.

---

## 4. Stratholme (329) — two wings, two themes

Wings come from spawn positions (centroids in `natives.json`): the Scarlet Bastion and the streets lie
west/north (x < 3800), the Ziggurats and the Slaughterhouse east (x > 3800). On a clone the gates between
them are open (GOs), so "wing" is layout data: entrance, anchors and arena per wing.

### 4a. Wing "Live" — theme `scarlet` + street undead

| Entry | Name | Spawns | Rank | Pack role | Status |
|---|---|---|---|---|---|
| 10418 | Crimson Guardsman | 17 | 1 | melee | S |
| 10424 | Crimson Gallant | 17 | 1 | melee | S |
| 10421 | Crimson Defender | 9 | 1 | heavy | S |
| 10420 | Crimson Initiate | 9 | 1 | caster | S |
| 10419 | Crimson Conjuror | 9 | 1 | caster | S |
| 10423 | Crimson Priest | 6 | 1 | caster (healer) | S |
| 10422 | Crimson Sorcerer | 3 | 1 | caster | S |
| 10425 | Crimson Battle Mage | 4 | 1 | caster | S |
| 10426 | Crimson Inquisitor | 3 | 1 | leader | S |
| 11043 | Crimson Monk | 4 | 1 | melee | S |
| 10390 | Skeletal Guardian | 55 | 0 | swarm (caster) | S |
| 10391 | Skeletal Berserker | 54 | 0 | swarm | S |
| 10382 | Mangled Cadaver | 35 | 1 | melee | S |
| 10381 | Ravaged Cadaver | 30 | 1 | melee | S |
| 10405 | Plague Ghoul | 34 | 1 | melee | S |
| 10414 | Patchwork Horror | 3 | 1 | heavy | S |
| 10384 | Spectral Citizen | 13 | 1 | melee | S |
| 10385 | Ghostly Citizen | 12 | 1 | melee | S |

Bosses (S): 10558 Hearthsinger Forresten, 10808 Timmy the Cruel, 10516 The Unforgiven (summons phantoms),
11032 Malor the Zealous, 10997 Cannon Master Willey, 10811 Archivist Galford, 10812 Grand Crusader Dathrohan
(final arena: the Bastion chapel; his Balnazzar transform must be checked in curation).
Rares: 10393 Skul (rank 2), 11082 Stratholme Courier, 14684 Balzaphon (Scourge Invasion boss, S).

### 4b. Wing "Dead" — theme `scourge-classic`

Already Scourge: a classic, level-60 Scourge theme of its own, distinct from the WotLK pool.

| Entry | Name | Spawns | Rank | Pack role | Status |
|---|---|---|---|---|---|
| 10406 | Ghoul Ravener | 17 | 1 | melee | S |
| 10407 | Fleshflayer Ghoul | 10 | 1 | melee | S |
| 10399 | Thuzadin Acolyte | 16 | 0 | swarm (caster) | S |
| 10398 | Thuzadin Shadowcaster | 14 | 1 | caster | S |
| 10400 | Thuzadin Necromancer | 17 | 1 | leader | S |
| 10463 | Shrieking Banshee | 9 | 1 | caster | S |
| 10464 | Wailing Banshee | 6 | 1 | caster | S |
| 10412 | Crypt Crawler | 7 | 1 | melee | S |
| 10413 | Crypt Beast | 5 | 1 | heavy | S |
| 10408 | Rockwing Gargoyle | 7 | 1 | melee | S |
| 10416 | Bile Spewer | 4 | 1 | heavy | S |
| 10417 | Venom Belcher | 9 | 1 | heavy | S |

Bosses: 10435 Magistrate Barthilas (S, at the wing gate), 10437 Nerub'enkan (S), 10438 Maleki the Pallid (S),
10436 Baroness Anastari (`F` → library boss), 10440 Baron Rivendare (S, Raise Dead; final arena: the
Slaughterhouse). Rares: 10809 Stonespine (rank 2), 10409 Rockwing Screecher.

Special: 10411 **Eye of Naxxramas** (3 spawns, S): invisible out of combat, summons Rockwing Gargoyles on
aggro. Not a pack member; reused as a map mechanic ("alarm eyes": an unseen eye calls a gargoyle pack) and as
the basis of a monster affix later.

Excluded: 10917 Aurius, 16031 Ysida Harmon, 15607 Elder Farwhisper (friendly); 10415 Ash'ari Crystal (Baron's
event object); triggers 14646, 16046, 16100.

Scourge-touched natives (Live wing): Crimson Guardsman, Crimson Gallant, Crimson Conjuror. The Dead wing is
Scourge from corruption 0; as corruption rises its classic Scourge are replaced by the WotLK pool
("the Lich King's newer army").

---

## 5. Drak'Tharon Keep (600) — theme `drakkari`

Living Drakkari trolls and their beasts, plus the Risen Drakkari the Scourge made of them. DTK is the one map
where "Scourge-touched natives" exist in stock data: the Risen Drakkari (already in the Scourge pool).

### Native (living) creatures
| Entry | Name | Spawns | Rank | Pack role | Status | Notes |
|---|---|---|---|---|---|---|
| 26620 | Drakkari Guardian | 37 | 1 | melee | S | Main native trash. |
| 26639 | Drakkari Shaman | 2 | 1 | caster | S | |
| 27431 | Drakkari Commander | 1 | 1 | leader | S | |
| 26641 | Drakkari Gutripper | 6 | 1 | melee | S | Beast. |
| 26628 | Drakkari Scytheclaw | 3 | 1 | melee | S | Beast. |
| 26622 | Drakkari Bat | 7 | 0 | swarm | P | |
| 26625 | Darkweb Recluse | 5 | 1 | caster | S | Spider: Encasing Webs, Poison Spit. |

### Scourge-touched (stock Risen Drakkari; in the Scourge pool)
26635 Risen Drakkari Warrior, 26636 Soulmage, 26637 Handler, 26638 Bat Rider, 26830 Death Knight;
26621 Ghoul Tormentor, 27871 Flesheating Ghoul, 26624 Wretched Belcher, 26623 Scourge Brute,
26626 Scourge Reanimator (leader).

At corruption 0, DTK already shows the Risen Drakkari at the "Scourge-touched" share (Scourge appendix,
section 8): native packs = living Drakkari, touched packs = Risen Drakkari, Scourge packs = WotLK pool.

Bosses: all four are `F` (Trollgore, Novos, King Dred, Tharon'ja: `GetDraktharonKeepAI`). Library bosses with
their models. Mechanic ideas from their stock fights, for the mechanic library: Trollgore's stacking
Infected Wound and corpse explosions, Novos' channelled shield with add waves (needs kill-priority, Q10),
Dred's raptor pack call, Tharon'ja's skeleton/flesh phase swap.

Excluded: 27909 Darkweb Victim (cocoon), 27490 cosmetic bats, 28492 event trigger, 26712, 26675 targets,
30534 Elder Kilias, critters 14881, 15475, 2110, 4075.

---

## 6. Halls of Lightning (602) — theme `stormforged`

Iron dwarves, Stormforged vrykul-constructs and fire/storm elementals in titan halls.

| Entry | Name | Spawns | Rank | Pack role | Status | Notes |
|---|---|---|---|---|---|---|
| 28578 | Hardened Steel Reaver | 8 | 1 | heavy | S | Shield Slam, Cleave. |
| 28579 | Hardened Steel Berserker | 6 | 1 | melee | S | Hurl Weapon, Enrage. |
| 28580 | Hardened Steel Skycaller | 6 | 1 | caster (ranged) | S | Impact Multi-Shot. |
| 28581 | Stormforged Tactician | 12 | 1 | caster | S | Welding Beam. |
| 28582 | Stormforged Mender | 5 | 1 | caster (healer) | S | Renew Steel. |
| 28836 | Stormforged Runeshaper | 10 | 1 | melee | S | Charged Flurry. |
| 28837 | Stormforged Sentinel | 6 | 1 | leader | S | Sleep, Renew. |
| 28835 | Stormforged Construct | 7 | 1 | heavy | S | Rune Punch, Forked Lightning. |
| 28920 | Stormforged Giant | 2 | 1 | heavy | S | Thunderstorm. Miniboss-sized. |
| 28838 | Titanium Vanguard | 7 | 1 | melee | S | Charge, poison spear. |
| 28961 | Titanium Siegebreaker | 16 | 1 | melee (dormant) | S | See "statues" below. |
| 28965 | Titanium Thunderer | 15 | 1 | caster (dormant) | S | See "statues" below. |
| 28583 | Blistering Steamrager | 13 | 1 | caster | S | Elemental, Steam Blast. |
| 28584 | Unbound Firestorm | 9 | 1 | caster | S | Elemental, Lava Burst. |
| 28826 | Stormfury Revenant | 5 | 1 | melee | S | Elemental. |
| 28547 | Storming Vortex | 2 | 1 | caster | S | Elemental. |
| 28585 | Slag | 14 | 0 | swarm | S | Elemental, Blast Wave. |

**Statues.** Titanium Siegebreakers and Thunderers spawn frozen (`Freeze Anim`), immune and unselectable;
their SmartAI wakes them on `ACTION_DONE` 0 (removes the flags and the aura). The module can wake them with
`AI()->DoAction(0)`. That gives a free, map-specific pack type: **dormant statues** that come alive when
players get close or when a nearby pack dies. They count as hostile pool members, despite the
non-selectable flag (the census marks them "NONHOSTILE" for that reason only).

Bosses: all four `F` (Bjarngrim, Volkhan, Ionar, Loken). Library bosses with their models; mechanic ideas:
Bjarngrim's stance changes, Volkhan's golems that shatter (on-death ground damage), Ionar's split into
sparks (spread), Loken's pulsing nova (stay close: inverse of spread).

Excluded: 28825 Cyclone (17 rank-0 units with no AI, decorative), 29048 Ulduar Monitor, 28823 Volkhan's Anvil,
32258 Gold Beetle.

Scourge-touched natives: Hardened Steel Reaver, Stormforged Construct, Titanium Vanguard (a corrupted
titan-forged look suits "saronite infection").

---

## 7. Summary

| Theme | Map | Pool entries | Stock-boss candidates | Rares | Special |
|---|---|---|---|---|---|
| `defias` | Deadmines | 14 | 6 | 2 | ship deck arena |
| `shadowfang` | Shadowfang Keep | 17 | 8 | 2 | ghosts bridge to Scourge |
| `scarlet` | Stratholme Live | 18 (10 Scarlet + 8 street undead) | 7 | 3 | |
| `scourge-classic` | Stratholme Dead | 12 | 4 | 2 | Eye of Naxxramas alarm |
| `drakkari` | Drak'Tharon Keep | 7 (+10 Risen in the Scourge pool) | 0 | 0 | stock Scourge-touched natives |
| `stormforged` | Halls of Lightning | 17 | 0 | 0 | dormant statues |

Every theme has swarm, melee, caster and leader entries, so the pack builder works on all five maps with
native packs only (corruption 0).

## 8. Next steps

1. Curation harness pass for these 85 native entries and 25 stock bosses (test stack).
2. Library bosses for DTK and HoL: one each for phase 1, built from the mechanic ideas above.
3. Decide the theme of each boss arena at corruption ≥ 3 (native boss with a Scourge affix, or a Scourge
   library boss).
4. Harvest (design section 6.1) will attach these entries to anchors; the theme tables above become
   `atlas_theme` / `atlas_theme_creature` rows.
