#include "AtlasBossSpawnQuery.h"

#include <gtest/gtest.h>

TEST(AtlasBossSpawnQueryTest, UsesCurrentCreatureImmunitySchema)
{
    std::string query = Atlas::BuildBossSpawnPointQuery(229);

    EXPECT_NE(query.find("creature_immunities"), std::string::npos);
    EXPECT_NE(query.find("CreatureImmunitiesId"), std::string::npos);
    EXPECT_NE(query.find("MechanicsMask"), std::string::npos);
    EXPECT_EQ(query.find("mechanic_immune_mask"), std::string::npos);
}

TEST(AtlasBossSpawnQueryTest, SelectsNumericEntryInsteadOfTemplateName)
{
    std::string query = Atlas::BuildBossSpawnPointQuery(229);

    EXPECT_NE(query.find("ct.entry"), std::string::npos);
    EXPECT_EQ(query.find("ct.name"), std::string::npos);
    EXPECT_NE(query.find("c.map = 229"), std::string::npos);
}
