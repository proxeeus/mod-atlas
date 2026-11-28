# atlas-endgame: clone catalog

Appendix to `atlas-endgame.DESIGN.md` (sections 6.1, 8, 15). Which existing maps become atlas maps, in what
order, and what each one needs.

**Source of the numbers:** static parse of `data/sql/base/db_world/*.sql` (no database, no server), done
2026-11-22 with the scripts in `tools/` next to this file. "Hostile spawns" excludes vendors, triggers and
non-selectable units. The base dump predates later `updates/`, so treat counts as approximate; the real
harvest (section 6.1 of the design) reads the actual spawns once, on a test stack.

---

## 1. Selection rules

A source map is a good atlas map when:

1. **It walks without gameobjects.** A clone has no GO spawns, so doors are open (good), but bridges,
   elevators and transports are gone (stock mmaps never contained them anyway). Any part reachable only through
   a GO is cut from the layout.
2. **No vehicles needed** to reach any part of it (Oculus, Eye of Eternity, Ulduar siege, Skadi).
3. **A clear route** from entrance to a boss arena. Mazes (Wailing Caverns) need a hand-made route.
4. **Water is limited.** Bots swim, but pulls in deep water are messy.
5. **Zone-keyed side effects are known** (section 4 below) and stripped by the module.

Big maps are not excluded: they are split into **wings** (layout data, one clone ID per source).

**Size classes** (rough run time solo + 4 bots at tier 1): S ≈ 8 min, M ≈ 15 min, L ≈ 25 min, XL needs
a hand-made route. Wings count as M or L each.

**Tier bands** (map item tiers 1–16):

| Band | Tiers | Pool |
|---|---|---|
| A | 1–4 | Short classic and TBC 5-mans. |
| B | 5–8 | WotLK 5-mans, larger classic/TBC 5-mans, Scourge-native small maps. |
| C | 9–12 | Big dungeons and their wings, outdoor maps. |
| D | 13–16 | Raid wings. |
| P | pinnacle | Boss arenas (fragments, section 11 of the design). |

Bands are where a map *starts* dropping; atlas progression can push any map into higher tiers. Corruption
(design section 8) rises with tier, not band, so a band-A map at tier 14 is mostly Scourge.

---

## 2. Phase-1 starter set

| Clone ID | Source | Why |
|---|---|---|
| 900 | Drak'Tharon Keep (600) | Scourge-native, compact, exercises the Scourge pool at its home. |
| 901 | Stratholme (329) | Scourge-native city, two wings: tests wing selection. |
| 902 | The Deadmines (36) | Non-Scourge native theme (Defias): tests corruption swapping natives out. Cave + ship geometry. |
| 903 | Halls of Lightning (602) | Iron dwarves/constructs, wide halls: big pulls, tests pack caps. |
| 904 | Shadowfang Keep (33) | Vertical keep with stairs: tests multi-floor pathing and anchor heights. |

Together they cover corridors, an open city, a cave plus a ship, wide halls and a vertical keep; three
native themes besides the Scourge; no vehicles, no transports. Zone leaks: DTK and HoL get the Wintergrasp
buff (strip it, section 4); the other three have none.

**Clone IDs:** 900–949 for clones, assigned in the order maps are added (the registry is `atlas_map_source`,
design section 19); 950–999 stay free for the phase-5 generated-map pool. No `Map.dbc` override exists in
`my_custom_dbcs/` (checked: only CharBaseInfo, CharStartOutfit, SkillRaceClassInfo), and the highest stock
instance map is 724, so the range is free in stock data. The live DB still needs a read-only check before
install (design section 19).

---

## 3. Full catalog (instances)

Verdict: ✅ usable, ⚠ usable with work (see notes), ❌ excluded.

