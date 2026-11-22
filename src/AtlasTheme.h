/*
 * mod-atlas — AtlasTheme.h
 * Population as pure data and math (DESIGN §8, SCOURGE appendix), so packs,
 * corruption and clustering can be unit-tested.
 *
 * A run's anchors are packs (clusters of a dungeon's stock spawns). Each pack
 * is native (the chosen timeline: the dungeon's own inhabitants, a curated
 * pool or a creature type), Scourge-touched (the same, marked by the plague)
 * or Scourge, by the corruption of the run's tier.
 */

#ifndef ATLAS_THEME_H
#define ATLAS_THEME_H

#include "Define.h"
#include "RunRng.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <string_view>
#include <utility>
#include <vector>

namespace Atlas
{

// Who goes into a pack (pool themes); scaling roles follow from rank.
enum class PackRole : uint8
{
    Leader = 0,
    Heavy,
    Melee,
    Caster,
    Swarm,
    Add,
    Rare,
    Boss,
    Count
};

inline PackRole ParsePackRole(std::string_view name)
{
    static constexpr std::pair<std::string_view, PackRole> names[] = {
        { "leader", PackRole::Leader }, { "heavy", PackRole::Heavy }, { "melee", PackRole::Melee },
        { "caster", PackRole::Caster }, { "swarm", PackRole::Swarm }, { "add", PackRole::Add },
        { "rare", PackRole::Rare },     { "boss", PackRole::Boss },
    };
    for (auto const& [text, role] : names)
        if (text == name)
            return role;
    return PackRole::Count;
}

enum class PackKind : uint8
{
    Native = 0,   // the run's timeline as chosen
    Touched,      // the same, marked by the plague
    Scourge       // the Scourge pool
};

// Shares (in %) of native, touched and Scourge packs.
struct CorruptionShares
{
    uint32 Native  = 100;
    uint32 Touched = 0;
    uint32 Scourge = 0;
};

// Corruption level of a tier: 0 for tiers 1-3, then one per three tiers, 5
// ("overrun") at 16 (SCOURGE appendix §8).
inline uint8 CorruptionLevel(uint32 tier)
{
    if (tier >= 16)
        return 5;
    return static_cast<uint8>((std::max<uint32>(tier, 1) - 1) / 3);
}

inline CorruptionShares DefaultCorruptionShares(uint8 level)
{
    static constexpr std::array<CorruptionShares, 6> table = { {
        { 90, 10, 0 }, { 60, 25, 15 }, { 35, 30, 35 }, { 15, 25, 60 }, { 0, 15, 85 }, { 0, 0, 100 },
    } };
    return table[std::min<uint8>(level, 5)];
}

inline PackKind RollPackKind(CorruptionShares const& shares, RunRng& rng)
{
    uint32 const total = shares.Native + shares.Touched + shares.Scourge;
    if (!total)
        return PackKind::Native;
    uint32 const draw = rng.Range(1, total);
    if (draw <= shares.Native)
        return PackKind::Native;
    if (draw <= shares.Native + shares.Touched)
        return PackKind::Touched;
    return PackKind::Scourge;
}

// A pool pack's member roles: the first member leads, the rest are mostly
// melee, with casters, the odd heavy and swarm filling in (SCOURGE §3).
inline PackRole PickMemberRole(std::size_t index, RunRng& rng)
{
    if (index == 0)
        return PackRole::Leader;
    uint32 const draw = rng.Range(1, 100);
    if (draw <= 45)
        return PackRole::Melee;
    if (draw <= 65)
        return PackRole::Caster;
    if (draw <= 75)
        return PackRole::Heavy;
    return PackRole::Swarm;
}

// Clusters spawn spots into packs: a spot joins a pack when it stands within
// `radius` of one of its members, up to `maxSize` members. Greedy in the
// spots' order, so deterministic (C8). Returns spot indices per pack.
struct SpawnSpot
{
    float X = 0.0f;
    float Y = 0.0f;
    float Z = 0.0f;
};

inline std::vector<std::vector<std::size_t>> ClusterSpots(std::vector<SpawnSpot> const& spots, float radius,
    std::size_t maxSize)
{
    std::vector<std::vector<std::size_t>> packs;
    std::vector<bool> taken(spots.size(), false);
    float const r2 = radius * radius;
    maxSize = std::max<std::size_t>(maxSize, 1);

    for (std::size_t seed = 0; seed < spots.size(); ++seed)
    {
        if (taken[seed])
            continue;
        std::vector<std::size_t> pack{ seed };
        taken[seed] = true;

        for (std::size_t m = 0; m < pack.size() && pack.size() < maxSize; ++m)
        {
            SpawnSpot const& a = spots[pack[m]];
            for (std::size_t j = 0; j < spots.size() && pack.size() < maxSize; ++j)
            {
                if (taken[j])
                    continue;
                float const dx = spots[j].X - a.X, dy = spots[j].Y - a.Y, dz = spots[j].Z - a.Z;
                // A floor apart is another pack, however close on the map.
                if (std::fabs(dz) > 6.0f || dx * dx + dy * dy > r2)
                    continue;
                taken[j] = true;
                pack.push_back(j);
            }
        }
        packs.push_back(std::move(pack));
    }
    return packs;
}

} // namespace Atlas

#endif
