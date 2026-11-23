/*
 * mod-atlas — AtlasLoot.h
 * The rewards ladder of map runs (PLAN 1.5, DESIGN §14) as pure data and
 * math, so the item levels, sigil sustain and the affixes' quantity and
 * rarity can be unit-tested.
 *
 * Gear: each tier draws from an item-level band of the existing WotLK gear
 * (200 at tiers 1-3 up to 251-264 at 16), epics more often as tiers rise.
 * Sigils: the boss gives one (mostly the run's tier or one lower), the cache
 * sometimes a second, each cleared pack a chance that grows with the forces
 * it held (so a dungeon of lone creatures drops no more than one of big
 * packs); an account's first
 * clear of a dungeon at a tier adds one of the next tier. Quantity and rarity
 * (affix bonuses, capped by config) scale counts, chances, epics and sigil
 * rarity.
 */

#ifndef ATLAS_LOOT_H
#define ATLAS_LOOT_H

#include "AtlasMapItem.h"
#include "Define.h"
#include "RunRng.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <vector>

namespace Atlas
{

// One tier's gear (world table atlas_loot_tier; these are its defaults).
struct LootTier
{
    uint16 MinItemLevel = 200;
    uint16 MaxItemLevel = 200;
    uint32 EpicShare    = 30;   // % of gear that is epic, before rarity
    uint32 GearCount    = 1;    // gear pieces in each player's share of the cache
};

// The DESIGN §14 ladder: Naxx-10 / heroic dungeons, Naxx-25, Ulduar, Trial of
// the Crusader, Icecrown.
inline LootTier DefaultLootTier(uint8 tier)
{
    static constexpr std::array<std::array<uint16, 2>, MAP_MAX_TIER> bands = { {
        { 200, 200 }, { 200, 200 }, { 200, 200 }, { 200, 213 }, { 200, 213 },
        { 213, 213 }, { 213, 219 }, { 219, 219 }, { 219, 226 }, { 226, 226 },
        { 232, 232 }, { 232, 239 }, { 239, 239 }, { 239, 245 }, { 245, 245 },
        { 251, 264 },
    } };
    uint8 const t = std::clamp<uint8>(tier, 1, MAP_MAX_TIER);
    LootTier lt;
    lt.MinItemLevel = bands[t - 1][0];
    lt.MaxItemLevel = bands[t - 1][1];
    lt.EpicShare    = std::min<uint32>(100, 30 + 6 * (t - 1));
    lt.GearCount    = t >= 11 ? 2 : 1;
    return lt;
}

// A chance in per mille, raised by a bonus in %.
inline uint32 ScaleChance(uint32 permille, uint32 bonusPct)
{
    return std::min<uint32>(1000, static_cast<uint32>(uint64(permille) * (100 + bonusPct) / 100));
}

inline bool RollPermille(uint32 permille, RunRng& rng)
{
    return permille && rng.Range(1, 1000) <= permille;
}

// `base` items raised by a quantity bonus in %: the whole part always, the
// rest as a chance (1 item at +50% gives 1, and 2 half the time).
inline uint32 RollQuantity(uint32 base, uint32 quantityPct, RunRng& rng)
{
    uint64 const scaled = uint64(base) * (100 + quantityPct);   // in hundredths
    uint32 count = static_cast<uint32>(scaled / 100);
    if (RollPermille(static_cast<uint32>((scaled % 100) * 10), rng))
        ++count;
    return count;
}

// Epic share of a tier's gear with a rarity bonus.
inline uint32 ScaleEpicShare(uint32 epicShare, uint32 rarityPct)
{
    return std::min<uint32>(100, static_cast<uint32>(uint64(epicShare) * (100 + rarityPct) / 100));
}

// Normal, magic, rare sigil weights with a rarity bonus: magic and rare grow.
inline std::array<uint32, 3> ScaleRarityWeights(std::array<uint32, 3> weights, uint32 rarityPct)
{
    weights[1] = static_cast<uint32>(uint64(weights[1]) * (100 + rarityPct) / 100);
    weights[2] = static_cast<uint32>(uint64(weights[2]) * (100 + rarityPct) / 100);
    return weights;
}

// A dropped sigil's tier, weights for one lower, the same, one and two higher.
// At tier 1 "lower" stays at 1; at 16 the higher ones stay at 16.
inline uint8 RollSigilTier(uint8 runTier, std::array<uint32, 4> const& shift, RunRng& rng)
{
    uint32 const total = shift[0] + shift[1] + shift[2] + shift[3];
    int32 delta = 0;
    if (total)
    {
        uint32 const draw = rng.Range(1, total);
        if (draw <= shift[0])
            delta = -1;
        else if (draw <= shift[0] + shift[1])
            delta = 0;
        else if (draw <= shift[0] + shift[1] + shift[2])
            delta = 1;
        else
            delta = 2;
    }
    return static_cast<uint8>(std::clamp<int32>(int32(runTier) + delta, 1, MAP_MAX_TIER));
}

struct SigilDropRules
{
    std::array<uint32, 4> TierShift{ { 45, 45, 9, 1 } };
    uint32 ExtraPermille  = 200;   // a second sigil in the cache
    uint32 ForcePer100k   = 130;   // a cleared pack's chance per force point it held (0.13%)
};

// A cleared pack's sigil chance, per 100,000: its forces times the chance per
// force point, raised by quantity, at most certain.
inline uint32 PackSigilChance(uint32 packForce, uint32 forcePer100k, uint32 quantityPct)
{
    uint64 const chance = uint64(packForce) * forcePer100k * (100 + quantityPct) / 100;
    return static_cast<uint32>(std::min<uint64>(chance, 100000));
}

inline bool RollPer100k(uint32 chance, RunRng& rng)
{
    return chance && rng.Range(1, 100000) <= chance;
}

// Tiers of the sigils one player takes from a cleared run (the boss's, the
// cache's extra, the packs', each pack given by the forces it held), before
// the first-clear bonus. For tests and the sustain target; the server rolls
// the same pieces as they happen.
inline std::vector<uint8> RollRunSigils(uint8 runTier, std::vector<uint32> const& packForces, uint32 quantityPct,
    SigilDropRules const& rules, RunRng& rng)
{
    std::vector<uint8> tiers;
    tiers.push_back(RollSigilTier(runTier, rules.TierShift, rng));
    if (RollPermille(ScaleChance(rules.ExtraPermille, quantityPct), rng))
        tiers.push_back(RollSigilTier(runTier, rules.TierShift, rng));
    for (uint32 force : packForces)
        if (RollPer100k(PackSigilChance(force, rules.ForcePer100k, quantityPct), rng))
            tiers.push_back(RollSigilTier(runTier, rules.TierShift, rng));
    return tiers;
}

} // namespace Atlas

#endif