| Map | Source | Verdict | Band | Size | Hostile spawns | Span x/y/z (yd) | Native theme | Notes |
|---|---|---|---|---|---|---|---|---|
| 33 | Shadowfang Keep | ✅ | A | M | 149 | 222/221/81 | Worgen, Shadowfang undead | Phase-1 starter. Vertical keep (stairs): tests multi-floor pathing. |
| 34 | The Stockade | ✅ | A | S | 93 | 126/285/10 | Defias prisoners | Tiny: one corridor plus cells. A "quick map". |
| 36 | The Deadmines | ✅ | A | M | 213 | 313/555/58 | Defias, goblins | Phase-1 starter. Long and linear: mine, foundry, cove, ship. Doors are GOs, so the clone is open. |
| 47 | Razorfen Kraul | ✅ | A | M | 188 | 226/604/40 | Quilboar | Bramble maze, linear in practice. |
| 129 | Razorfen Downs | ✅ | A | M | 259 | 292/456/40 | Quilboar under Scourge (Amnennar) | Natives already Scourge-touched: natural low-corruption map. |
| 189 | Scarlet Monastery | ✅ | A | 4 wings | 338 | 1845/1908/41 | Scarlet Crusade | One map ID, four wings (Graveyard, Library, Armory, Cathedral) as layout data. |
| 389 | Ragefire Chasm | ✅ | A | S | 131 | 396/362/72 | Burning Blade, troggs | Short cavern loop. |
| 542 | Blood Furnace | ✅ | A | M | 152 | 516/395/176 | Fel orcs | Cell room = natural wave anchor. |
| 543 | Hellfire Ramparts | ✅ | A | S | 98 | 331/301/37 | Fel orcs | Short outdoor ramparts. |
| 546 | Underbog | ✅ | A | M | 198 | 500/555/123 | Naga, bog beasts | Some swimming. |
| 547 | Slave Pens | ✅ | A | M | 168 | 432/809/92 | Naga, slaves | Some swimming. |
| 554 | Mechanar | ✅ | A | S | 98 | 296/245/30 | Mechanical | Short. |
| 557 | Mana-Tombs | ✅ | A | M | 173 | 394/295/20 | Ethereals | Flat. |
| 558 | Auchenai Crypts | ✅ | A | M | 144 | 452/467/186 | Undead draenei, spirits | Undead-native TBC map: low-corruption Scourge flavour. |
| 70 | Uldaman | ✅ | B | L | 310 | 516/396/30 | Troggs, Dark Iron, titan constructs | Two halves; the titan vault is a good arena. |
| 109 | Sunken Temple | ✅ | B | M | 311 | 381/417/136 | Trolls, dragonkin | Ring of balconies around a central pit; atrium = arena. |
| 209 | Zul'Farrak | ✅ | B | L | 268 | 665/632/46 | Sandfury trolls | Mostly outdoor, open-field pulls. Pyramid stairs = arena. |
| 269 | Black Morass | ✅ | B | arena | 218 | 578/620/16 | Infinite dragonflight | Small island: fits a wave map (Argent Trial, Scourge Rift). |
| 289 | Scholomance | ✅ | B | M | 362 | 311/294/70 | Cult of the Damned, Scourge | Scourge-native. Gandling's room teleports are scripted (absent on a clone). |
| 329 | Stratholme | ✅ | B | 2 wings | 458 | 682/817/81 | Scarlet Crusade, Scourge | Phase-1 starter. Scourge-native city; wings Live (Scarlet) and Dead (Baron). Gates are GOs (open). |
| 349 | Maraudon | ✅ | B | 3 wings | 603 | 1278/826/177 | Centaurs, elementals, satyrs | Orange / Purple / Inner wings; open caverns. |
| 540 | Shattered Halls | ✅ | B | M | 240 | 570/445/80 | Fel orcs | Linear halls. |
| 545 | Steamvault | ✅ | B | M | 165 | 427/508/42 | Naga | Water channels; walkable routes exist. |
| 552 | Arcatraz | ✅ | B | M | 92 | 409/360/68 | Prisoners, Burning Legion | Linear. |
| 553 | Botanica | ✅ | B | M | 184 | 404/541/31 | Blood elves, botanical | Linear, wide rooms. |
| 555 | Shadow Labyrinth | ✅ | B | M | 226 | 436/572/41 | Cabal cultists | Long corridors; Murmur's room = arena. |
| 556 | Sethekk Halls | ✅ | B | M | 159 | 368/348/84 | Arakkoa | Compact. |
| 574 | Utgarde Keep | ✅ | B | M | 161 | 449/638/279 | Vrykul (Dragonflayer) | Source vehicle spawns are irrelevant (a clone spawns none). |
| 576 | The Nexus | ✅ | B | M | 254 | 489/560/73 | Blue dragonflight, Scourge | Open frozen halls. |
| 585 | Magisters' Terrace | ✅ | B | M | 165 | 293/815/66 | Blood elves, Burning Legion | Compact, two floors joined by stairs. |
| 599 | Halls of Stone | ✅ | B | M | 121 | 453/525/40 | Iron dwarves, titan constructs | Tribunal hall is a fine arena. |
| 600 | Drak'Tharon Keep | ✅ | B | M | 145 | 317/231/138 | Drakkari, Scourge | Phase-1 starter. Compact, Scourge-native. Novos' room = arena. |
| 602 | Halls of Lightning | ✅ | B | M | 149 | 426/535/59 | Iron dwarves, constructs | Phase-1 starter. Wide halls, good for big pulls. |
| 608 | Violet Hold | ✅ | B | arena | 20 | 147/158/49 | Blue dragonflight, portals | One room with cells: perfect wave map. |
| 619 | Ahn'kahet | ✅ | B | M | 183 | 568/719/130 | Nerubians, Twilight cult, faceless | Multi-level caves. Good Nerubian Burrow host. |
| 632 | Forge of Souls | ✅ | B | S | 61 | 762/518/129 | Scourge | Scourge-native, short. |
| 658 | Pit of Saron | ✅ | B | M | 199 | 658/550/206 | Scourge, Ymirjar, slaves | Scourge-native outdoor tunnel; icicles are scripted (absent). |
| 668 | Halls of Reflection | ✅ | B | S | 14 | 320/571/85 | Scourge | Corridor + throne room; the escape tunnel suits a chase encounter. |
| 229 | Blackrock Spire | ✅ | C | 2 wings | 585 | 441/350/141 | Blackrock orcs, dragonkin | Lower and Upper Spire (Rend's arena). Some GO-gated paths: check in harvest. |
| 230 | Blackrock Depths | ✅ | C | 3 wings | 982 | 1213/1033/86 | Dark Iron dwarves, fire elementals | Huge. Wings: Detention/Ring, Lyceum/Grim Guzzler, Imperial Seat. |
| 309 | Zul'Gurub | ✅ | C | L | 908 | 952/778/152 | Gurubashi trolls, beasts | Outdoor jungle; big open pulls. |
| 429 | Dire Maul | ✅ | C | 3 wings | 822 | 1081/1712/160 | Ogres, satyrs, highborne ghosts | Dire Maul East / West / North as wings. |
| 509 | Ruins of Ahn'Qiraj | ✅ | C | L | 356 | 1242/1072/170 | Qiraji, silithid | Outdoor ruins; open pulls. |
| 568 | Zul'Aman | ✅ | C | L | 232 | 684/1100/106 | Amani trolls | Outdoor-ish, four animal-god platforms. |
| 595 | Culling of Stratholme | ✅ | C | L | 238 | 1026/927/74 | Scourge, Infinite dragonflight | Scourge-native city streets. Strip the human-disguise auras (spell_area 35480–35483). |
| 409 | Molten Core | ✅ | D | L | 233 | 618/689/120 | Fire elementals, core hounds | Big open caverns: ideal for big pack sizes. Keep anchors off lava. |
| 469 | Blackwing Lair | ✅ | D | M | 309 | 361/401/74 | Black dragonflight, orcs | Suppression room and Vael hall = gauntlet anchors. |
| 531 | Temple of Ahn'Qiraj | ✅ | D | L | 563 | 1351/1271/250 | Qiraji, silithid | Very big. Wings: Skeram → Twins; C'Thun chamber as pinnacle arena. |
| 532 | Karazhan | ✅ | D | 2 wings | 650 | 564/569/335 | Spectral servants, Burning Legion | Vertical tower. Lower (stables → Opera), Upper (library → Prince). Chess hall excluded. |
| 533 | Naxxramas | ✅ | D | 4 wings + 2 arenas | 820 | 1232/2310/180 | Scourge | Scourge-native raid. Arachnid, Plague, Military, Construct quarters as wings; Sapphiron and Kel'Thuzad rooms as pinnacles. Teleporters are GOs; the module places its own portals. |
| 550 | Tempest Keep (The Eye) | ✅ | D | M | 188 | 802/878/137 | Blood elves, mechanical | Four boss rooms off one corridor; Kael's room = arena. |
| 564 | Black Temple | ✅ | D | 4 wings | 668 | 774/1060/1019 | Illidari, demons | Wings: Sanctuary, Halls of Anguish, Den, Temple Summit (Illidan pinnacle). |
| 580 | Sunwell Plateau | ✅ | D | L | 280 | 612/639/185 | Blood elves, Burning Legion | Kil'jaeden's platform = pinnacle arena. Strip the aura-gated spell_area rows only if they show up. |
| 249 | Onyxia's Lair | ✅ | P | arena | 5 | 190/121/49 | Black dragonflight | Pinnacle arena (Onyxia's cave). Whelp caves = add spawn points. |
| 544 | Magtheridon's Lair | ✅ | P | arena | 17 | 110/148/46 | Burning Legion | Pinnacle arena (Magtheridon). |
| 565 | Gruul's Lair | ✅ | P | arena | 18 | 172/238/18 | Gronn | Pinnacle arena (Gruul). |
| 615 | Obsidian Sanctum | ✅ | P | arena | 26 | 323/349/41 | Black dragonflight | Pinnacle arena (Sartharion island). |
| 624 | Vault of Archavon | ✅ | P | arena | 13 | 480/393/34 | Elementals | Four boss rooms; Wintergrasp gating does not apply on a clone. |
| 649 | Trial of the Crusader | ✅ | P | arena | 18 | 135/109/28 | Argent Crusade, Nerubians | Coliseum: Argent Trial host / pinnacle. |
| 650 | Trial of the Champion | ✅ | P | arena | 36 | 143/116/31 | Argent Crusade | Coliseum arena. |
| 724 | Ruby Sanctum | ✅ | P | arena | 29 | 281/321/17 | Red / twilight dragonflight | Pinnacle arena. |
| 43 | Wailing Caverns | ⚠ | A | L | 311 | 506/884/90 | Druids of the Fang, raptors | Maze with dead ends; poor flow. Later, with a hand-made route. |
| 48 | Blackfathom Deeps | ⚠ | A | L | 211 | 674/868/58 | Naga, Twilight cult | Lots of deep water; pulls in water are messy for bots. Later. |
| 90 | Gnomeregan | ⚠ | A | L | 400 | 522/859/183 | Troggs, leper gnomes, mechanical | Very vertical with ramps. Later. |
| 575 | Utgarde Pinnacle | ⚠ | B | M | 110 | 347/311/45 | Vrykul, Ymirjar | Skadi gauntlet needs harpoons/drakes: exclude. Ymiron and Svala rooms fine. |
| 601 | Azjol-Nerub | ⚠ | B | 2 wings | 92 | 182/544/605 | Nerubians (Scourge) | Deep vertical shaft with one-way drops. Wings: Krik'thir level, Anub'arak arena. |
| 604 | Gundrak | ⚠ | B | M | 201 | 377/363/68 | Drakkari | Gal'darah bridge is a GO (unreachable on stock mmaps): use another arena. |
| 534 | Hyjal Summit | ⚠ | C | XL | 173 | 1453/2811/789 | Scourge, Burning Legion | Huge outdoor slope. Good Scourge Rift / siege map; needs a hand-made route. |
| 560 | Old Hillsbrad Foothills | ⚠ | C | XL | 522 | 1134/1450/105 | Humans, orcs (Durnholde) | Huge outdoor area; open-field maps later. |
| 548 | Serpentshrine Cavern | ⚠ | D | L | 273 | 806/905/95 | Naga | Lifts and water platforms; parts unreachable without GO geometry. Later. |
| 603 | Ulduar | ⚠ | D | 3 wings | 984 | 3700/3218/190 | Iron dwarves, Old God | Siege area needs vehicles; Mimiron needs the elevator GO: both excluded. Wings: Antechamber, Keepers, Descent. |
| 631 | Icecrown Citadel | ⚠ | D | 4 wings + arena | 397 | 5321/5801/1042 | Scourge, Cult, San'layn | Strip Chill of the Throne (spell_area 69127). Gunship excluded; Rampart elevator is a GO, so wings are entered by module teleport. Wings: Lower Spire, Plagueworks, Crimson Hall, Frostwing Halls; Frozen Throne = pinnacle. |
| 578 | The Oculus | ❌ | - | - | 114 | 425/422/295 | Blue dragonflight | Floating platforms need drake vehicles. |
| 616 | Eye of Eternity | ❌ | - | - | 1 | 7/92/30 | Blue dragonflight | Single floating disc; needs vehicles. |

