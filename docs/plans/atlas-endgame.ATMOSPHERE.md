# Atlas: echo atmosphere catalogue (fog, light, screen effects)

Checked 2026-11-22 against the play server's client data (DBCs copied read-only from the `ac-client-data` volume) and the decompiled 3.3.5a client (Wowdecomp2). Regenerate with `tools/atmosphere.py <dbc dir> tools/`; full data is in `tools/atmosphere_lights.csv` (715 lights) and `tools/atmosphere_screen_effects.csv` (40 effects, 49 carrier spells).

Goal (DESIGN, "Echo atmosphere"): an echo must look unlike the stock dungeon, and the look comes from the modifiers (theme, then depth and affixes).

## 1. How light overrides really work

| Fact | Source |
|---|---|
| `SMSG_OVERRIDE_LIGHT` carries `(lightToReplace, overrideLight, fadeMs)`. The client removes any override keyed on `lightToReplace`, then copies the **override light's params** (fog, sky, colours, skybox) onto a new entry placed at **`lightToReplace`'s position and falloff**. `overrideLight = 0` removes it. | `Ui/GameUI.cpp:6679`, `WorldClient/DayNight.cpp:3219` (`AddLightOverride`) |
| Each frame the override is blended in only when the player stands inside that copied sphere. | `DayNight.cpp:2093-2103` |
| Lights are looked up **by ID only**, not by map: any light of any map can be the look, and any light can be the anchor. | `DayNight.cpp:3220-3221` |
| Global lights (position 0,0,0) are **not** transformed on load: their falloff stays raw and centres on the map origin. Area lights are /36. | `DB/DBClient/Lights.cpp:74-96` |
| The client clears every override when it unloads the map, so leaving the echo resets the sky by itself. | `DayNight.cpp:2353-2359` (`UnloadMap`) |
| AC's `Map::SetZoneOverrideLight` always replaces `_defaultLight` = the map's global light (`GetDefaultMapLight`), or 0 when the map has none, in which case the client ignores it. It stores the override per instance and replays it to late arrivals. | `Map.cpp:72, 3245-3264, 3351-3362`, `DBCStores.cpp:897` |

**Consequence:** the stock API only works where the dungeon's own global light covers the rooms. For the 44 Atlas dungeons:
- 19 have no global light at all.
- Most of the other 25 have one whose sphere (centred on the map origin) is between about 10 and 2,000 yards across, so it covers part of the dungeon at best.

Only Botanica and Azjol-Nerub have one that is sure to cover the whole map.

**Recommended:** the module sends its own `SMSG_OVERRIDE_LIGHT` with an **anchor** light whose untransformed sphere covers any map, and the chosen look as the override. These are global lights with a raw falloff of at least 20,000 yards:

