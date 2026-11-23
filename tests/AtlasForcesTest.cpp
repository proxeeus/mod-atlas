#include "AtlasForces.h"

#include <gtest/gtest.h>

using namespace Atlas;

TEST(AtlasForces, ThresholdRoundsUp)
{
    EXPECT_EQ(ForcesThreshold(0, 75), 0u);
    EXPECT_EQ(ForcesThreshold(100, 75), 75u);
    EXPECT_EQ(ForcesThreshold(101, 75), 76u);      // 75.75 -> 76
    EXPECT_EQ(ForcesThreshold(3, 1), 1u);          // never 0 with forces present
    EXPECT_EQ(ForcesThreshold(450, 100), 450u);
    EXPECT_EQ(ForcesThreshold(450, 150), 450u);    // capped at all of them
    EXPECT_EQ(ForcesThreshold(600, 75, 150), 150u);  // a huge dungeon: the cap
    EXPECT_EQ(ForcesThreshold(100, 75, 150), 75u);   // a small one: the share
    EXPECT_EQ(ForcesThreshold(100, 75, 0), 75u);     // 0 = no cap
}

TEST(AtlasForces, ProgressAndSteps)
{
    EXPECT_EQ(ForcesProgress(0, 75), 0u);
    EXPECT_EQ(ForcesProgress(30, 75), 40u);
    EXPECT_EQ(ForcesProgress(75, 75), 100u);
    EXPECT_EQ(ForcesProgress(200, 75), 100u);
    EXPECT_EQ(ForcesProgress(5, 0), 100u);

    EXPECT_EQ(ForcesStep(0), 0);
    EXPECT_EQ(ForcesStep(24), 0);
    EXPECT_EQ(ForcesStep(25), 1);
    EXPECT_EQ(ForcesStep(74), 2);
    EXPECT_EQ(ForcesStep(99), 3);
    EXPECT_EQ(ForcesStep(100), 4);
    EXPECT_EQ(ForcesStep(250), 4);
}
