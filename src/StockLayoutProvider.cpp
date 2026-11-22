/*
 * mod-atlas — StockLayoutProvider.cpp
 * Layouts of the stock dungeons V0 echoes run in (see AtlasLayout.h).
 */

#include "StockLayoutProvider.h"
#include "AtlasBossSpawnQuery.h"
#include "AtlasConfig.h"
#include "AtlasTheme.h"
#include "Creature.h"
#include "DBCStores.h"
#include "ObjectMgr.h"
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

bool IsFitToFight(uint32 entry, bool requireHostile)
{
    // Display IDs whose CreatureDisplayInfoExtra has an empty BakeName: the
    // 3.3.5 client crashes (Error #132) when one is spawned in view.
    static constexpr uint32 CRASH_DISPLAYS[] = {
        15331, 15332, 15444, 17116, 20392, 21951, 23290, 23296, 23836, 23886, 29323 };

    CreatureTemplate const* t = sObjectMgr->GetCreatureTemplate(entry);
    if (!t || t->Models.empty())
        return false;
    if (t->type == CREATURE_TYPE_CRITTER || t->HasFlagsExtra(CREATURE_FLAG_EXTRA_TRIGGER))
        return false;
    if (t->unit_flags & (UNIT_FLAG_NOT_SELECTABLE | UNIT_FLAG_NON_ATTACKABLE))
        return false;

    if (requireHostile)
    {
        FactionTemplateEntry const* faction = sFactionTemplateStore.LookupEntry(t->faction);
        if (!faction || !faction->IsHostileToPlayers())
            return false;
    }

    for (CreatureModel const& model : t->Models)
    {
        if (!model.CreatureDisplayID)
            return false;
        for (uint32 bad : CRASH_DISPLAYS)
            if (model.CreatureDisplayID == bad)
                return false;
        CreatureModelInfo const* info = sObjectMgr->GetCreatureModelInfo(model.CreatureDisplayID);
        if (!info || info->bounding_radius <= 0.0f || info->combat_reach <= 0.0f)
            return false;
    }
    return true;
}

std::shared_ptr<Layout const> StockLayoutProvider::Build(uint32 mapId)
{
    auto layout = std::make_shared<Layout>();
    layout->MapId = mapId;
    layout->Entrance = QueryEntrance(mapId);

    float const ex = layout->Entrance.GetPositionX();
    float const ey = layout->Entrance.GetPositionY();
    float const ez = layout->Entrance.GetPositionZ();
    auto distance = [ex, ey, ez](Position const& p)
    {
        float const dx = p.GetPositionX() - ex, dy = p.GetPositionY() - ey, dz = p.GetPositionZ() - ez;
        return std::sqrt(dx * dx + dy * dy + dz * dz);
    };

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
            bc.Dist = distance(bc.Pos);
            bosses.push_back(bc);
        } while (bossResult->NextRow());
    }

    std::stable_sort(bosses.begin(), bosses.end(),
        [](BossCandidate const& a, BossCandidate const& b) { return a.Dist > b.Dist; });
    for (uint32 i = 0; i < bossCount && i < bosses.size(); ++i)
    {
        LayoutArena arena;
        arena.Centre = bosses[i].Pos;
        arena.DistanceFromEntrance = bosses[i].Dist;
        arena.NativeEntry = IsFitToFight(bosses[i].Entry) ? bosses[i].Entry : 0;
        layout->Arenas.push_back(arena);
    }
    if (!bosses.empty())
        LOG_INFO("module", "Atlas: Map {} — found {} boss candidate(s). Last boss spawn entry {} at "
            "({:.1f}, {:.1f}, {:.1f}), MechanicsMask={}, dist={:.1f}",
            mapId, bosses.size(), bosses[0].Entry, bosses[0].Pos.GetPositionX(), bosses[0].Pos.GetPositionY(),
            bosses[0].Pos.GetPositionZ(), bosses[0].MechanicsMask, bosses[0].Dist);

    // Every other stock spawn is a pack spot, nearest first; the boss's own
    // spot belongs to its arena.
    struct Spot
    {
        LayoutSpot Spot;
        float      Dist = 0.0f;
    };
    std::vector<Spot> spots;
    if (QueryResult result = WorldDatabase.Query(
            "SELECT position_x, position_y, position_z, orientation, id FROM creature WHERE map = {} ORDER BY guid",
            mapId))
    {
        do
        {
            Field* f = result->Fetch();
            Spot s;
            s.Spot.Pos.Relocate(f[0].Get<float>(), f[1].Get<float>(), f[2].Get<float>(), f[3].Get<float>());
            bool atArena = false;
            for (LayoutArena const& arena : layout->Arenas)
                if (arena.Centre.GetExactDist(&s.Spot.Pos) < 3.0f)
                    atArena = true;
            if (atArena)
                continue;
            uint32 const entry = f[4].Get<uint32>();
            s.Spot.NativeEntry = IsFitToFight(entry) ? entry : 0;
            s.Dist = distance(s.Spot.Pos);
            spots.push_back(s);
        } while (result->NextRow());
    }
    std::stable_sort(spots.begin(), spots.end(), [](Spot const& a, Spot const& b) { return a.Dist < b.Dist; });

    // Spots that stood together become one pack.
    std::vector<SpawnSpot> points;
    points.reserve(spots.size());
    for (Spot const& s : spots)
        points.push_back({ s.Spot.Pos.GetPositionX(), s.Spot.Pos.GetPositionY(), s.Spot.Pos.GetPositionZ() });
    for (std::vector<std::size_t> const& pack : ClusterSpots(points, sAtlasConfig->GetPackRadius(),
             sAtlasConfig->GetMaxPackSize()))
    {
        LayoutAnchor anchor;
        anchor.Pos = spots[pack.front()].Spot.Pos;
        anchor.DistanceFromEntrance = spots[pack.front()].Dist;
        for (std::size_t i : pack)
        {
            anchor.Spots.push_back(spots[i].Spot);
            anchor.Radius = std::max(anchor.Radius, anchor.Pos.GetExactDist(&spots[i].Spot.Pos));
        }
        anchor.PackBudget = static_cast<uint32>(anchor.Spots.size());
        layout->Anchors.push_back(std::move(anchor));
    }

    // No boss in the DB: the farthest packs become arenas instead, their
    // first creature the native boss.
    if (layout->Arenas.empty())
    {
        LOG_WARN("module", "Atlas: Map {} — no boss creatures found in DB, "
            "falling back to farthest spawn points.", mapId);

        std::size_t const take = std::min<std::size_t>(bossCount, layout->Anchors.size());
        for (std::size_t i = layout->Anchors.size() - take; i < layout->Anchors.size(); ++i)
        {
            LayoutArena arena;
            arena.Centre = layout->Anchors[i].Pos;
            arena.DistanceFromEntrance = layout->Anchors[i].DistanceFromEntrance;
            arena.NativeEntry = layout->Anchors[i].Spots.front().NativeEntry;
            layout->Arenas.push_back(arena);
        }
        layout->Anchors.resize(layout->Anchors.size() - take);
    }

    return layout;
}

} // namespace Atlas