| Anchor light | From | Raw falloff (yd) |
|---|---|---|
| 1826 | Azjol-Nerub | 35,539 |
| 2488 | Icecrown Citadel | 30,830 |
| 1773 | The Eye of Eternity | 30,726 (Malygos' stock override uses it) |
| 515 | Tempest Keep: The Botanica | 28,948 |

Different anchors give independent override slots, so a theme look and an affix look can stack. Deliver the packet on map entry (the allmap script) and when the look changes (depth, affix). Zero core edits: `WorldPacket(SMSG_OVERRIDE_LIGHT)` plus `SendDirectMessage`.

**Open, to test in game first:**
- **Interiors.** Most dungeons are indoor WMOs whose own fog is mixed in (`m_interiorFogBlend`); how much an area-light override still shows indoors is unknown.
- **Test plan:** one indoor echo (Forge of Souls, no lights of its own), one outdoor-ish (Culling of Stratholme), one TBC (Shadow Labyrinth), each with a heavy-fog look, for example 40 or 1673.

## 2. The Atlas dungeons' own lights

Native fog is what an echo looks like without overrides. "No lights" means pure WMO lighting.

| Group | Dungeons |
|---|---|
| No lights at all | Ragefire Chasm, The Stockade, Wailing Caverns, Blackfathom Deeps, Gnomeregan, Uldaman, Maraudon, Sunken Temple, Blackrock Depths, Blackrock Spire, Slave Pens, Underbog, The Nexus, Violet Hold, Gundrak, Forge of Souls |
| Area lights only | Shadowfang Keep, Razorfen Downs, Zul'Farrak |
| Global light, falloff 0 | Deadmines 97, Razorfen Kraul 330, Scarlet Monastery 319, Scholomance 333 (heavy, 111 yd), Stratholme 341 |
| Global light, partial falloff (about 10 to 2,000 yd from the origin) | Ramparts 493, Blood Furnace 612, Shattered Halls 722, the Auchindoun four (639-642), Mechanar 544, Arcatraz 545, Utgarde Keep 1452, Utgarde Pinnacle 1487, Ahn'kahet 1789, Drak'Tharon 1711, Halls of Stone 1835, Halls of Lightning 1836, Culling of Stratholme 1607, Pit of Saron 1971, Halls of Reflection 2504 |
| Global light covering the map | Botanica 515, Azjol-Nerub 1826 |

## 3. Look palette (override lights)

Fog end is at noon, in yards: lower means heavier. Colour is the fog colour. Test and dev maps are excluded. "Zone" lights are world area lights, named by their position in WorldMapArea.

### By Atlas theme

| Theme | Light | Where it's from | Fog end | Fog colour | Notes |
|---|---|---|---|---|---|
| **Echo signature (bronze, time)** | 84 | Opening of the Dark Portal | 111 | #231C25 | Caverns of Time, smoky |
| | 730 | Escape From Durnholde | 139 | #234A48 | dark teal haze |
| | 1607 | Culling of Stratholme | 333 | #694644 | rust red |
| **Scourge corruption** | 27 | Western Plaguelands | 181 | #231C25 | grey ash |
| | 43 | Eastern Plaguelands | 222 | #4E300F | sick amber |
| | 1817 | Icecrown Glacier | 250 | #271E1D | grey-brown |
| | 2508 | Icecrown Citadel | 278 | #002E40 | deep frost teal |
| | 333 | Scholomance | 111 | #231C25 | heavy, near-black |
| | 1673 | Culling of Stratholme | **25** | #694644 | the heaviest fog in the game, for the deepest tiers |
| **Burning Legion** | 745 | Shadowmoon Valley | 389 | #3B170F | dark red |
| | 592 | Black Temple | 389 | #201E0E | olive black |
| | 670 | Netherstorm | 361 | #372333 | dark magenta |
| | 214 | Felwood | 389 | #193C19 | fel green |
| **Void, shadow, nightmare** | 371 | Zul'Gurub | 167 | #2B0A32 | dark violet |
| | 305 | Desolace | 250 | #420A52 | violet |
| | 189 | Emerald Dream | 361 | #1E003A | nightmare purple |
| **Frost** | 56 | Hillsbrad | 111 | #003441 | dark teal, heavy |
| | 61 | Alterac | 139 | #306765 | teal |
| | 1703 | Icecrown Glacier | 1722 | #003E55 | light, clear frost |
| **Mist, night** | 40 | Duskwood | **42** | #231C51 | near-blinding night fog |
| | 2 | Duskwood | 181 | #00759D | blue mist |
| | 1773 | Eye of Eternity | 417 | #11132D | starry dark blue |
| **Fire, dusk** | 223 | Durotar | 83 | #7D2800 | burnt orange, heavy |
| | 1832 | Zul'Drak | 194 | #522F1E | dark orange |
| | 1769 | Obsidian Sanctum | 417 | #3B2625 | dark red |
| | 2538 | Ruby Sanctum | 333 | #523830 | ember |
| **Wild, beasts** | 248 | Un'Goro Crater | 222 | #60A267 | jungle green |
| | 1374 | Grizzly Hills | 278 | #426533 | forest green |
| | 241 | Feralas | 389 | #8A844E | sickly yellow |

**Depth intensity:** within a theme, step down fog end as depth rises. For the Scourge, for example: 1817 (250) → 27 (181) → 333 (111) → 1673 (25). The full sorted list per colour family is in the CSV (`fog_tint`, `fog_end_noon_yd`).

## 4. Screen effects (ScreenEffect.dbc, aura 260)

| Fact | Source |
|---|---|
| The client applies the first aura 260 it finds on the player, reading the spell from **its own** Spell.dbc. A server-only `spell_dbc` row does nothing. The aura must be sent (not passive). | `Ui/WorldFrame.cpp:1370-1403` |
| Effect types: 1 = death (grey world), 2 = netherworld fog (`SetOverrideFog`), 3 = swirling-fog shader (param0 = ARGB colour) or glow on old hardware. A row may also override LightParams, ambience and zone music. | `Ui/WorldFrame.cpp:1294-1353` |

### Carriers that are safe to apply

No other effects, and no phase aura (261) or stat changes.

| Effect | Look | Carrier spell | Duration | Icon |
|---|---|---|---|---|
| 161 CoT Stratholme - Time Warp | bronze swirl #FF8C6D00 | 52767 "Screen Effect Test" | 10 s | **shows**, with that name |
| 43 Zombie-Vision | red swirl #C7FF0000 | 50224 "Zombie-Vision!", 63752 "Low Sanity Screen Effect" (5 s) | -1 / 5 s | shows |
| 21 Arthas Memory Events | black swirl | 50153 "Haunting Memories" | -1 | shows |
| 41 / 602 Mist of the Kvaldir | white mist | 50203 / 68085 "Mist of the Kvaldir" | -1 | shows |
| 362 Icecrown - Naz'anak | dark, LightParams 3 | 59533 "Naz'anak Screen Filter" | -1 | shows |
| 382 Infra-green Vision | green swirl, LightParams 0 | 61358 "Infra-green Vision, Visual Only" | -1 | shows |
| 744 Swarming Shadows | purple swirl | 72985 "Swarming Shadows" | 8 s | shows |
| 61 Jungle Madness | green-yellow swirl | 50010 "BOTM - Jungle Brew - Jungle Madness" | 17 s | shows |
| 101 Eye of Acherus | heavy black | 57889 "Using the Eye of the Lich King" | 120 s | shows |

Avoid: carriers with aura 261 (phase: would phase the player out of the echo), 193/33 (haste, slow), 23 (periodic trigger), 36 (shapeshift), 105, 139, and effects 36 or 28 (learn spell, summon).

**The catch:**
- **Every safe carrier shows a buff icon** with its own spell name ("Screen Effect Test", "Naz'anak Screen Filter"…). That breaks the lore.
- The only no-icon carriers (attribute 0x80) are phase auras or the music-only Cataclysm-prologue cues (78000-78002).
- Durations can be made unlimited server-side (`AddAura`, then `SetMaxDuration(-1)` and `SetDuration(-1)`), but the name can't change without a client patch.
- Whether 0x80 hides the icon comes from AC's attribute docs; not confirmed in the decompiled buff frame.

**Recommendation:**
- **V1:** light overrides only (no icon, no client patch), plus per-instance zone music (`Map::SetZoneMusic`) where it fits a theme.
- **Screen effects go to the client-patch phase (DESIGN §17).** The patch would add module-owned no-icon Spell.dbc rows carrying aura 260, and module ScreenEffect.dbc rows (our own colours for the swirl, for example the bronze Time Warp as every echo's signature). Unpatched clients just don't see the effect.
- **Until then:** a tiny bit of the AtlasChronicle addon could hide those buff icons, if we want effects earlier.

## 5. Why overrides look stock indoors (2026-11-24, clone 902 run)

- The user saw the stock Deadmines look in clone 902 although the override (light 189 over anchor 1826) was sent.
- `DayNight::UpdateFog` (Wowdecomp2 `WorldClient/DayNight.cpp:2117-2230`): inside a WMO, `World::QueryCameraFog`
  gives the WMO's own fog (`MFOG`) and it is blended over the light's fog by `distance * 0.04`, so about 25 yards
  in, the WMO fog wins completely. Screen-effect fog type 2 (`SetOverrideFog`) goes through the same blend.
- Deadmines: the WDT has no global WMO; 36 ADT tiles place `AZ_DEADMINES_B.WMO` (44 groups, 5.4 MB) and
  `AZ_DEADMINES_D.WMO` (7 groups, 0.4 MB), each with 2 `MFOG` entries (interior: end 111 yd, start x0.20).
- `ScreenEffect.m_lightParamsID` is a slot index 0-7 of the current light (`SetOverrideLightParamsID`), not a
  LightParams row: no custom fog that way.
- Routes to a distinct look inside a clone:
  1. Screen effect type 3 (swirling-fog shader, ARGB colour from `m_param`): a post-process on top of the scene,
     so WMO fog doesn't hide it. Needs patch-T rows in ScreenEffect.dbc and no-icon aura-260 spells in Spell.dbc
     (whole file, about 49 MB) plus server `spell_dbc` rows. Per-run colours (theme, affix).
  2. The clone's own WMO fog: a new Map.dbc Directory for the clone, its own WDT and 36 ADTs whose `MWMO` names
     point at renamed copies of the two Deadmines WMOs (root with new `MFOG` colour and distance, groups copied
     under the new name, about 6 MB). True fog in the world, fixed per clone; the stock Deadmines is untouched.

## 6. Fogs in the patch (route 1, built 2026-11-24)

- `tools/clones/fogs.json`: 15 fogs (spells 90400-90414, ScreenEffect 900-914), one per theme, corruption 3-5 and
  the two weather affixes. Each spell is a copy of 50203 (Mist of the Kvaldir) with our name and tooltip (the buff
  shows with that name: the client's aura list has no hidden-attribute filter, `PushUnitAura`,
  Wowdecomp2 `Ui/ScriptEvents.cpp:1043`), plus `AURA_INTERRUPT_FLAG_CHANGE_MAP` (AC removes it on far teleports
  and before the logout save, `WorldSession.cpp:771`).
- Swirling fog parameters (`EffectSwirlingFog::SetParameters`, `FFXEffects.cpp:604`): param0 seed colour ARGB,
  param1 / 255 propagation, param2 % blend. Stock: Kvaldir FFFFFFFF/6/40, Time Warp FF8C6D00/8/40.
- Unknown spell IDs in the aura list are skipped (`UpdateScreenEffect`, `Ui/WorldFrame.cpp:1371`): an old patch shows
  nothing, no crash. The shader needs a device API (`SetScreenEffect` case 3), else the glow effect.
- Module: `atlas_fog` picked like `atlas_atmosphere` (affix fog wins), applied in `SendAtmosphere` to real players
  who opted in, on the run's map only; `.atlas fog list|off|<id>` to try them; login clears a fog saved by a crash.

**Route 1 does not render on the user's client (2026-11-24).** The user plays the Wowdecomp2 native macOS build,
whose Gx device is OpenGL (`CGxDeviceOpenGL`, `GxApi_OpenGl = 0`). `CGWorldFrame::SetScreenEffect` case 3 only
draws the swirling fog when `DeviceApi()` is non-zero, else it sets the glow effect (`Ui/WorldFrame.cpp:1337`). First
run (Razorfen Kraul, fog 90404 sent): nothing visible. Lesson: check the user's renderer before any client-side
visual. What renders on OpenGL and indoors: a WMO's own `MFOG` (route 2), including WMOs placed as gameobjects
(type 14 MAP_OBJECT, `CWorld::ObjectCreate` makes a `CMapObjDef` for `.wmo` models, `WorldClient/World.cpp:5221`),
which is also what kit maps (DESIGN 6.2) would use.
