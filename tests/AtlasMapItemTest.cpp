#include "AtlasMapItem.h"

#include <gtest/gtest.h>

using namespace Atlas;

TEST(AtlasMapItem, EntriesRoundTrip)
{
    EXPECT_EQ(MapItemEntry(1, MapRarity::Normal), 90001u);
    EXPECT_EQ(MapItemEntry(16, MapRarity::Normal), 90016u);
    EXPECT_EQ(MapItemEntry(3, MapRarity::Magic), 90103u);
    EXPECT_EQ(MapItemEntry(16, MapRarity::Rare), 90216u);
    EXPECT_EQ(MapItemEntry(0, MapRarity::Normal), 90001u);    // clamped
    EXPECT_EQ(MapItemEntry(40, MapRarity::Magic), 90116u);

    for (uint8 r = 0; r < static_cast<uint8>(MapRarity::Count); ++r)
        for (uint8 t = 1; t <= MAP_MAX_TIER; ++t)
        {
            uint8 tier = 0;
            MapRarity rarity = MapRarity::Count;
            ASSERT_TRUE(DecodeMapItemEntry(MapItemEntry(t, MapRarity(r)), tier, rarity));
            EXPECT_EQ(tier, t);
            EXPECT_EQ(rarity, MapRarity(r));
        }
}

TEST(AtlasMapItem, NonSigilEntriesAreRejected)
{
    EXPECT_FALSE(IsMapItemEntry(0));
    EXPECT_FALSE(IsMapItemEntry(90000));
    EXPECT_FALSE(IsMapItemEntry(90017));
    EXPECT_FALSE(IsMapItemEntry(90100));
    EXPECT_FALSE(IsMapItemEntry(90301));
    EXPECT_FALSE(IsMapItemEntry(37869));
    EXPECT_TRUE(IsMapItemEntry(90201));
}

TEST(AtlasMapItem, RarityNames)
{
    EXPECT_EQ(ParseMapRarity("normal"), MapRarity::Normal);
    EXPECT_EQ(ParseMapRarity("magic"), MapRarity::Magic);
    EXPECT_EQ(ParseMapRarity("rare"), MapRarity::Rare);
    EXPECT_EQ(ParseMapRarity("unique"), MapRarity::Count);
    EXPECT_STREQ(MapRarityName(MapRarity::Magic), "magic");
}

TEST(AtlasMapItem, AffixCountsByRarity)
{
    EXPECT_EQ(MapAffixCountRange(MapRarity::Normal).second, 0u);
    EXPECT_EQ(MapAffixCountRange(MapRarity::Magic).first, 1u);
    EXPECT_EQ(MapAffixCountRange(MapRarity::Magic).second, 2u);
    EXPECT_EQ(MapAffixCountRange(MapRarity::Rare).first, 3u);
}

TEST(AtlasMapItem, RollRarityHonoursWeights)
{
    RunRng rng(11);
    for (int i = 0; i < 100; ++i)
        EXPECT_EQ(RollMapRarity({ 0, 0, 1 }, rng), MapRarity::Rare);
    EXPECT_EQ(RollMapRarity({ 0, 0, 0 }, rng), MapRarity::Normal);

    uint32 counts[3] = {};
    for (int i = 0; i < 10000; ++i)
        ++counts[static_cast<int>(RollMapRarity({ 60, 30, 10 }, rng))];
    EXPECT_NEAR(double(counts[0]), 6000, 300);
    EXPECT_NEAR(double(counts[1]), 3000, 300);
    EXPECT_NEAR(double(counts[2]), 1000, 200);
}

TEST(AtlasMapItem, SourceBands)
{
    EXPECT_EQ(MapSourceBand(1), 0);
    EXPECT_EQ(MapSourceBand(5), 0);
    EXPECT_EQ(MapSourceBand(6), 1);
    EXPECT_EQ(MapSourceBand(10), 1);
    EXPECT_EQ(MapSourceBand(11), 2);
    EXPECT_EQ(MapSourceBand(16), 2);
}

TEST(AtlasMapItem, AffixesSerializeRoundTrip)
{
    std::vector<ActiveAffix> const affixes = { { 1, 25.0f }, { 14, 7.5f }, { 30, 120.25f } };
    std::string const text = SerializeMapAffixes(affixes);
    EXPECT_EQ(text, "1:25.00;14:7.50;30:120.25");

    std::vector<ActiveAffix> const back = ParseMapAffixes(text);
    ASSERT_EQ(back.size(), affixes.size());
    for (std::size_t i = 0; i < back.size(); ++i)
    {
        EXPECT_EQ(back[i].Id, affixes[i].Id);
        EXPECT_FLOAT_EQ(back[i].Value, affixes[i].Value);
    }

    EXPECT_TRUE(SerializeMapAffixes({}).empty());
    EXPECT_TRUE(ParseMapAffixes("").empty());
    EXPECT_EQ(ParseMapAffixes("bad;2:3;x:1;:4;5:").size(), 1u);
}
