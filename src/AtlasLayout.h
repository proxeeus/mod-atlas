/*
 * mod-atlas — AtlasLayout.h
 * The provider-neutral description of where a run happens (DESIGN §5).
 *
 * Only a LayoutProvider knows where a layout comes from: StockLayoutProvider
 * reads a stock dungeon's own spawns (V0 echoes); a CloneLayoutProvider will
 * read frozen harvested layouts for clone maps (PLAN stage 2), and a
 * KitLayoutProvider generated ones (DESIGN §6.2). Everything downstream
 * consumes a Layout and never asks which.
 */

#ifndef ATLAS_LAYOUT_H
#define ATLAS_LAYOUT_H

#include "Define.h"
#include "Position.h"

#include <memory>
#include <vector>

namespace Atlas
{

// One creature's place in a pack, with who stood there in the stock dungeon
// (0 when that creature isn't fit to fight: friendly, a critter, a trigger,
// an invisible model). The native timeline puts that creature back.
struct LayoutSpot
{
    Position Pos;
    uint32   NativeEntry = 0;
};

// A pack: stock spawns that stood together (clustered), nearest first. The
// pack budget is how many creatures it holds.
struct LayoutAnchor
{
    Position                Pos;                        // the first spot's
    float                   DistanceFromEntrance = 0.0f;
    float                   Radius               = 0.0f;
    uint32                  PackBudget           = 1;
    std::vector<LayoutSpot> Spots;
};

// Where a boss fights, and the stock boss that fought there.
struct LayoutArena
{
    Position Centre;
    float    DistanceFromEntrance = 0.0f;
    float    Radius               = 0.0f;
    uint32   NativeEntry          = 0;
};

struct Layout
{
    uint32                    MapId = 0;
    Position                  Entrance;
    std::vector<LayoutAnchor> Anchors;   // nearest to the entrance first
    std::vector<LayoutArena>  Arenas;    // in the order bosses are placed

    bool HasEntrance() const
    {
        return Entrance.GetPositionX() != 0.0f || Entrance.GetPositionY() != 0.0f
            || Entrance.GetPositionZ() != 0.0f;
    }
};

class LayoutProvider
{
public:
    virtual ~LayoutProvider() = default;

    // The layout of a run on this map, or nullptr when the provider has none.
    // Layouts are immutable once built; holding the pointer is safe.
    virtual std::shared_ptr<Layout const> GetLayout(uint32 mapId) const = 0;
};

} // namespace Atlas

#endif
