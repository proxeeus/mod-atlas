/*
 * mod-atlas — StockLayoutProvider.h
 * Layouts of the stock dungeons V0 echoes run in, read from the world DB.
 */

#ifndef ATLAS_STOCK_LAYOUT_PROVIDER_H
#define ATLAS_STOCK_LAYOUT_PROVIDER_H

#include "AtlasLayout.h"

#include <mutex>
#include <unordered_map>

namespace Atlas
{

struct DungeonInfo;

class StockLayoutProvider : public LayoutProvider
{
public:
    // Builds every dungeon's layout. Entrances come from areatrigger_teleport,
    // anchors from the dungeon's creature spawns, arenas from its bosses. The
    // world tables don't change while the server runs, so this happens at
    // startup and on `.atlas reload`, never during play.
    void Load(std::vector<DungeonInfo> const& dungeons);

    std::shared_ptr<Layout const> GetLayout(uint32 mapId) const override;

private:
    static Position QueryEntrance(uint32 mapId);
    static std::shared_ptr<Layout const> Build(uint32 mapId);

    std::unordered_map<uint32, std::shared_ptr<Layout const>> _layouts;
    mutable std::mutex _mutex;
};

} // namespace Atlas

#endif
