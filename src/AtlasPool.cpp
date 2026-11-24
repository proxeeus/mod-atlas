/*
 * mod-atlas — AtlasPool.cpp
 * AtlasMgr: the generated-map spike (DESIGN §6.2). A pool map (950-999) has
 * an empty WDT in the Atlas patch: no terrain, no WMO, no server map, vmap or
 * mmap files. Kit pieces from atlas_pool_piece are placed in each instance as
 * gameobjects (type 14, a stock WMO with a stock display, so the server has
 * their collision model in GameObjectModels.dtree). `.atlas spike` takes a
 * player who has the patch, with the bots beside them, into one; `.atlas
 * spike leave` brings them back.
 *
 * Arrivals are rooted until the piece has surely loaded on the client (the
 * gameobject comes with the map's first update), then put on its floor.
 */

#include "AtlasMgr.h"
#include "AtlasClones.h"
#include "AtlasConfig.h"
#include "Chat.h"
#include "DBCStores.h"
#include "DatabaseEnv.h"
#include "Field.h"
#include "GameObject.h"
#include "Group.h"
#include "InstanceSaveMgr.h"
#include "Log.h"
#include "Map.h"
#include "ObjectAccessor.h"
#include "ObjectMgr.h"
#include "Player.h"
#include "QueryResult.h"
#include "StringFormat.h"
#include "WorldSession.h"
#include <cmath>

namespace Atlas
{

namespace
{
    // The piece outlives any test; the instance unloads long before.
    constexpr uint32 PIECE_LIFETIME = 4 * 3600;
    // Long enough for the client to load the WMO after the loading screen.
    constexpr auto ARRIVAL_ROOT = std::chrono::seconds(5);

