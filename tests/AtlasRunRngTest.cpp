#include "RunRng.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <numeric>
#include <set>

using Atlas::RunRng;

// xoshiro256** seeded through splitmix64(0); cross-checked against an
// independent implementation. If this changes, every logged seed replays
// differently: never change the engine without a new seed format.
TEST(AtlasRunRngTest, MatchesReferenceSequence)
{
    RunRng rng(0);
    EXPECT_EQ(rng.Next(), 0x99EC5F36CB75F2B4ULL);
    EXPECT_EQ(rng.Next(), 0xBF6E1F784956452AULL);
    EXPECT_EQ(rng.Next(), 0x1A5F849D4933E6E0ULL);
}

TEST(AtlasRunRngTest, SameSeedSameDraws)
{
    RunRng a(0x3F2A9C0D11E4B7A5ULL);
    RunRng b(0x3F2A9C0D11E4B7A5ULL);
    for (int i = 0; i < 1000; ++i)
        ASSERT_EQ(a.Range(1, 100), b.Range(1, 100));
    EXPECT_EQ(a.GetSeed(), 0x3F2A9C0D11E4B7A5ULL);
}

TEST(AtlasRunRngTest, DifferentSeedsDiffer)
{
    RunRng a(1);
    RunRng b(2);
    int same = 0;
    for (int i = 0; i < 100; ++i)
        same += a.Next() == b.Next();
    EXPECT_EQ(same, 0);
}

TEST(AtlasRunRngTest, ReseedRestartsTheSequence)
{
    RunRng rng(77);
    uint64 const first = rng.Next();
    rng.Next();
    rng.Seed(77);
    EXPECT_EQ(rng.Next(), first);
}

TEST(AtlasRunRngTest, RangeStaysInBoundsAndCoversThem)
{
    RunRng rng(5);
    std::set<uint32> seen;
    for (int i = 0; i < 10000; ++i)
    {
        uint32 const v = rng.Range(3, 9);
        ASSERT_GE(v, 3u);
        ASSERT_LE(v, 9u);
        seen.insert(v);
    }
    EXPECT_EQ(seen.size(), 7u);
    EXPECT_EQ(rng.Range(4, 4), 4u);
    EXPECT_EQ(rng.Range(9, 2), 9u);   // empty range: the low bound
}

TEST(AtlasRunRngTest, ChanceEdges)
{
    RunRng rng(11);
    for (int i = 0; i < 1000; ++i)
    {
        ASSERT_FALSE(rng.Chance(0));
        ASSERT_TRUE(rng.Chance(100));
    }
}

TEST(AtlasRunRngTest, ShuffleIsADeterministicPermutation)
{
    std::vector<int> a(20);
    std::iota(a.begin(), a.end(), 0);
    std::vector<int> b = a;

    RunRng ra(9);
    RunRng rb(9);
    ra.Shuffle(a);
    rb.Shuffle(b);
    EXPECT_EQ(a, b);

    std::vector<int> sorted = a;
    std::sort(sorted.begin(), sorted.end());
    std::vector<int> expected(20);
    std::iota(expected.begin(), expected.end(), 0);
    EXPECT_EQ(sorted, expected);
}

TEST(AtlasRunRngTest, PickReturnsAnElement)
{
    std::vector<uint32> items = { 10, 20, 30 };
    RunRng rng(3);
    for (int i = 0; i < 100; ++i)
    {
        uint32 const v = rng.Pick(items);
        ASSERT_TRUE(v == 10 || v == 20 || v == 30);
    }
}