Totals: 61 usable, 11 usable with work, 2 excluded. Spans include outliers (ICC and Hyjal spans include
far-away spawns), so they are a size hint, not a measurement.

### Battleground and arena maps (no creature spawns; later phases)

| Map | Use | Notes |
|---|---|---|
| 529 Arathi Basin, 489 Warsong Gulch, 30 Alterac Valley | Open-field maps (PoE-style outdoor maps). | Anchors placed by hand or by a navmesh sampler (no spawns to harvest). Bases and towers are terrain/WMO, not GOs, so they stay. |
| 566 Eye of the Storm | ⚠ | Floating island: edges are falls. Leash or cut. |
| 607 Strand of the Ancients, 628 Isle of Conquest | ⚠ | Gates, walls and boats are GOs/transports; only the land parts work. |
| 559 Nagrand, 562 Blade's Edge, 572 Ruins of Lordaeron, 617 Dalaran Sewers | Pinnacle / Argent Trial arenas. | Small, flat, good sight lines. |
| 618 Ring of Valor | ⚠ | Moving floor and pillars are GOs. |

Not usable: 44 (Old Scarlet Monastery, no data), 169 (Emerald Dream test map), test maps 13/25/42.
Curiosity: 37 (Azshara Crater, unfinished battleground) has terrain; check in the client before planning on it.

