#include "AtlasAffix.h"

#include <gtest/gtest.h>

#include <set>

using namespace Atlas;

namespace
{
    AffixDefinition Def(uint32 id, AffixHandler handler, std::string group = "", uint32 minTier = 1,
        bool botSafe = true, float v1 = 10.0f, float v2 = 20.0f, float v3 = 30.0f)
    {
        AffixDefinition d;
        d.Id = id;
        d.Code = "T" + std::to_string(id);
        d.Name = d.Code;
        d.Description = "Value {}%";
        d.Handler = handler;
        d.ExclusiveGroup = std::move(group);
        d.MinTier = minTier;
        d.BotSafe = botSafe;
        d.Values[0] = v1;
        d.Values[1] = v2;
        d.Values[2] = v3;
        return d;
    }

    std::vector<AffixDefinition> Pool()
    {
        return {
            Def(1, AffixHandler::MeleeDealt, "dmg"),
            Def(2, AffixHandler::ExtraHit, "dmg"),
            Def(3, AffixHandler::MonsterLife, "", 5),
            Def(4, AffixHandler::PhysTaken, "", 1, false),
            Def(5, AffixHandler::Unknown),
        };
    }
}

TEST(AtlasAffixTest, TierBands)
{
    EXPECT_EQ(BandForTier(1), 0);
    EXPECT_EQ(BandForTier(5), 0);
    EXPECT_EQ(BandForTier(6), 1);
    EXPECT_EQ(BandForTier(10), 1);
    EXPECT_EQ(BandForTier(11), 2);
    EXPECT_EQ(BandForTier(16), 2);
}

TEST(AtlasAffixTest, HandlerNames)
{
    EXPECT_TRUE(ParseAffixHandler("MELEE_DEALT") == AffixHandler::MeleeDealt);
    EXPECT_TRUE(ParseAffixHandler("GROUND_PERIODIC") == AffixHandler::GroundPeriodic);
    EXPECT_TRUE(ParseAffixHandler("BOSS_ADDS") == AffixHandler::BossAdds);
    EXPECT_TRUE(ParseAffixHandler("NOPE") == AffixHandler::Unknown);
}

TEST(AtlasAffixTest, RollRespectsGroupsTiersAndBotSafety)
{
    std::vector<AffixDefinition> const pool = Pool();

    for (uint64 seed = 1; seed < 200; ++seed)
    {
        RunRng rng(seed);
        // Tier 1, bot-safe only: one of the "dmg" pair, nothing else is eligible.
        std::vector<ActiveAffix> const a = RollAffixes(pool, 1, 3, true, rng);
        ASSERT_EQ(a.size(), 1u);
        ASSERT_TRUE(a[0].Id == 1 || a[0].Id == 2);

        // Tier 5 adds affix 3; still never both of the "dmg" pair.
        std::vector<ActiveAffix> const b = RollAffixes(pool, 5, 3, true, rng);
        ASSERT_EQ(b.size(), 2u);
        std::set<uint32> ids;
        for (ActiveAffix const& x : b)
            ids.insert(x.Id);
        ASSERT_TRUE(ids.count(3));
        ASSERT_FALSE(ids.count(1) && ids.count(2));

        // Allowing unsafe affixes brings in affix 4; the unknown handler never rolls.
        std::vector<ActiveAffix> const c = RollAffixes(pool, 5, 5, false, rng);
        ASSERT_EQ(c.size(), 3u);
        for (ActiveAffix const& x : c)
            ASSERT_NE(x.Id, 5u);
    }
}

TEST(AtlasAffixTest, RollIsDeterministicAndBanded)
{
    std::vector<AffixDefinition> const pool = Pool();
    RunRng r1(42);
    RunRng r2(42);
    std::vector<ActiveAffix> const a = RollAffixes(pool, 12, 3, false, r1);
    std::vector<ActiveAffix> const b = RollAffixes(pool, 12, 3, false, r2);
    ASSERT_EQ(a.size(), b.size());
    for (std::size_t i = 0; i < a.size(); ++i)
    {
        EXPECT_EQ(a[i].Id, b[i].Id);
        EXPECT_FLOAT_EQ(a[i].Value, 30.0f);   // tier 12: the third band
    }
}

