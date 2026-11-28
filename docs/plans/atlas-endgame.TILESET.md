# Atlas generated maps: our own tileset (TILESET)

Started 2026-11-25 after the two-mine joint (mod-atlas ec1ab83) showed stock art cannot close a joint: no stock
tunnel pieces have server collision, the 40Man AQ WMOs are blockouts, stock doorways share no size (kit survey,
STAGE2 §10.x, shared handover note). User chose "build own tileset".

## Goal
Diablo / PoE style random layouts: a set of pieces that all share one doorway, so any opening fits any other.
Each piece is a WMO we generate (Python), shown in game as a gameobject (type 14) and collided by the server
through a collision model we also generate. Layouts come from AtlasKit.h (sockets at doorways, KitBuildLayout).

## Standard doorway (v1)
Mine tunnel cross-section, Goldmine style: 8 yd wide, 7 yd high, floor at local z = 0. A socket sits on the
floor at the centre of the opening, facing out. Pieces are built on a 2 yd grid; lengths in multiples of 4 yd.

## Pieces (v1 list; milestone 1 = corridor + room)
- corridor_straight (8 x 20, sockets at both ends)
- corner (90 degrees), tee, cross
- room_small (24 x 24, doorways on any of 4 sides: variants with 1-4 doorways)
- dead_end (cap)
Later: stairs/ramps (z change by 4 yd), large hall, boss room, props (stock M2 doodads: beams, lanterns).

## Client side (patch-T.MPQ) — facts from Wowdecomp2
- Root WMO chunk order is fixed and untagged-checked: MVER(17) MOHD MOTX MOMT MOGN MOGI MOSB MOPV MOPT MOPR MOVV
  MOVB MOLT MODS MODN MODD MFOG [MCVP] (MapObjRead.cpp CMapObj::Load).
- Group file: MVER MOGP{68-byte header, then MOPY MOVI MOVT MONR MOTV MOBA, then by flags: MOLR 0x200, MODR 0x800,
  MOBN+MOBR 0x1, MOCV 0x4, MLIQ 0x1000, ...} (CreateDataPointers / CreateOptionalDataPointers).
- Structs: SMOHeader 64 B, SMOMaterial 64 B, SMOGroupInfo 32 B, SMOBatch 24 B, SMOPoly 2 B, CAaBspNode 16 B
  (flags u16 [0/1/2 axis, 4 leaf], neg u16, pos u16, nFaces u16, faceStart u32, planeDist f32), MOGP header 68 B.
- BSP digest cache handles at most 1024 faces per leaf: split the tree.
- Groups must be EXTERIOR (0x8): a gameobject WMO shows interior groups only when the camera is inside them or
  sees them through a portal, so a doorway into the next piece would look into black.
- A missing WMO file falls back to world\wmo\Dungeon\test\missingwmo.wmo (no crash).
- Textures: reuse the Goldmine room's (DUNGEONS\TEXTURES\ROCK\MM_CAVEWALL_08/09, FLOOR\MM_CAVEFLOOR_03,
  WOOD\MM_MINEBEAM_01/03, FLOOR\MM_MINEPLANKS_01).