### Open-world clones (idea, phase 4 or later)
A continent clone (`Directory` = Northrend etc.) with a **leash**: a run covers one area, and leaving its
bounds teleports the player back. Server cost is only the grids players stand in. WMO towns are part of the
terrain, so a "ruined Icecrown" or "Scourge-held Stormwind" needs no new assets. Risks: huge mmaps per clone
ID (symlinks, so no disk cost, but the navmesh object is per map ID), weather and zone-keyed spells, flying.
Not planned; listed so the layout interface keeps it possible.

---

## 4. Zone-keyed side effects (Q2 audit, from `spell_area`)

A clone reports its source's zone and area IDs, so `spell_area` rows for those zones apply inside clones too.
Rows found for instance zones (base dump, 2026-11-22):

| Zone (source) | Spell(s) | Autocast | Effect on a clone | Action |
|---|---|---|---|---|
| 4812 ICC | 69127 | yes | Chill of the Throne (−dodge) on every player. | Strip on enter (or keep as a deliberate ICC-clone affix). |
| 4812 ICC | 72293 | no | Not auto-applied. | None. |
| 4100 CoS, 2367 Old Hillsbrad | 35480–35483 | yes (by race/gender) | Players are disguised as humans. | Strip on enter. |
| Northrend 5-mans (206, 1196, 4100, 4196, 4228, 4264, 4265, 4272, 4277, 4415, 4416, 4494, 4723, 4809, 4813, 4820) | 57940 | yes | Essence of Wintergrasp buff when your faction holds Wintergrasp. | Strip on enter (keeps runs fair). |
| 1196 Utgarde Pinnacle | 47546 | yes | Unknown zone aura. | Check the spell in the client DBC; strip unless harmless. |
| 3607 SSC | 37280 | yes | Unknown zone aura. | Check; strip unless harmless. |
| TBC raids/5-mans (3606, 3607, 3845, 3847, 3848, 3849, 3959, 4075) | 41604–41607, 46838, 46840 | yes, gated by `aura_spell` | Only applies when the player already has a matching aura. | None expected; verify. |
| 3923 Gruul | 40577–40588, 40763 | yes, gated by `aura_spell` | Same. | None expected. |
| 4075 Sunwell | 45374 (gated), 70193 (quest-gated) | yes | Same. | None expected. |
| Hellfire zones 3562/3713/3714/3836 | 32096, 32098 | no | Not auto-applied. | None. |
| 3457 Karazhan, 3805 Zul'Aman | 30550–30567, 43816–43822 | no | Not auto-applied. | None. |

**Implementation:** an `atlas_zone_strip` list (spell IDs per source map) applied on map enter and on every
zone/area update inside a run (the server re-applies `spell_area` auras on area change, so stripping once is
not enough). Graveyard choice and area flags are handled separately (design sections 7 and 24).

---

## 5. Per-clone checklist (when adding a map)

1. `atlas_map_source` row: clone ID (next free in 900–949), source ID, band, size, native theme, enabled = 0.
2. Client patch: `Map.dbc`, `MapDifficulty.dbc`, `Light.dbc` rows (design section 17).
3. Server: `map_dbc`, `mapdifficulty_dbc`, `instance_template` rows (one SQL file, always together).
4. Symlinks for the clone ID (`maps/`, `vmaps/`, `mmaps/`), generated by the tool.
5. Harvest → review → freeze layout (design section 6.1). Mark GO-only and vehicle-only parts as cut.
6. Zone-strip list for the source zone (section 4 above).
7. Native theme curation (pool appendix) for the source's own creatures.
8. Test run on the test stack with bots, then enable = 1.