    void Tell(Player* player, std::string const& text)
    {
        if (player && player->GetSession())
            ChatHandler(player->GetSession()).SendSysMessage(text);
    }
}

bool AtlasMgr::IsPoolMap(uint32 mapId) const
{
    if (mapId < POOL_MAP_ID_MIN || mapId > POOL_MAP_ID_MAX)
        return false;
    for (PoolPiece const& p : _poolPieces)
        if (p.MapId == mapId)
            return true;
    return false;
}

void AtlasMgr::LoadPoolPieces()
{
    _poolPieces.clear();
    if (!WorldDatabase.Query("SELECT 1 FROM information_schema.tables WHERE table_schema = DATABASE() "
            "AND table_name = 'atlas_pool_piece'"))
        return;

    QueryResult result = WorldDatabase.Query("SELECT go_entry, map, x, y, z, o, entrance_x, entrance_y, entrance_z "
        "FROM atlas_pool_piece ORDER BY go_entry");
    if (!result)
        return;
    do
    {
        Field* f = result->Fetch();
        PoolPiece p;
        p.Entry = f[0].Get<uint32>();
        p.MapId = f[1].Get<uint32>();
        p.Pos.Relocate(f[2].Get<float>(), f[3].Get<float>(), f[4].Get<float>(), f[5].Get<float>());
        p.Entrance.Relocate(f[6].Get<float>(), f[7].Get<float>(), f[8].Get<float>(), f[5].Get<float>());

        MapEntry const* map = sMapStore.LookupEntry(p.MapId);
        GameObjectTemplate const* go = sObjectMgr->GetGameObjectTemplate(p.Entry);
        if (p.MapId < POOL_MAP_ID_MIN || p.MapId > POOL_MAP_ID_MAX || !map || !map->IsDungeon()
            || !sObjectMgr->GetInstanceTemplate(p.MapId) || !go || go->type != GAMEOBJECT_TYPE_MAP_OBJECT)
        {
            LOG_ERROR("module", "Atlas: pool piece {} on map {} is not usable (pool map rows, instance template or "
                "a type-14 gameobject_template missing).", p.Entry, p.MapId);
            continue;
        }
        _poolPieces.push_back(p);
        LOG_INFO("module", "Atlas: pool piece {} (display {}) on map {} at ({:.1f}, {:.1f}, {:.1f}).", p.Entry,
            go->displayId, p.MapId, p.Pos.GetPositionX(), p.Pos.GetPositionY(), p.Pos.GetPositionZ());
    } while (result->NextRow());
}

void AtlasMgr::ProcessPoolMap(Map* map)
{
    if (!map || _poolPieces.empty() || !IsPoolMap(map->GetId()))
        return;
    {
        std::lock_guard<std::mutex> lock(_poolMutex);
        if (!_poolSpawned.insert(map->GetInstanceId()).second)
            return;
    }
    for (PoolPiece const& p : _poolPieces)
    {
        if (p.MapId != map->GetId())
            continue;
        float const o = p.Pos.GetOrientation();
        GameObject* go = map->SummonGameObject(p.Entry, p.Pos.GetPositionX(), p.Pos.GetPositionY(),
            p.Pos.GetPositionZ(), o, 0.0f, 0.0f, std::sin(o / 2.0f), std::cos(o / 2.0f), PIECE_LIFETIME, false);
        if (!go)
        {
            LOG_ERROR("module", "Atlas: could not place pool piece {} in instance {}.", p.Entry, map->GetInstanceId());
            continue;
        }
        // A room is wider than the normal sight range of one object.
        go->SetVisibilityDistanceOverride(VisibilityDistanceType::Gigantic);
        LOG_INFO("module", "Atlas: pool piece {} placed in map {} instance {}.", p.Entry, map->GetId(),
            map->GetInstanceId());
    }
}

void AtlasMgr::OnPlayerEnteredPool(Player* player)
{
    if (!player || !IsPoolMap(player->GetMapId()))
        return;

    player->SetControlled(true, UNIT_STATE_ROOT);
    ObjectGuid const guid = player->GetGUID();
    player->m_Events.AddEventAtOffset([guid]()
    {
        Player* p = ObjectAccessor::FindPlayer(guid);
        if (!p || !p->IsInWorld())
            return;
        // On the piece's floor (the server sees it through the gameobject's
        // collision model), in case the client fell a little before it loaded.
        float const floor = p->GetMap()->GetHeight(p->GetPhaseMask(), p->GetPositionX(), p->GetPositionY(),
            p->GetPositionZ() + 4.0f, true, 50.0f);
        if (floor > INVALID_HEIGHT && std::fabs(p->GetPositionZ() - floor) > 1.0f)
            p->NearTeleportTo(p->GetPositionX(), p->GetPositionY(), floor + 0.5f, p->GetOrientation());
        p->SetControlled(false, UNIT_STATE_ROOT);
        LOG_INFO("module", "Atlas: pool arrival {} released at ({:.1f}, {:.1f}, {:.1f}), floor {:.1f}.", p->GetName(),
            p->GetPositionX(), p->GetPositionY(), p->GetPositionZ(), floor);
    }, ARRIVAL_ROOT);
}

bool AtlasMgr::EnterSpike(Player* player, std::string& error)
{
    if (!sAtlasConfig->IsClonesEnabled())
    {
        error = "Clone and pool maps are off on this realm (Atlas.Clones.Enable).";
        return false;
    }
    if (!IsCloneOptedIn(player->GetSession()->GetAccountId()))
    {
        error = "The Timeways need the Atlas client patch: see .atlas clones.";
        return false;
    }
    if (_poolPieces.empty())
    {
        error = "No kit piece is loaded (atlas_pool.sql not installed?).";
        return false;
    }
    if (GetSessionByPlayer(player->GetGUID()))
    {
        error = "Close your echo first.";
        return false;
    }
    if (IsPoolMap(player->GetMapId()))
    {
        error = "You are already in the Timeways (.atlas spike leave).";
        return false;
    }
    if (player->IsInCombat() || !player->IsAlive())
    {
        error = "Not now: you must be alive and out of combat.";
        return false;
    }

    PoolPiece const& piece = _poolPieces.front();

    // The player, and the bots of their group standing in the same place.
    std::vector<Player*> travelers{ player };
    if (Group* g = player->GetGroup())
        for (GroupReference* ref = g->GetFirstMember(); ref; ref = ref->next())
        {
            Player* m = ref->GetSource();
            if (m && m != player && m->IsInWorld() && !IsRealPlayer(m) && m->GetMapId() == player->GetMapId()
                && m->IsAlive())
                travelers.push_back(m);
        }

    for (Player* t : travelers)
    {
        {
            std::lock_guard<std::mutex> lock(_poolMutex);
            _spikeReturns[t->GetGUID()] = WorldLocation(t->GetMapId(), t->GetPositionX(), t->GetPositionY(),
                t->GetPositionZ(), t->GetOrientation());
        }
        // A fresh instance each time.
        if (sInstanceSaveMgr->PlayerGetBoundInstance(t->GetGUID(), piece.MapId, DUNGEON_DIFFICULTY_NORMAL))
            sInstanceSaveMgr->PlayerUnbindInstance(t->GetGUID(), piece.MapId, DUNGEON_DIFFICULTY_NORMAL, true, t);
        t->TeleportTo(piece.MapId, piece.Entrance.GetPositionX(), piece.Entrance.GetPositionY(),
            piece.Entrance.GetPositionZ(), piece.Entrance.GetOrientation());
    }
    LOG_INFO("module", "Atlas: spike — {} and {} bot(s) to pool map {}.", player->GetName(), travelers.size() - 1,
        piece.MapId);
    return true;
}

void AtlasMgr::LeaveSpike(Player* player)
{
    std::vector<Player*> travelers{ player };
    if (Group* g = player->GetGroup())
        for (GroupReference* ref = g->GetFirstMember(); ref; ref = ref->next())
        {
            Player* m = ref->GetSource();
            if (m && m != player && m->IsInWorld() && !IsRealPlayer(m) && IsPoolMap(m->GetMapId()))
                travelers.push_back(m);
        }

    for (Player* t : travelers)
    {
        WorldLocation back(t->m_homebindMapId, t->m_homebindX, t->m_homebindY, t->m_homebindZ, t->GetOrientation());
        {
            std::lock_guard<std::mutex> lock(_poolMutex);
            auto it = _spikeReturns.find(t->GetGUID());
            if (it != _spikeReturns.end())
            {
                back = it->second;
                _spikeReturns.erase(it);
            }
        }
        uint32 const poolMap = t->GetMapId();
        t->SetControlled(false, UNIT_STATE_ROOT);
        t->TeleportTo(back);
        if (sInstanceSaveMgr->PlayerGetBoundInstance(t->GetGUID(), poolMap, DUNGEON_DIFFICULTY_NORMAL))
            sInstanceSaveMgr->PlayerUnbindInstance(t->GetGUID(), poolMap, DUNGEON_DIFFICULTY_NORMAL, true, t);
    }
    Tell(player, Acore::StringFormat("{} The Timeways let you go.", CHAT_PREFIX));
}

} // namespace Atlas
