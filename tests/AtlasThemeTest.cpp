#include "AtlasTheme.h"

#include <gtest/gtest.h>

#include <set>

using namespace Atlas;

TEST(AtlasTheme, CorruptionLevelFollowsTierBands)
{
    EXPECT_EQ(CorruptionLevel(0), 0);
    EXPECT_EQ(CorruptionLevel(1), 0);
    EXPECT_EQ(CorruptionLevel(3), 0);
    EXPECT_EQ(CorruptionLevel(4), 1);
    EXPECT_EQ(CorruptionLevel(9), 2);
    EXPECT_EQ(CorruptionLevel(10), 3);
    EXPECT_EQ(CorruptionLevel(15), 4);
    EXPECT_EQ(CorruptionLevel(16), 5);
    EXPECT_EQ(CorruptionLevel(40), 5);
}

TEST(AtlasTheme, DefaultSharesAddUpAndGrowDarker)
{
    uint32 lastNative = 101;
    for (uint8 level = 0; level <= 5; ++level)
    {
        CorruptionShares const s = DefaultCorruptionShares(level);
        EXPECT_EQ(s.Native + s.Touched + s.Scourge, 100u);
        EXPECT_LE(s.Native, lastNative);
        lastNative = s.Native;
    }
    EXPECT_EQ(DefaultCorruptionShares(0).Touched, 10u);
    EXPECT_EQ(DefaultCorruptionShares(5).Scourge, 100u);
    EXPECT_EQ(DefaultCorruptionShares(9).Scourge, 100u);
}

TEST(AtlasTheme, RollPackKindHonoursShares)
{
    RunRng rng(42);
    for (int i = 0; i < 200; ++i)
        EXPECT_EQ(RollPackKind({ 100, 0, 0 }, rng), PackKind::Native);
    for (int i = 0; i < 200; ++i)
        EXPECT_EQ(RollPackKind({ 0, 0, 100 }, rng), PackKind::Scourge);
    EXPECT_EQ(RollPackKind({ 0, 0, 0 }, rng), PackKind::Native);

    uint32 counts[3] = {};
    for (int i = 0; i < 10000; ++i)
        ++counts[static_cast<int>(RollPackKind({ 60, 25, 15 }, rng))];
    EXPECT_NEAR(double(counts[0]), 6000, 300);
    EXPECT_NEAR(double(counts[1]), 2500, 300);
    EXPECT_NEAR(double(counts[2]), 1500, 300);
}

TEST(AtlasTheme, RollPackKindIsDeterministic)
{
    RunRng a(7), b(7);
    for (int i = 0; i < 100; ++i)
        EXPECT_EQ(RollPackKind({ 35, 30, 35 }, a), RollPackKind({ 35, 30, 35 }, b));
}

TEST(AtlasTheme, PackLeaderComesFirst)
{
    RunRng rng(3);
    EXPECT_EQ(PickMemberRole(0, rng), PackRole::Leader);

    std::set<PackRole> seen;
    for (int i = 0; i < 1000; ++i)
    {
        PackRole const r = PickMemberRole(1 + i % 5, rng);
        EXPECT_NE(r, PackRole::Leader);
        seen.insert(r);
    }
    EXPECT_EQ(seen.size(), 4u);   // melee, caster, heavy, swarm
}

TEST(AtlasTheme, ParsePackRole)
{
    EXPECT_EQ(ParsePackRole("leader"), PackRole::Leader);
    EXPECT_EQ(ParsePackRole("caster"), PackRole::Caster);
    EXPECT_EQ(ParsePackRole("boss"), PackRole::Boss);
    EXPECT_EQ(ParsePackRole("Boss"), PackRole::Count);
    EXPECT_EQ(ParsePackRole(""), PackRole::Count);
}

TEST(AtlasTheme, ClusterSpotsGroupsNeighbours)
{
    // Two groups of three, 50 yd apart, and a loner.
    std::vector<SpawnSpot> const spots = {
        { 0, 0, 0 }, { 4, 0, 0 }, { 50, 0, 0 }, { 8, 0, 0 }, { 54, 0, 0 }, { 52, 3, 0 }, { 200, 0, 0 },
    };
    auto const packs = ClusterSpots(spots, 5.0f, 6);
    ASSERT_EQ(packs.size(), 3u);
    EXPECT_EQ(packs[0], (std::vector<std::size_t>{ 0, 1, 3 }));   // chained: 0-4-8
    EXPECT_EQ(packs[1], (std::vector<std::size_t>{ 2, 4, 5 }));
    EXPECT_EQ(packs[2], (std::vector<std::size_t>{ 6 }));
}

TEST(AtlasTheme, ClusterSpotsRespectsSizeAndFloors)
{
    std::vector<SpawnSpot> line;
    for (int i = 0; i < 10; ++i)
        line.push_back({ float(i), 0, 0 });
    auto const packs = ClusterSpots(line, 2.0f, 4);
    ASSERT_EQ(packs.size(), 3u);
    EXPECT_EQ(packs[0].size(), 4u);
    EXPECT_EQ(packs[1].size(), 4u);
    EXPECT_EQ(packs[2].size(), 2u);

    // Same spot on the map, a floor apart: two packs.
    auto const floors = ClusterSpots({ { 0, 0, 0 }, { 1, 0, 10 } }, 5.0f, 6);
    EXPECT_EQ(floors.size(), 2u);

    // Every spot ends up in exactly one pack.
    std::set<std::size_t> all;
    for (auto const& p : packs)
        all.insert(p.begin(), p.end());
    EXPECT_EQ(all.size(), line.size());

    EXPECT_TRUE(ClusterSpots({}, 5.0f, 6).empty());
    EXPECT_EQ(ClusterSpots({ { 0, 0, 0 }, { 1, 0, 0 } }, 5.0f, 0).size(), 2u);
}
