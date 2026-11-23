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