- GameObjectDisplayInfo.dbc rows (19 fields: ID, ModelName, 10 sounds, geo box 6 floats, effect package):
  new IDs 9900-9949 (client max 9624 incl. the user's own patch).

## Server side
- gameobjectdisplayinfo_dbc rows (same IDs) and gameobject_template entries 900100-900199 (atlas_pool.sql).
- Collision: one .vmo per piece in the client-data volume's vmaps/, and an entry per display in
  vmaps/GameObjectModels.dtree (read once at start). The dtree is a stock file: back it up, replace it with stock
  entries + ours, keep an undo (like link_data.sh: dry run / --apply / --remove). Format: see §Server formats.
- Pieces need a navmesh later (mmaps for map 950) — after the first walkable layout.

## IDs
GO entries 900100-900199 (kit), displays 9900-9949, WMO paths World\wmo\Dungeon\AtlasKit\<piece>.wmo (+ _000).

## Milestones
1. corridor + room generated, in patch-T, collision installed; `.atlas spike` builds room-corridor-room;
   walk the whole layout without falling or clipping. (Goldmine stays in the kit file but not in layouts.)
2. corner/tee/cross/dead end; layouts of 6-10 pieces; overlap checks hold.
3. Props and lighting; navmesh; monsters on the layout (hook into map runs).

## Server formats (AzerothCore src/common/Collision, researched 2026-11-25)
- dtree: "VMAP_4.8", then entries to EOF: u32 display, u8 isWmo, u32 nameLen (<500), name (no NUL), f32 lo[3] hi[3]
  (model-space AABB, must enclose all geometry, non-zero). First entry per display wins. Read once at start.
- model: DataDir/vmaps/<name>.vmo, name verbatim from the dtree incl. extension (Foo.wmo -> Foo.wmo.vmo).
- .vmo: "VMAP_4.8" "WMOD" u32 8 u32 rootWMOID, "GMOD" u32 nGroups, groups, "GBIH" BIH(group ids).
  group: AABox, u32 mogpFlags (0x8 outdoors), u32 groupWMOID, "VERT" u32 size u32 n f32[3][n], "TRIM" u32 size
  u32 m u32[3][m], "MBIH" BIH(tri ids), "LIQU" u32 0. Triangles are two-sided for the server.
- BIH: f32 lo[3] hi[3], u32 treeSize, u32 tree[], u32 count, u32 objects[]. One leaf = [0xC0000000|first, n, 0].
- isWmo=1: MOD_WORLDSPAWN, and only WMO gameobjects give floor/area info (GetLocationInfo).

## Status (2026-11-25)
- kitwmo.py / kitvmo.py / kit.json (room 24x24 with 4 doors, corridor 20, cap) built and self-checked; patch carries
  them with displays 9900-9902; atlas_pool.sql has display rows, templates 900100-900102, role column.
- AtlasKit.h: Cap pieces, KitOpenSockets, KitCapLayout (69 tests). ProcessPoolMap caps every open doorway.
- Goldmine room retired from layouts.
- Next (user): Docker up -> world backup -> kit_server.py dry run -> OK -> --apply -> rebuild -> copy patch ->
  `.atlas spike`.

## Mix and match (user, 2026-11-25)
Realism path chosen: BOTH, mixed in one layout. Pieces only meet through the standard doorway, so the builder
mixes them freely:
- **Real rooms**: single groups cut out of stock dungeon WMOs (Blizzard geometry, textures, doodads), each shipped
  as its own WMO (root rebuilt around the one group: its materials, its doodads, no portals) with server collision
  from its real triangles. Each of the room's doorways (portals to groups we dropped) gets a generated **adapter**:
  a short passage lofted from the portal's own outline to the standard 8x7 opening, ending in a socket. Openings
  too odd for an adapter are sealed (cap). Prefer cave/mine art (matches the generated passages).
- **Generated passages, dressed**: corridors, corners, tees, crosses, caps; rough rock (noisy cross-sections),
  support frames, stock M2 props (WMO doodad sets), baked vertex lighting (MOCV), the tileset's own WMO fog
  (MFOG, works on the user's OpenGL client) for per-map atmosphere.
Order: room survey -> WMO splitter (one group -> piece) -> adapter loft -> collision from real geometry -> in game;
then passage dressing (rock, frames, props, light, fog).

### Room survey (2026-11-25; tool: .agents/plans/atlas-endgame/tools/roomsurvey.py <root wmo> ...)
451 dungeon root WMOs in the client. Best sources (doorway = portal width x height, yd):
- MD_Goldmine (10 groups): 9 interior rooms, 1-3 doors, mostly 8-9 x 7-8 (≈ our 8x7), doodads inside (g0 17,
  g4 12, g7 13). Same art as our pieces. g5 = dead end (5x6 door), g4/g6/g8 = 3-door junction rooms.
- MD_Caveden dens (Ice, Jungle, Mushroom, Troll, Volcanic, Vrykul, Warm...): one interior room + one 12x10 door
  each -> themed dead-end rooms.
- MD_Dwarven Tunnels (Small/Medium/Large): modular segments, standard 11x11 doors (Medium).
- MD_Hordemine, MD_Goldmine_variantA-F: more rooms; variantB has 84x67 to 99x79 halls (boss rooms).
- Also MD_CaveTunnels, MD_Mountaincave (Icecave01-07, corrupt, hellfire).

### Key design point: keep real rooms INTERIOR, joined to their adapters by real portals
Stock rooms are interior groups lit by their vertex colours (MOCV). A gameobject WMO shows an interior group only
when the camera is inside it or sees it through a portal of the same WMO. So a real-room piece = the stock interior
group + one generated EXTERIOR adapter group per doorway (inside the same WMO), linked by the room's own portal
polygon (MOPT/MOPV/MOPR). From any other piece the camera sees the adapter (exterior), and through the portal the
room, exactly as the stock Goldmine shows its inside through its mouth. Blizzard lighting stays intact.

### 2026-11-25 evening: cut rooms are leaky; fog lives in the WMOs
- In game after 18f3598/4503256: seams still large where cut rooms' irregular walls meet our pieces; the new map
  light showed no fog because inside a WMO the client uses the WMO's MFOG (fixed in a6e589f: every kit WMO carries
  the pool fog).
- Lesson: cutting single groups leaves holes wherever a removed neighbour provided walls/floor/ceiling. Patching
  (collars, shell slabs, longer adapters) cannot close them all.
- Proposed pivot, "real wings": use stock WMOs WHOLE (every interior group, portals between them intact), drop only
  their exterior groups, and put adapters at the portals that led outside (the mouths). A mine/den/cave interior is
  closed by design, so the only joins are its mouths. Candidates: MD_Goldmine (9 rooms, 1 mouth), Goldmine
  variants A-F, MD_Hordemine (1 mouth), Caveden themes (1 mouth each), Mountaincave Icecave01-07, Dwarven tunnels
  and MD_CaveTunnels (2 mouths: through pieces). Layouts: a hub of clean generated rooms/corridors, real wings as
  dead ends and through-tunnels. Cut single rooms leave the kit.

### Wing survey (2026-11-25; .agents/plans/atlas-endgame/tools/wingsurvey.py -> atlas-endgame.wings.tsv)
787 WMOs with interiors; 329 usable as wings (1-6 mouths 4-24 x 4-20 yd, footprint 15-400 yd); 73 with 2+ mouths
(through-wings, the backbone of sprawling layouts). Themes (through = 2+ mouths, wing = 1 mouth):
- Deep Caves: through MD_CaveTunnel01 / MD_WarmCaveTunnel01 (12x10, 65 yd), BladesEdgeOgreTunnel (300x124),
  Dwarven Small/MediumTunnel (6x8, 113/184 yd); wings MD_Mushroomcave, MD_Warmcave, MD_Junglecave(+Crystal,
  Medium) (12 groups, 213x161), Caveden dens. Blizzard's MD cave kit shares a 12x10 mouth.
- Frozen Depths: through Small/MediumTunnel_Snow, MD_ColdCaveTunnel01; wings MD_Icecave01-07, MD_IceDen.
- Barrows & Crypts: wings BarrowDens / CorruptBarrowDens (32 groups, 227x166), BarrowDensOneRm,
  Ruinedkeep_crypt_instance (21 groups, 380x307); through Undead_RogueTemple01 (6 mouths), Undead_SkullTower01.
- Mines: through MD_Goldmine_variantB (9 groups, 3 mouths, 232x204), variantC (3 mouths, 398x375); wings
  MD_Goldmine, MD_Hordemine, MD_Spidermine (kobold), AZ_Deadmines_A (37 groups).
- The Hive: MD_Hive mini-hive_D / _albino (6 groups, 3 mouths 18x17, 223x188) + other MD_Hive pieces.
- Ogre Mounds: wings MD_Brogremound / MD_Grogremound (171x131); through BladesEdgeOgreTunnel.
- Troll Temples: through woodtroll_temple (128x260), Zul'Aman temples, troll_hotel01 (4 mouths); wings Junglecaves.
- Nerubian: through ND_Nerubian_Temple05 (3 mouths); wings Temple01-06.
- Blackrock / Fel: through Blackrock2, BlackrockOrcGreatHall (3 mouths); wing BlackrockOrcFortress (20 groups).
Idea: per-theme doorway standard (caves 12x10, Dwarven tunnels 6x8) so adapters are nearly straight in a family.

### 2026-11-27: Deep Caves, the first theme (real wings)
- wing.py: whole stock WMOs as pieces (interior groups + internal portals kept, exterior dropped, adapters at
  mouths; MOPR rebuilt contiguous per group). kit.json has a "themes" section (door, textures) merged into pieces;
  pool map has a theme; the server builds each pool map's layouts from its theme's pieces only (PoolMap::Kit).
- Deep Caves (door 12x10, JLO_MCAVE textures): generated Cross32, Hall48, Tee24, Corner24, Tunnel20/40, Cap;
  wings: cave tunnels x2, ogre tunnels I-IV, Dwarven Small/Medium/Large tunnels, Mushroom/Warm/Jungle/Crystal/
  Jungle-medium caves, 6 dens. Ogre Tunnel V left out (reaches 750 yd from its origin; Infinite visibility = 533).
- Layout sim (30 seeds, max 40): median 40 pieces, 16 wings, ~1.7 km x 0.4 km, z -85..56; a few seeds stall ~10.
- Server: pieces Infinite visibility; void limit per instance = lowest piece z - 150.
- Old 8x7 generated pieces and cut rooms left the kit (their .vmo files stay in the volume, unused).
