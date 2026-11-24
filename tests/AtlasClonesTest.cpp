#include "AtlasClones.h"

#include <gtest/gtest.h>

using namespace Atlas;

namespace
{
    CloneRegistry Deadmines()
    {
        CloneRegistry r;
        r.Set({ { 902, 36, "Echo of the Deadmines" } });
        return r;
    }
}

TEST(AtlasClones, ReservedRange)
{
    EXPECT_FALSE(IsReservedCloneId(899));
    EXPECT_TRUE(IsReservedCloneId(900));
    EXPECT_TRUE(IsReservedCloneId(949));
    EXPECT_FALSE(IsReservedCloneId(950));
}

TEST(AtlasClones, SourceAndCloneLookups)
{
    CloneRegistry const r = Deadmines();
    EXPECT_TRUE(r.IsClone(902));
    EXPECT_FALSE(r.IsClone(36));
    EXPECT_EQ(r.SourceOf(902), 36u);
    EXPECT_EQ(r.SourceOf(36), 36u);
    EXPECT_EQ(r.SourceOf(33), 33u);
    EXPECT_EQ(r.CloneOf(36), 902u);
    EXPECT_EQ(r.CloneOf(33), 0u);
    ASSERT_NE(r.Get(902), nullptr);
    EXPECT_EQ(r.Get(902)->Name, "Echo of the Deadmines");
    EXPECT_EQ(r.Get(36), nullptr);
}

TEST(AtlasClones, SetDropsBadRows)
{
    CloneRegistry r;
    std::vector<CloneMap> const kept = r.Set({
        { 902, 36, "a" },
        { 903, 36, "second clone of the same source" },
        { 902, 33, "same clone ID again" },
        { 850, 33, "outside the reserved IDs" },
        { 904, 902, "a clone of a clone" },
        { 905, 0, "no source" },
        { 904, 33, "b" } });
    ASSERT_EQ(kept.size(), 2u);
    EXPECT_EQ(kept[0].CloneId, 902u);
    EXPECT_EQ(kept[1].CloneId, 904u);
    EXPECT_EQ(r.Size(), 2u);
    EXPECT_EQ(r.CloneOf(33), 904u);
    EXPECT_FALSE(r.IsClone(903));
    EXPECT_FALSE(r.IsClone(850));
}

TEST(AtlasClones, SetReplacesTheRegistry)
{
    CloneRegistry r = Deadmines();
    r.Set({ { 904, 33, "Echo of Shadowfang Keep" } });
    EXPECT_FALSE(r.IsClone(902));
    EXPECT_EQ(r.CloneOf(36), 0u);
    EXPECT_EQ(r.CloneOf(33), 904u);
}

TEST(AtlasClones, ChooseRunMap)
{
    // Clones on, a clone, everyone opted in: the clone.
    EXPECT_EQ(ChooseRunMap(true, 36, 902, 1, 1), 902u);
    EXPECT_EQ(ChooseRunMap(true, 36, 902, 2, 2), 902u);
    // Anything missing: the stock dungeon, as before stage 2.
    EXPECT_EQ(ChooseRunMap(false, 36, 902, 1, 1), 36u);
    EXPECT_EQ(ChooseRunMap(true, 36, 0, 1, 1), 36u);
    EXPECT_EQ(ChooseRunMap(true, 36, 902, 1, 0), 36u);
    EXPECT_EQ(ChooseRunMap(true, 36, 902, 2, 1), 36u);
    EXPECT_EQ(ChooseRunMap(true, 36, 902, 0, 0), 36u);
}
