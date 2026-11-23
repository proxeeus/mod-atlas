#include "AtlasLoot.h"

#include <gtest/gtest.h>

using namespace Atlas;

TEST(AtlasLoot, LadderFollowsTheDesign)
{
    EXPECT_EQ(DefaultLootTier(1).MinItemLevel, 200);    // PLAN 1.5: tier 1 drops 200
    EXPECT_EQ(DefaultLootTier(1).MaxItemLevel, 200);
    EXPECT_GE(DefaultLootTier(6).MinItemLevel, 213);    // tier 6: 213 or more
    EXPECT_EQ(DefaultLootTier(16).MinItemLevel, 251);
    EXPECT_EQ(DefaultLootTier(16).MaxItemLevel, 264);
    EXPECT_EQ(DefaultLootTier(0).MinItemLevel, 200);    // clamped
    EXPECT_EQ(DefaultLootTier(30).MaxItemLevel, 264);

    uint16 lastMin = 0;
    uint32 lastEpic = 0;
    for (uint8 t = 1; t <= MAP_MAX_TIER; ++t)
    {
        LootTier const lt = DefaultLootTier(t);
        EXPECT_LE(lt.MinItemLevel, lt.MaxItemLevel);
        EXPECT_GE(lt.MinItemLevel, lastMin);
        EXPECT_GE(lt.EpicShare, lastEpic);
        EXPECT_LE(lt.EpicShare, 100u);
        EXPECT_GE(lt.GearCount, 1u);
        lastMin = lt.MinItemLevel;
        lastEpic = lt.EpicShare;
    }
}

TEST(AtlasLoot, ChancesScaleAndCap)
{
    EXPECT_EQ(ScaleChance(10, 0), 10u);
    EXPECT_EQ(ScaleChance(10, 50), 15u);
    EXPECT_EQ(ScaleChance(800, 100), 1000u);
    EXPECT_EQ(ScaleEpicShare(40, 50), 60u);
    EXPECT_EQ(ScaleEpicShare(90, 50), 100u);

    std::array<uint32, 3> const w = ScaleRarityWeights({ 60, 30, 10 }, 100);
    EXPECT_EQ(w[0], 60u);
    EXPECT_EQ(w[1], 60u);
    EXPECT_EQ(w[2], 20u);
}

TEST(AtlasLoot, QuantityRollsItsExpectation)
{
    RunRng rng(9);
    for (int i = 0; i < 100; ++i)
    {
        EXPECT_EQ(RollQuantity(2, 0, rng), 2u);
        EXPECT_EQ(RollQuantity(1, 100, rng), 2u);
        EXPECT_EQ(RollQuantity(0, 300, rng), 0u);
    }

    uint32 total = 0;
    for (int i = 0; i < 10000; ++i)
        total += RollQuantity(1, 50, rng);
    EXPECT_NEAR(double(total) / 10000.0, 1.5, 0.03);
}

TEST(AtlasLoot, SigilTiersStayInRange)
{
    RunRng rng(4);
    for (int i = 0; i < 500; ++i)
    {
        uint8 const low = RollSigilTier(1, { 45, 45, 9, 1 }, rng);
        EXPECT_GE(low, 1);
        EXPECT_LE(low, 3);
        uint8 const high = RollSigilTier(16, { 45, 45, 9, 1 }, rng);
        EXPECT_GE(high, 15);
        EXPECT_LE(high, 16);
        EXPECT_EQ(RollSigilTier(8, { 0, 1, 0, 0 }, rng), 8);
        EXPECT_EQ(RollSigilTier(8, { 0, 0, 0, 1 }, rng), 10);
        EXPECT_EQ(RollSigilTier(8, { 0, 0, 0, 0 }, rng), 8);
    }
}

// PLAN 1.5: clearing tier-N maps sustains tier N about two times out of three.
// A run slays about 150 forces before its master falls (Atlas.Boss.ForcesCap).
TEST(AtlasLoot, DefaultsSustainAboutTwoInThree)
{
    SigilDropRules const rules;
    std::vector<uint32> const packs(50, 3);   // 150 forces
    RunRng rng(2026);
    uint32 sustained = 0;
    uint32 const runs = 20000;
    for (uint32 i = 0; i < runs; ++i)
    {
        std::vector<uint8> const tiers = RollRunSigils(8, packs, 0, rules, rng);
        if (std::any_of(tiers.begin(), tiers.end(), [](uint8 t) { return t >= 8; }))
            ++sustained;
    }
    double const share = double(sustained) / runs;
    EXPECT_GT(share, 0.60);
    EXPECT_LT(share, 0.73);

    // Quantity helps: an affixed map sustains better.
    uint32 boosted = 0;
    for (uint32 i = 0; i < runs; ++i)
    {
        std::vector<uint8> const tiers = RollRunSigils(8, packs, 100, rules, rng);
        if (std::any_of(tiers.begin(), tiers.end(), [](uint8 t) { return t >= 8; }))
            ++boosted;
    }
    EXPECT_GT(boosted, sustained);
}

TEST(AtlasLoot, BossSigilAlwaysDrops)
{
    SigilDropRules rules;
    rules.ExtraPermille = 0;
    rules.ForcePer100k = 0;
    RunRng rng(1);
    for (int i = 0; i < 100; ++i)
        EXPECT_EQ(RollRunSigils(5, std::vector<uint32>(30, 4), 0, rules, rng).size(), 1u);
}

// The same forces drop about the same sigils in lone creatures or in big packs
// (Maraudon's many lone spawns dropped far more under a flat chance per pack).
TEST(AtlasLoot, PackSigilsFollowForcesNotPackCount)
{
    SigilDropRules rules;
    rules.ExtraPermille = 0;
    std::vector<uint32> const lone(150, 1);
    std::vector<uint32> const big(30, 5);
    RunRng rng(77);
    uint32 fromLone = 0;
    uint32 fromBig = 0;
    uint32 const runs = 20000;
    for (uint32 i = 0; i < runs; ++i)
    {
        fromLone += static_cast<uint32>(RollRunSigils(8, lone, 0, rules, rng).size()) - 1;
        fromBig  += static_cast<uint32>(RollRunSigils(8, big, 0, rules, rng).size()) - 1;
    }
    double const perRunLone = double(fromLone) / runs;
    double const perRunBig = double(fromBig) / runs;
    EXPECT_NEAR(perRunLone, 0.195, 0.02);   // 150 x 0.13%
    EXPECT_NEAR(perRunBig, 0.195, 0.02);

    EXPECT_EQ(PackSigilChance(4, 130, 0), 520u);
    EXPECT_EQ(PackSigilChance(4, 130, 50), 780u);
    EXPECT_EQ(PackSigilChance(100000, 130, 0), 100000u);
}
