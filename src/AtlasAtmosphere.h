/*
 * mod-atlas — AtlasAtmosphere.h
 * Which sky and fog a run shows (PLAN 1.6, ATMOSPHERE appendix), as pure data
 * so the choice can be unit-tested.
 *
 * The look is an existing client light, sent as an override of an "anchor"
 * light whose sphere covers any map (SMSG_OVERRIDE_LIGHT; the client copies
 * the look onto the anchor's position and clears it when it leaves the map).
 * World table atlas_atmosphere picks it: the run's theme, then its corruption
 * level (the plague's greys take over at high tiers), and separately a second
 * look for an affix (Blizzard-swept frost, Plague-cloud amber) on its own
 * anchor.
 *
 * Fogs (atlas_fog) are picked the same way, with an affix fog winning over
 * the theme's: a swirling-fog screen effect carried by an aura-260 spell of
 * the Atlas client patch, drawn over the scene so a dungeon's own WMO fog,
 * which hides light overrides indoors, can't hide it.
 */

#ifndef ATLAS_ATMOSPHERE_H
#define ATLAS_ATMOSPHERE_H

#include "Define.h"

#include <vector>

namespace Atlas
{

constexpr uint8 ATMOSPHERE_ANY_CORRUPTION = 255;

struct AtmosphereRow
{
    uint32 ThemeId    = 0;                           // 0 = any theme
    uint8  Corruption = ATMOSPHERE_ANY_CORRUPTION;   // corruption level 0-5, 255 = any
    uint32 AffixId    = 0;                           // > 0: an affix look (second anchor)
    uint32 LightId    = 0;
    uint32 MusicId    = 0;                           // SoundEntries id played on entry, 0 = none
    uint32 FogSpell   = 0;                           // atlas_fog rows: the fog spell
};

using AtmosphereField = uint32 AtmosphereRow::*;

// The theme look: the theme at this corruption level, then any theme at this
// level, then the theme at any level, then the default. Affix rows never match.
// `field` is what a row must have: a light (atlas_atmosphere) or a fog spell.
inline AtmosphereRow const* PickThemeLook(std::vector<AtmosphereRow> const& rows, uint32 themeId, uint8 corruption,
    AtmosphereField field = &AtmosphereRow::LightId)
{
    AtmosphereRow const* best = nullptr;
    int bestRank = 0;
    for (AtmosphereRow const& r : rows)
    {
        if (r.AffixId || !(r.*field))
            continue;
        bool const themeMatch = r.ThemeId == themeId && themeId != 0;
        bool const levelMatch = r.Corruption == corruption;
        int rank = 0;
        if (themeMatch && levelMatch)
            rank = 4;
        else if (r.ThemeId == 0 && levelMatch)
            rank = 3;
        else if (themeMatch && r.Corruption == ATMOSPHERE_ANY_CORRUPTION)
            rank = 2;
        else if (r.ThemeId == 0 && r.Corruption == ATMOSPHERE_ANY_CORRUPTION)
            rank = 1;
        if (rank > bestRank)
        {
            best = &r;
            bestRank = rank;
        }
    }
    return best;
}

// The affix look: the first of the run's affixes (in their order) that has one.
inline AtmosphereRow const* PickAffixLook(std::vector<AtmosphereRow> const& rows, std::vector<uint32> const& affixIds,
    AtmosphereField field = &AtmosphereRow::LightId)
{
    for (uint32 id : affixIds)
        for (AtmosphereRow const& r : rows)
            if (r.AffixId == id && r.*field)
                return &r;
    return nullptr;
}

// The run's fog spell: an affix fog wins over the theme's (the client shows
// one screen effect at a time); 0 when no row fits.
inline uint32 PickFog(std::vector<AtmosphereRow> const& fogs, uint32 themeId, uint8 corruption,
    std::vector<uint32> const& affixIds)
{
    if (AtmosphereRow const* r = PickAffixLook(fogs, affixIds, &AtmosphereRow::FogSpell))
        return r->FogSpell;
    if (AtmosphereRow const* r = PickThemeLook(fogs, themeId, corruption, &AtmosphereRow::FogSpell))
        return r->FogSpell;
    return 0;
}

} // namespace Atlas

#endif
