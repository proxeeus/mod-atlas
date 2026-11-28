# Atlas: crazy clones (CRAZY_CLONES)

Started 2026-11-27, after generated maps were scrapped (mod-atlas b446a35; TILESET.md, STAGE2.md §10). The user's
direction: "focus on actual dungeon clones but where we go batfuck crazy with modifiers, themes, mobs, fog".

## Principle
Real Blizzard dungeons (clone maps 900-949: CLONES.md, DESIGN §6.1) always look right. Every run of a clone is
made different by what fills it and how it feels, not by its walls: Path of Exile's maps work the same way. Stage 1
already has the machinery (map runs from sigils, affixes, themes, Scourge corruption, populations from harvested
layouts, bosses, rewards); stage 2 has the clone plumbing (clone 902 Echo of the Deadmines works in game).

## Pillars
1. **Atmosphere you can see** (the gap so far: the screen-effect fogs never render on the user's OpenGL client).
   - Each clone map gets its own Light rows (lightdata.py, now generic): sky, ambient, fog colour per theme.
   - Thick fog rules, from the client code (Wowdecomp2 DayNight / CMapEntity::QueryCameraFog):
     map fog is a real fog only with a fog end under 27.8 yd (longer turns into a thin haze over the draw distance);
     inside a WMO the WMO's own fog wins unless the WMO has exactly ONE fog entry.
   - Clones reuse the source's WMOs, so to let the clone's map fog reach inside them the patch carries those WMO
     root files with a single fog entry (their stock global fog). Only the user's client sees that change; the real
     dungeon keeps its own map light, so it looks almost as before. Check per dungeon.
   - Several looks per theme (fog end, colour, ambient): rolled per run when a clone has more than one light set,
     or one clone ID per look (IDs 900-949 are plenty).
   - Each clone its own zone (AreaTable row, as the Timeways had): name ("The Timeways" was liked: candidate for
     the umbrella zone name of all echoes), music, ambience, no rest flags.
2. **Modifiers, a lot more of them** (AFFIXES.md): stacking affixes on packs, elites and bosses; map-wide modifiers
   (darkness, volatile dead, mirror images, bloodlust...); player-side curses with bigger rewards; rarity tiers and
   corruption pushing counts up. Rolled on the sigil, shown before the run.
3. **Themes** (THEMES.md, SCOURGE.md): which creature families fill a dungeon (native, pool, Scourge corruption),
   tied to the atmosphere (an undead Deadmines in a green plague fog).
4. **Mobs**: pack variety, named elites and champions with their own affixes, boss swaps (bosses from other
   dungeons), invasions (encounter modules, DESIGN §12).
5. **More clones**: enable the starter set (900 DTK, 901 Stratholme, 903 Halls of Lightning, 904 Shadowfang next
   to 902) and grow the catalog by band (CLONES.md).
6. **Inside-the-walls variety** (later): sealed wings, alternative entrances and boss arenas per run.

## Milestones (proposed)
- M1 Visible atmosphere: clone 902 with its own light set and thick themed fog that shows inside the mine (one-fog
  WMO copies), its own zone; then the other starter clones.
- M2 Starter clones live: 900, 901, 903, 904 (data links, rows, harvest check), each with a look.
- M3 Modifier overhaul: many more affixes and map-wide modifiers, rarity/corruption scaling, sigil display.
- M4 Mobs: elites/champions, boss swaps, invasions.
- M5 Catalog growth by band.

## Kept from the generated-map work
lightdata.py (Light / LightParams / bands), wmofloor.py (WMO floors), the WMO and DBC readers/writers, and the
lessons: one MFOG entry, fog end < 27.8 yd, MOGP groupLiquid 15 = none, unculled materials, server .vmo format.
