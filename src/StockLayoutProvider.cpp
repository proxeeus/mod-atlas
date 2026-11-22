/*
 * mod-atlas — StockLayoutProvider.cpp
 * Layouts of the stock dungeons V0 echoes run in (see AtlasLayout.h).
 */

#include "StockLayoutProvider.h"
#include "AtlasBossSpawnQuery.h"
#include "AtlasConfig.h"
#include "DatabaseEnv.h"
#include "Field.h"
#include "Log.h"
#include "QueryResult.h"
#include <algorithm>
#include <cmath>

namespace Atlas
{

void StockLayoutProvider::Load(std::vector<DungeonInfo> const& dungeons)
{
    std::unordered_map<uint32, std::shared_ptr<Layout const>> layouts;
    for (DungeonInfo const& dg : dungeons)
        layouts[dg.MapId] = Build(dg.MapId);

    std::lock_guard<std::mutex> lock(_mutex);
    _layouts = std::move(layouts);

    LOG_INFO("module", "Atlas: Cached stock layouts (entrance, spawn points, bosses) for {} dungeons.",
        _layouts.size());
}

std::shared_ptr<Layout const> StockLayoutProvider::GetLayout(uint32 mapId) const
{
    std::lock_guard<std::mutex> lock(_mutex);
    auto itr = _layouts.find(mapId);
    return itr != _layouts.end() ? itr->second : nullptr;
}

// Dungeon entrance from areatrigger_teleport. Some dungeons have several
// (Stratholme, Scarlet Monastery): take the lowest trigger ID, so the entrance
// is the same on every start (C8).
Position StockLayoutProvider::QueryEntrance(uint32 mapId)
{
    if (QueryResult r = WorldDatabase.Query(
            "SELECT target_position_x, target_position_y, target_position_z, target_orientation "
            "FROM areatrigger_teleport WHERE target_map = {} ORDER BY ID LIMIT 1", mapId))
    {
        Field* f = r->Fetch();
        return { f[0].Get<float>(), f[1].Get<float>(), f[2].Get<float>(), f[3].Get<float>() };
    }
    LOG_WARN("module", "Atlas: No areatrigger_teleport for map {}", mapId);
    return { 0, 0, 0, 0 };
}

std::shared_ptr<Layout const> StockLayoutProvider::Build(uint32 mapId)
{
    auto layout = std::make_shared<Layout>();
    layout->MapId = mapId;
    layout->Entrance = QueryEntrance(mapId);

    float const ex = layout->Entrance.GetPositionX();
    float const ey = layout->Entrance.GetPositionY();
    float const ez = layout->Entrance.GetPositionZ();
    auto distance = [ex, ey, ez](float x, float y, float z)
    {
        float const dx = x - ex, dy = y - ey, dz = z - ez;
        return std::sqrt(dx * dx + dy * dy + dz * dz);
    };

    // One anchor per stock creature spawn, nearest first.
    if (QueryResult result = WorldDatabase.Query(
            "SELECT position_x, position_y, position_z, orientation FROM creature WHERE map = {} ORDER BY guid",
            mapId))
    {
        do
        {
            Field* f = result->Fetch();
            LayoutAnchor anchor;
            anchor.Pos.Relocate(f[0].Get<float>(), f[1].Get<float>(), f[2].Get<float>(), f[3].Get<float>());
            anchor.DistanceFromEntrance = distance(f[0].Get<float>(), f[1].Get<float>(), f[2].Get<float>());
            layout->Anchors.push_back(anchor);
        } while (result->NextRow());
    }

    std::stable_sort(layout->Anchors.begin(), layout->Anchors.end(),
        [](LayoutAnchor const& a, LayoutAnchor const& b)
        { return a.DistanceFromEntrance < b.DistanceFromEntrance; });

    uint32 const bossCount = sAtlasConfig->GetBossCount();

    // Arenas at the stock bosses' positions; the "last boss" is the boss-type
    // creature farthest from the entrance.
    struct BossCandidate
    {
        Position Pos;
        float    Dist = 0.0f;
        uint64   MechanicsMask = 0;
        uint32   Entry = 0;
    };
    std::vector<BossCandidate> bosses;
    if (QueryResult bossResult = WorldDatabase.Query(BuildBossSpawnPointQuery(mapId).c_str()))
    {
        do
        {
            Field* f = bossResult->Fetch();
            BossCandidate bc;
            bc.Pos.Relocate(f[0].Get<float>(), f[1].Get<float>(), f[2].Get<float>(), f[3].Get<float>());
            bc.MechanicsMask = f[4].Get<uint64>();
            bc.Entry = f[6].Get<uint32>();
            bc.Dist = distance(f[0].Get<float>(), f[1].Get<float>(), f[2].Get<float>());
            bosses.push_back(bc);
        } while (bossResult->NextRow());
    }

    if (!bosses.empty())
    {
        std::stable_sort(bosses.begin(), bosses.end(),
            [](BossCandidate const& a, BossCandidate const& b) { return a.Dist > b.Dist; });

        BossCandidate const& lastBoss = bosses[0];
        LOG_INFO("module", "Atlas: Map {} — found {} boss candidate(s). "
            "Last boss spawn entry {} at ({:.1f}, {:.1f}, {:.1f}), MechanicsMask={}, dist={:.1f}",
            mapId, bosses.size(), lastBoss.Entry, lastBoss.Pos.GetPositionX(), lastBoss.Pos.GetPositionY(),
            lastBoss.Pos.GetPositionZ(), lastBoss.MechanicsMask, lastBoss.Dist);

        for (uint32 i = 0; i < bossCount && i < bosses.size(); ++i)
        {
            LayoutArena arena;
            arena.Centre = bosses[i].Pos;
            arena.DistanceFromEntrance = bosses[i].Dist;
            layout->Arenas.push_back(arena);
        }
    }
    else
    {
        // No boss in the DB: the farthest spawn points become arenas instead of
        // anchors, kept in near-to-far order.
        LOG_WARN("module", "Atlas: Map {} — no boss creatures found in DB, "
            "falling back to farthest spawn points.", mapId);

        std::size_t const take = std::min<std::size_t>(bossCount, layout->Anchors.size());
        for (std::size_t i = layout->Anchors.size() - take; i < layout->Anchors.size(); ++i)
        {
            LayoutArena arena;
            arena.Centre = layout->Anchors[i].Pos;
            arena.DistanceFromEntrance = layout->Anchors[i].DistanceFromEntrance;
            layout->Arenas.push_back(arena);
        }
        layout->Anchors.resize(layout->Anchors.size() - take);
    }

    return layout;
}

} // namespace Atlas
