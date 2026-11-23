/*
 * mod-atlas — AtlasForces.h
 * The echo's forces (pure math, unit-tested): its master is not waiting in a
 * room to be found. The run's creatures carry a weight (trash, elite, rare);
 * once the party has slain a share of the total (Atlas.Boss.SummonAtForces),
 * the master steps out of the timeways beside them.
 */

#ifndef ATLAS_FORCES_H
#define ATLAS_FORCES_H

#include "Define.h"

#include <algorithm>

namespace Atlas
{

struct ForceWeights
{
    uint32 Trash = 1;
    uint32 Elite = 3;
    uint32 Rare  = 10;
};

// Forces to slay before the master comes: `pct` % of the total, at most
// `cap` (0 = no cap) so a huge dungeon is not a full clear, at least 1.
inline uint32 ForcesThreshold(uint32 total, uint32 pct, uint32 cap = 0)
{
    if (!total)
        return 0;
    uint64 needed = (uint64(total) * std::min<uint32>(pct, 100) + 99) / 100;
    if (cap)
        needed = std::min<uint64>(needed, cap);
    return static_cast<uint32>(std::max<uint64>(needed, 1));
}

// How far the bar is filled, 0-100 (100 with nothing to slay).
inline uint32 ForcesProgress(uint32 killed, uint32 threshold)
{
    if (!threshold)
        return 100;
    return static_cast<uint32>(std::min<uint64>(100, uint64(killed) * 100 / threshold));
}

// Quarter steps of the bar (0-4), for announcements.
inline uint8 ForcesStep(uint32 progress)
{
    return static_cast<uint8>(std::min<uint32>(progress, 100) / 25);
}

} // namespace Atlas

#endif
