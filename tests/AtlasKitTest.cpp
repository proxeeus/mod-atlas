#include "AtlasKit.h"

#include <gtest/gtest.h>

using namespace Atlas;

namespace
{
    constexpr float DEG = float(M_PI) / 180.0f;

    // The Goldmine room (MD_Goldmine_1Room.wmo): its box, and one socket at the
    // far end of the apron outside the mine's mouth (pool.json).
    KitPiece Goldmine()
    {
        KitPiece p;
        p.Entry = 900100;
        p.EntranceX = 0.0f; p.EntranceY = -5.0f; p.EntranceZ = -0.5f;
        p.MinX = -60.46f; p.MinY = -38.49f; p.MaxX = 45.70f; p.MaxY = 26.57f;
        p.Sockets = { { 44.58f, 9.73f, -4.03f, 11.79f * DEG } };
        return p;
    }

    KitPoint const ORIGIN{ -266.0f, -266.0f, 0.0f, 0.0f };

    float AngleGap(float a, float b)
    {
        float d = std::fabs(KitAngle(a) - KitAngle(b));
        return std::min(d, 2.0f * float(M_PI) - d);
    }
}

TEST(AtlasKit, JoinPutsTheSocketsTogetherFacingApart)
{
    KitSocket const s{ 12.0f, -7.0f, 2.5f, 40.0f * DEG };
    for (float o : { 0.0f, 1.0f, 2.5f, 4.0f, 6.0f })
    {
        KitPoint const at{ 100.0f, -50.0f, 3.0f, o };
        KitPlacement const p = KitJoin(at, 7, s);
        KitPoint const back = KitSocketInWorld(p, s);
        EXPECT_NEAR(back.X, at.X, 1e-3f);
        EXPECT_NEAR(back.Y, at.Y, 1e-3f);
        EXPECT_NEAR(back.Z, at.Z, 1e-3f);
        EXPECT_NEAR(AngleGap(back.O, at.O), float(M_PI), 1e-3f);
        EXPECT_EQ(p.Entry, 7u);
    }
}

TEST(AtlasKit, WorldBoxTurnsWithThePiece)
{
    KitPiece p;
    p.MinX = -1.0f; p.MinY = -2.0f; p.MaxX = 3.0f; p.MaxY = 4.0f;
    KitBox const flat = KitWorldBox(p, { 1, 10.0f, 20.0f, 0.0f, 0.0f });
    EXPECT_NEAR(flat.MinX, 9.0f, 1e-4f);
    EXPECT_NEAR(flat.MaxY, 24.0f, 1e-4f);
    KitBox const turned = KitWorldBox(p, { 1, 10.0f, 20.0f, 0.0f, 90.0f * DEG });
    EXPECT_NEAR(turned.MinX, 6.0f, 1e-4f);   // local y 4 -> world x -4
    EXPECT_NEAR(turned.MaxX, 12.0f, 1e-4f);
    EXPECT_NEAR(turned.MinY, 19.0f, 1e-4f);
    EXPECT_NEAR(turned.MaxY, 23.0f, 1e-4f);
}

TEST(AtlasKit, OverlapAllowsAShallowJoint)
{
    KitBox const a{ 0.0f, 0.0f, 10.0f, 10.0f };
    EXPECT_FALSE(KitOverlaps(a, { 8.0f, 0.0f, 20.0f, 10.0f }));   // 2 yd deep
    EXPECT_TRUE(KitOverlaps(a, { 4.0f, 2.0f, 20.0f, 9.0f }));     // 6 x 7 yd
    EXPECT_FALSE(KitOverlaps(a, { 20.0f, 20.0f, 30.0f, 30.0f }));
}

TEST(AtlasKit, TwoGoldminesMeetAtTheirAprons)
{
    std::vector<KitPiece> const kit{ Goldmine() };
    RunRng rng(42);
    std::vector<KitPlacement> const layout = KitBuildLayout(kit, 900100, ORIGIN, 4, rng);
    ASSERT_EQ(layout.size(), 2u);   // one socket each: closed after one joint

    EXPECT_FLOAT_EQ(layout[0].X, ORIGIN.X);
    EXPECT_FLOAT_EQ(layout[0].O, 0.0f);

    // The second mine is turned half a round, mouth facing the first one's.
    EXPECT_NEAR(AngleGap(layout[1].O, float(M_PI)), 0.0f, 1e-4f);
    EXPECT_NEAR(layout[1].X, -176.84f, 0.01f);
    EXPECT_NEAR(layout[1].Y, -246.54f, 0.01f);
    EXPECT_NEAR(layout[1].Z, 0.0f, 1e-4f);

    KitPoint const a = KitSocketInWorld(layout[0], kit[0].Sockets[0]);
    KitPoint const b = KitSocketInWorld(layout[1], kit[0].Sockets[0]);
    EXPECT_NEAR(a.X, b.X, 1e-3f);
    EXPECT_NEAR(a.Y, b.Y, 1e-3f);
    EXPECT_NEAR(a.Z, b.Z, 1e-3f);
}

TEST(AtlasKit, LayoutStopsAtMaxPiecesAndNeedsTheStartPiece)
{
    std::vector<KitPiece> const kit{ Goldmine() };
    RunRng rng(1);
    EXPECT_EQ(KitBuildLayout(kit, 900100, ORIGIN, 1, rng).size(), 1u);
    EXPECT_TRUE(KitBuildLayout(kit, 900100, ORIGIN, 0, rng).empty());
    EXPECT_TRUE(KitBuildLayout(kit, 123, ORIGIN, 4, rng).empty());
}

TEST(AtlasKit, APieceThatWouldOverlapIsNotPlaced)
{
    // A socket in the middle of the piece: anything joined there sits on it.
    KitPiece p = Goldmine();
    p.Sockets = { { 0.0f, 0.0f, 0.0f, 0.0f } };
    RunRng rng(7);
    EXPECT_EQ(KitBuildLayout({ p }, 900100, ORIGIN, 4, rng).size(), 1u);
}

TEST(AtlasKit, CorridorGrowsAndIsReproducible)
{
    // A 10 x 4 corridor with a socket at each end: a straight line of pieces.
    KitPiece c;
    c.Entry = 1;
    c.MinX = -5.0f; c.MinY = -2.0f; c.MaxX = 5.0f; c.MaxY = 2.0f;
    c.Sockets = { { 5.0f, 0.0f, 0.0f, 0.0f }, { -5.0f, 0.0f, 0.0f, float(M_PI) } };
    std::vector<KitPiece> const kit{ c };

    RunRng a(99), b(99);
    std::vector<KitPlacement> const la = KitBuildLayout(kit, 1, { 0.0f, 0.0f, 0.0f, 0.0f }, 5, a);
    std::vector<KitPlacement> const lb = KitBuildLayout(kit, 1, { 0.0f, 0.0f, 0.0f, 0.0f }, 5, b);
    ASSERT_EQ(la.size(), 5u);
    ASSERT_EQ(lb.size(), 5u);
    for (std::size_t i = 0; i < la.size(); ++i)
    {
        EXPECT_FLOAT_EQ(la[i].X, lb[i].X);
        EXPECT_FLOAT_EQ(la[i].Y, lb[i].Y);
        EXPECT_NEAR(la[i].Y, 0.0f, 1e-3f);   // all on one line
        EXPECT_NEAR(std::fmod(std::fabs(la[i].X) + 0.001f, 10.0f), 0.0f, 0.01f);   // 10 yd apart
    }
}
