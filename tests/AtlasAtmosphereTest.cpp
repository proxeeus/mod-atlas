#include "AtlasAtmosphere.h"

#include <gtest/gtest.h>

using namespace Atlas;

namespace
{
    std::vector<AtmosphereRow> Rows()
    {
        return {
            { 0, ATMOSPHERE_ANY_CORRUPTION, 0, 84, 0 },    // the echo default
            { 0, 3, 0, 27, 0 },                            // plague greys, any theme
            { 0, 5, 0, 1673, 0 },
            { 1, ATMOSPHERE_ANY_CORRUPTION, 0, 1374, 0 },  // beasts
            { 51, ATMOSPHERE_ANY_CORRUPTION, 0, 2508, 0 }, // the Scourge
            { 51, 5, 0, 333, 0 },                          // the Scourge, overrun
            { 0, ATMOSPHERE_ANY_CORRUPTION, 202, 1703, 0 },// Blizzard-swept
            { 0, ATMOSPHERE_ANY_CORRUPTION, 203, 43, 0 },  // Plague-cloud
        };
    }

    uint32 Light(AtmosphereRow const* row) { return row ? row->LightId : 0; }
}

TEST(AtlasAtmosphere, ThemeLookByPriority)
{
    auto const rows = Rows();
    EXPECT_EQ(Light(PickThemeLook(rows, 1, 0)), 1374u);     // the theme's own
    EXPECT_EQ(Light(PickThemeLook(rows, 1, 3)), 27u);       // corruption takes over
    EXPECT_EQ(Light(PickThemeLook(rows, 1, 5)), 1673u);
    EXPECT_EQ(Light(PickThemeLook(rows, 51, 5)), 333u);     // the theme's own at that level wins
    EXPECT_EQ(Light(PickThemeLook(rows, 51, 1)), 2508u);
    EXPECT_EQ(Light(PickThemeLook(rows, 50, 0)), 84u);      // no row: the default
    EXPECT_EQ(Light(PickThemeLook(rows, 0, 0)), 84u);
    EXPECT_EQ(Light(PickThemeLook({}, 1, 0)), 0u);
}

TEST(AtlasAtmosphere, AffixRowsStayOutOfThemeLooks)
{
    std::vector<AtmosphereRow> const rows = { { 0, ATMOSPHERE_ANY_CORRUPTION, 202, 1703, 0 } };
    EXPECT_EQ(PickThemeLook(rows, 1, 0), nullptr);
}

TEST(AtlasAtmosphere, AffixLookFollowsAffixOrder)
{
    auto const rows = Rows();
    EXPECT_EQ(Light(PickAffixLook(rows, { 1, 203, 202 })), 43u);
    EXPECT_EQ(Light(PickAffixLook(rows, { 202 })), 1703u);
    EXPECT_EQ(PickAffixLook(rows, { 1, 2 }), nullptr);
    EXPECT_EQ(PickAffixLook(rows, {}), nullptr);
}

namespace
{
    AtmosphereRow Fog(uint32 theme, uint8 corruption, uint32 affix, uint32 spell)
    {
        AtmosphereRow r;
        r.ThemeId = theme;
        r.Corruption = corruption;
        r.AffixId = affix;
        r.FogSpell = spell;
        return r;
    }

    std::vector<AtmosphereRow> Fogs()
    {
        return {
            Fog(0, ATMOSPHERE_ANY_CORRUPTION, 0, 90400),   // Timeways Mist
            Fog(1, ATMOSPHERE_ANY_CORRUPTION, 0, 90401),   // beasts
            Fog(0, 4, 0, 90411),                           // plague, any theme
            Fog(51, 2, 0, 90409),                          // the Scourge, deeper
            Fog(0, ATMOSPHERE_ANY_CORRUPTION, 202, 90413), // Blizzard-swept
        };
    }
}

TEST(AtlasAtmosphere, FogFollowsThemeAndCorruption)
{
    auto const fogs = Fogs();
    EXPECT_EQ(PickFog(fogs, 1, 0, {}), 90401u);    // the theme's own
    EXPECT_EQ(PickFog(fogs, 1, 4, {}), 90411u);    // corruption takes over
    EXPECT_EQ(PickFog(fogs, 7, 0, {}), 90400u);    // a theme without a fog: the default
    EXPECT_EQ(PickFog(fogs, 51, 2, {}), 90409u);   // the theme at its level
    EXPECT_EQ(PickFog(fogs, 51, 1, {}), 90400u);
}

TEST(AtlasAtmosphere, AffixFogWins)
{
    auto const fogs = Fogs();
    EXPECT_EQ(PickFog(fogs, 1, 0, { 7, 202 }), 90413u);
    EXPECT_EQ(PickFog(fogs, 1, 4, { 202 }), 90413u);
    EXPECT_EQ(PickFog(fogs, 1, 0, { 7 }), 90401u);  // an affix without a fog changes nothing
}

TEST(AtlasAtmosphere, FogRowsAndLightRowsStayApart)
{
    // A light row has no fog and a fog row no light: each pick sees only its own.
    std::vector<AtmosphereRow> const lights = Rows();
    EXPECT_EQ(PickFog(lights, 1, 0, { 202 }), 0u);
    auto const fogs = Fogs();
    EXPECT_EQ(PickThemeLook(fogs, 1, 0), nullptr);
    EXPECT_EQ(PickAffixLook(fogs, { 202 }), nullptr);
}
