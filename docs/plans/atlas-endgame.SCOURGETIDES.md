# Atlas: Scourgetides (SCOURGETIDES)

Written 2026-11-28. Direction only; nothing built. Supersedes BACKLOG B2 (outbreaks) for the open world; B6
(zone corruption) and B7 (zones linked to maps) can come later on top.

The user: "Scourgetides should leverage the phasing system and swarm players (or maybe hordes should come/hunt
the player depending on what's best)". Model: Diablo IV's Helltide, a timed takeover of a zone where every
player there fights scaled demons, gathers a currency and opens chests.

## 1. Facts this rests on (checked 2026-11-28)

- **Phasing is aura-driven.** `SPELL_AURA_PHASE` (261) gives the target the OR of its phase auras' masks
  (`AuraEffect::HandlePhase`, SpellAuraEffects.cpp:1949). No phase aura means `PHASEMASK_NORMAL` (1). The
  server sends the client `SendSetPhaseShift`. A GM with GM mode on sees all phases.
- Pets, guardians and summons follow their owner's phase (`Unit::SetPhaseMask`, Unit.cpp:15010).
- `UnitScript::CanSetPhaseMask` exists (a veto hook). `OnPlayerUpdateZone(player, zone, area)` exists.
- **Free phase bits:** bits 0-8 are used by stock phasing (DK start, Wrathgate, Icecrown...). From bit 16 up,
  the only spawns are the "every phase" ones (302 creatures, about 7,000 gameobjects: masks like 0xFFFFFFFF).
  A high bit (e.g. bit 29) is free on every map. Check per map before picking one.
- The module already owns server spell rows (spell_dbc, 91001), and the client patch can now add a Spell.dbc
  row (icon and tooltip for the tide aura).
- Stock assets: game event 17 "Scourge Invasion" and its sub-events 120-130 (necropolises, attack zones), the
  Atlas Scourge pool (147 creatures by pack role), the affix handlers, `SMSG_OVERRIDE_LIGHT`. Outdoors there is
  no WMO fog to fight (CRAZY_CLONES), so a plague sky shows.

## 2. Phasing: an overlay, not a replacement

A player in a tide zone gets a tide aura with mask **1 | T** (T = the tide bit):

- They still see the whole normal world (phase 1): quest givers, flight masters, other players.
- Tide creatures spawn with mask **T only**: only players in the tide see them, fight them, loot them.
  Players outside the tide (other zones, or opted out) never meet them.
- Players in the tide and outside it still see each other (both masks hold bit 1).
- The party's bots get the same aura (they are Players); their pets follow.

Leaving the zone removes the aura. The overlay breaks no stock quest, since phase 1 never goes away.

**Fallen places** (later, curated only): inside a few listed sub-areas (a burning village), the aura becomes
**T only**. Phase-1 creatures there disappear for tide players and Scourge-held versions take their place.
BACKLOG BQ2 applies: audit each place for quests and scripts first.

## 3. Swarm and hunt: both, driven by a threat meter

Static packs alone make you go looking for the fight. Pure hunting relies on pathing across open terrain
(cliffs, water, houses), and that will sometimes fail. Diablo IV's answer fits both: a per-player threat meter.

- **The tide's ground:** ambient Scourge packs in a ring 40-90 yd around each real player in the tide, a few at a
  time, capped per player and per zone, despawned when nobody is near. Built from the population code (packs,
  roles, rares with affixes) on the open-world map.
- **Threat (the "Blight"):** every tide kill raises the player's Blight. At each step a **hunting pack**
  appears out of sight and runs at the player (chase over the zone's navmesh). Packs that can't reach the player
  despawn and come back from another side.
- **At full Blight:** a named **Harbinger** (rare or miniboss role, 2-3 affixes) arrives with a wave. Killing it
  pays out and resets the Blight. Dying resets it too.
- So the player always chooses: farm the ambient swarm calmly, or let the Blight climb for the Harbinger.

The tide ends on its timer. Everything T-phased despawns, and the auras come off.

## 4. Scaling and rewards

- **Level:** the player's own level, clamped to the zone's level band (a level-80 player in Elwynn faces
  level-80 Scourge only if the band allows; open decision S2). The tier comes from the tide's intensity and
  the Blight step, on the CURVE.md curve.
- **Every alt meets them**, at any level. That's the point: the grind mode while levelling.
- **Rewards:** a currency dropped by tide creatures (Helltide's cinders analog), spent on **Plague Caches**
  placed around the zone (T-phased chests), plus kill XP, the Harbinger's drop, and at level 80 sigils and later
  rift keys. Currency drops only for real players (bots don't need it).

## 5. Schedule

- One or two zones at a time, rotating, about 45 minutes each, announced to the zone and on the HUD.
- Only zones with a real player in them get spawns; a tide in an empty zone costs nothing.
- Never in cities, battlegrounds, instances or sanctuaries.

## 6. To verify before building

- Do summoned temporary creatures give kill XP and use the normal loot path at any level?
- Open-world chase pathing: do mmaps exist for every continent tile in the volume, and how often does a chase
  fail?
- Does a module spell row with aura 261 apply and clear cleanly (login, zone change, death, GM mode)?
- Bots: do they attack T-phased creatures that attack their master? Do they follow through the phase change?
- Performance: spawns per player, map updates on continents with many players.

## 7. Open decisions

- S1: zone rotation on a timer (Helltide), or triggered by activity (BACKLOG Director)?
- S2: can a high-level player trigger or join a tide in a low-level zone, and at what level do its creatures
  fight?
- S3: fallen places: worth the curation, or the overlay only?
