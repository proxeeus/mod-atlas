#include "AtlasScalingMath.h"

#include <gtest/gtest.h>

using namespace Atlas;

TEST(AtlasScalingMathTest, TierGrowth)
{
    EXPECT_FLOAT_EQ(TierMultiplier(1.075f, 1), 1.0f);
    EXPECT_FLOAT_EQ(TierMultiplier(1.075f, 0), 1.0f);   // tier 0 is treated as 1
    EXPECT_NEAR(TierMultiplier(1.075f, 16), 2.959f, 0.005f);
}

TEST(AtlasScalingMathTest, PartyFactorIsOneForFive)
{
    EXPECT_FLOAT_EQ(PartyFactor(5, 0.5f, 0.25f), 1.0f);
    EXPECT_FLOAT_EQ(PartyFactor(1, 0.5f, 0.25f), 0.25f);
    EXPECT_FLOAT_EQ(PartyFactor(3, 0.5f, 0.25f), 0.75f);
    EXPECT_NEAR(PartyFactor(1, 0.5f, 0.1f), 0.357f, 0.001f);
    EXPECT_GT(PartyFactor(6, 0.5f, 0.25f), 1.0f);
}

TEST(AtlasScalingMathTest, ExpansionColumnByLevel)
{
    EXPECT_EQ(ExpansionForLevel(1), 0);
    EXPECT_EQ(ExpansionForLevel(60), 0);
    EXPECT_EQ(ExpansionForLevel(61), 1);
    EXPECT_EQ(ExpansionForLevel(70), 1);
    EXPECT_EQ(ExpansionForLevel(71), 2);
    EXPECT_EQ(ExpansionForLevel(80), 2);
}

// The weapon damage and attack power ComputeMelee sets must make AzerothCore's
// formula land on base x multiplier, whatever the template's DamageModifier.
TEST(AtlasScalingMathTest, MeleeCancelsTheTemplateModifier)
{
    float const base = 164.924f;   // creature_classlevelstats, level 80 warrior, damage_exp2
    float const ap   = 642.0f;
    float const mult = 3.0f;       // the boss role
    float const want = (base + ap / 14.0f) * mult * 2.0f;

    for (float dm : { 0.5f, 1.0f, 7.5f, 35.0f })
    {
        MeleeSetup const m = ComputeMelee(base, ap, mult, dm);
        EXPECT_NEAR(MeleeHit(m.WeaponMin, m.AttackPower, dm, 2.0f), want, want * 0.001f) << "dm " << dm;
        EXPECT_NEAR(m.WeaponMax / m.WeaponMin, 1.5f, 0.001f);
    }
}

TEST(AtlasScalingMathTest, MeleeSurvivesAZeroModifier)
{
    MeleeSetup const m = ComputeMelee(100.0f, 200.0f, 1.0f, 0.0f);
    EXPECT_GE(m.WeaponMin, 1.0f);
    EXPECT_GE(m.WeaponMax, m.WeaponMin);
}

TEST(AtlasScalingMathTest, DesignRoleDefaults)
{
    EXPECT_FLOAT_EQ(DefaultRoleMultipliers(CreatureRole::Trash).Health, 1.0f);
    EXPECT_FLOAT_EQ(DefaultRoleMultipliers(CreatureRole::Elite).Health, 2.5f);
    EXPECT_FLOAT_EQ(DefaultRoleMultipliers(CreatureRole::Boss).Health, 30.0f);
    EXPECT_FLOAT_EQ(DefaultRoleMultipliers(CreatureRole::Boss).Damage, 3.0f);
    EXPECT_FLOAT_EQ(DefaultRoleMultipliers(CreatureRole::Add).Damage, 0.7f);
    EXPECT_STREQ(RoleName(CreatureRole::Miniboss), "Miniboss");
}

TEST(AtlasScalingMathTest, ClampScale)
{
    EXPECT_FLOAT_EQ(ClampScale(50.0f, 0.05f, 20.0f), 20.0f);
    EXPECT_FLOAT_EQ(ClampScale(0.0f, 0.05f, 20.0f), 0.05f);
    EXPECT_FLOAT_EQ(ClampScale(std::nanf(""), 0.05f, 20.0f), 1.0f);
}