TEST(AtlasAffixTest, FormatsTheValue)
{
    AffixDefinition d = Def(1, AffixHandler::MeleeDealt);
    EXPECT_EQ(FormatAffix(d, 25.0f), "Value 25%");
    EXPECT_EQ(FormatAffix(d, 0.5f), "Value 0.5%");
    d.Description = "No number here";
    EXPECT_EQ(FormatAffix(d, 3.0f), "No number here");
}

TEST(AtlasAffixTest, Aggregates)
{
    std::vector<AffixDefinition> defs = {
        Def(1, AffixHandler::MonsterLife),
        Def(2, AffixHandler::PhysTaken),
        Def(3, AffixHandler::PlayerAura),
        Def(4, AffixHandler::GroundPeriodic),
        Def(5, AffixHandler::ExtraHit),
        Def(6, AffixHandler::BossAdds),
    };
    defs[2].SpellId = 90001;
    defs[2].Param1 = -1.0f;
    defs[3].SpellId = 19099;
    defs[3].Param1 = 12000.0f;
    defs[4].SpellId = 13897;

    AffixEffects const fx = AggregateAffixes(defs, {
        { 1, 25.0f }, { 2, 10.0f }, { 3, 30.0f }, { 4, 50.0f }, { 5, 20.0f }, { 6, 4.0f }, { 99, 1.0f } });

    EXPECT_FLOAT_EQ(fx.MonsterHealth, 1.25f);
    EXPECT_FLOAT_EQ(fx.PhysTaken, 0.9f);
    ASSERT_EQ(fx.PlayerAuras.size(), 1u);
    EXPECT_EQ(fx.PlayerAuras[0].SpellId, 90001u);
    EXPECT_EQ(fx.PlayerAuras[0].Amount, -30);
    ASSERT_EQ(fx.Ground.size(), 1u);
    EXPECT_EQ(fx.Ground[0].IntervalMs, 12000u);
    EXPECT_FLOAT_EQ(fx.Ground[0].Pct, 0.5f);
    ASSERT_EQ(fx.ExtraHits.size(), 1u);
    EXPECT_FLOAT_EQ(fx.ExtraHits[0].Pct, 0.2f);
    EXPECT_EQ(fx.BossAddsPerWave, 4u);
    EXPECT_FLOAT_EQ(fx.BossHealth, 1.0f);
}

TEST(AtlasAffix, MarkersComeFromCreatureAffixesOnce)
{
    std::vector<AffixDefinition> defs = Pool();
    for (AffixDefinition& d : defs)
        d.MarkerSpell = 500 + d.Id;
    // Two creature affixes sharing one marker, and a player affix with one.
    defs[0].MarkerSpell = 777;
    defs[1].MarkerSpell = 777;

    std::vector<ActiveAffix> active;
    for (AffixDefinition const& d : defs)
        active.push_back({ d.Id, d.Values[0] });
    AffixEffects const fx = AggregateAffixes(defs, active);

    ASSERT_EQ(fx.Markers.size(), 3u);            // 777 once, 503, 504; not the Unknown affix's 505
    EXPECT_EQ(fx.Markers[0], 777u);
    std::set<uint32> seen(fx.Markers.begin(), fx.Markers.end());
    EXPECT_EQ(seen.size(), fx.Markers.size());   // no repeats
    for (uint32 spell : fx.Markers)
    {
        bool fromCreatureAffix = false;
        for (AffixDefinition const& d : defs)
            if (d.MarkerSpell == spell && IsCreatureAffix(d.Handler))
                fromCreatureAffix = true;
        EXPECT_TRUE(fromCreatureAffix);
    }
    EXPECT_TRUE(AggregateAffixes(defs, {}).Markers.empty());
}
