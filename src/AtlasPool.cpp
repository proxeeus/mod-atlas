/*
 * mod-atlas — AtlasPool.cpp
 * AtlasMgr: generated maps (DESIGN §6.2). A pool map (950-999) has no terrain:
 * its WDT in the Atlas patch holds only a global WMO out of sight (for the
 * client's zone), and there are no server map, vmap or mmap files. Each
 * instance gets its own layout of kit pieces (atlas_pool_piece, joined socket
 * to socket: AtlasKit.h, atlas_pool_socket) from its pool map's start piece at
 * its origin (atlas_pool_map), placed as gameobjects (type 14, a stock WMO with
 * a stock display, so the server has their collision model in
 * GameObjectModels.dtree). `.atlas spike` takes a player who has the patch,
 * with the bots beside them, into one; `.atlas spike leave` brings them back.
 *
 * Arrivals are rooted until the pieces have surely loaded on the client (they
 * come with the map's first update), then put on the floor.
 */

#include "AtlasMgr.h"
#include "AtlasClones.h"
#include "AtlasKit.h"
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
#include "RunRng.h"
#include "StringFormat.h"
#include "WorldSession.h"
#include <algorithm>
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

AtlasMgr::PoolMap const* AtlasMgr::GetPoolMap(uint32 mapId) const
{
    if (mapId < POOL_MAP_ID_MIN || mapId > POOL_MAP_ID_MAX)
        return nullptr;
    for (PoolMap const& m : _poolMaps)
        if (m.MapId == mapId)
            return &m;
    return nullptr;
}

bool AtlasMgr::IsPoolMap(uint32 mapId) const
{
    return GetPoolMap(mapId) != nullptr;
}

void AtlasMgr::LoadPoolPieces()
{
    _poolKit.clear();
    _poolMaps.clear();
    if (!WorldDatabase.Query("SELECT 1 FROM information_schema.tables WHERE table_schema = DATABASE() "
            "AND table_name = 'atlas_pool_map'"))
        return;

    if (QueryResult result = WorldDatabase.Query("SELECT go_entry, entrance_x, entrance_y, entrance_z, min_x, min_y, "
            "max_x, max_y FROM atlas_pool_piece ORDER BY go_entry"))
    {
        do
        {
            Field* f = result->Fetch();
            KitPiece p;
            p.Entry = f[0].Get<uint32>();
            p.EntranceX = f[1].Get<float>();
            p.EntranceY = f[2].Get<float>();
            p.EntranceZ = f[3].Get<float>();
            p.MinX = f[4].Get<float>();
            p.MinY = f[5].Get<float>();
            p.MaxX = f[6].Get<float>();
            p.MaxY = f[7].Get<float>();
            GameObjectTemplate const* go = sObjectMgr->GetGameObjectTemplate(p.Entry);
            if (!go || go->type != GAMEOBJECT_TYPE_MAP_OBJECT)
            {
                LOG_ERROR("module", "Atlas: kit piece {} skipped: no type-14 gameobject_template.", p.Entry);
                continue;
            }
            _poolKit.push_back(p);
        } while (result->NextRow());
    }

    if (QueryResult result = WorldDatabase.Query("SELECT go_entry, x, y, z, facing FROM atlas_pool_socket "
            "ORDER BY go_entry, socket"))
    {
        do
        {
            Field* f = result->Fetch();
            uint32 const entry = f[0].Get<uint32>();
            auto it = std::find_if(_poolKit.begin(), _poolKit.end(), [entry](KitPiece const& p)
            {
                return p.Entry == entry;
            });
            if (it == _poolKit.end())
                continue;
            it->Sockets.push_back({ f[1].Get<float>(), f[2].Get<float>(), f[3].Get<float>(),
                f[4].Get<float>() * float(M_PI) / 180.0f });
        } while (result->NextRow());
    }

    if (QueryResult result = WorldDatabase.Query("SELECT map, origin_x, origin_y, origin_z, origin_o, start, "
            "max_pieces FROM atlas_pool_map ORDER BY map"))
    {
        do
        {
            Field* f = result->Fetch();
            PoolMap m;
            m.MapId = f[0].Get<uint32>();
            m.Origin = { f[1].Get<float>(), f[2].Get<float>(), f[3].Get<float>(),
                f[4].Get<float>() * float(M_PI) / 180.0f };
            m.Start = f[5].Get<uint32>();
            m.MaxPieces = std::max<uint32>(1, f[6].Get<uint32>());
            MapEntry const* map = sMapStore.LookupEntry(m.MapId);
            if (m.MapId < POOL_MAP_ID_MIN || m.MapId > POOL_MAP_ID_MAX || !map || !map->IsDungeon()
                || !sObjectMgr->GetInstanceTemplate(m.MapId) || !KitFind(_poolKit, m.Start))
            {
                LOG_ERROR("module", "Atlas: pool map {} is not usable (map rows, instance template or its start piece "
                    "{} missing).", m.MapId, m.Start);
                continue;
            }
            _poolMaps.push_back(m);
            LOG_INFO("module", "Atlas: pool map {} — start piece {} at ({:.1f}, {:.1f}, {:.1f}), up to {} pieces, "
                "{} kit piece(s).", m.MapId, m.Start, m.Origin.X, m.Origin.Y, m.Origin.Z, m.MaxPieces,
                _poolKit.size());
        } while (result->NextRow());
    }
}

void AtlasMgr::ProcessPoolMap(Map* map)
{
    // Instances only. OnMapUpdate also runs for the pool map's base map (the
    // MapInstanced parent, instance 0), whose ToInstanceMap() is a bad cast:
    // a gameobject created there reads a garbage instance script and crashes
    // the server in GameObject::Create -> SetZoneScript.
    if (!map || !map->GetInstanceId())
        return;
    PoolMap const* pool = GetPoolMap(map->GetId());
    if (!pool)
        return;
    {
        std::lock_guard<std::mutex> lock(_poolMutex);
        if (!_poolSpawned.insert(map->GetInstanceId()).second)
            return;
    }

    RunRng rng(RunRng::MakeSeed());
    std::vector<KitPlacement> const layout = KitBuildLayout(_poolKit, pool->Start, pool->Origin, pool->MaxPieces, rng);
    uint32 placed = 0;
    for (KitPlacement const& p : layout)
    {
        GameObject* go = map->SummonGameObject(p.Entry, p.X, p.Y, p.Z, p.O, 0.0f, 0.0f, std::sin(p.O / 2.0f),
            std::cos(p.O / 2.0f), PIECE_LIFETIME, false);
        if (!go)
        {
            LOG_ERROR("module", "Atlas: could not place kit piece {} in map {} instance {}.", p.Entry, map->GetId(),
                map->GetInstanceId());
            continue;
        }
        // A room is wider than the normal sight range of one object.
        go->SetVisibilityDistanceOverride(VisibilityDistanceType::Gigantic);
        ++placed;
        LOG_INFO("module", "Atlas: kit piece {} placed in map {} instance {} at ({:.1f}, {:.1f}, {:.1f}) facing "
            "{:.0f} deg.", p.Entry, map->GetId(), map->GetInstanceId(), p.X, p.Y, p.Z, p.O * 180.0f / float(M_PI));
    }
    LOG_INFO("module", "Atlas: map {} instance {} layout: {} of {} piece(s) placed (seed {}).", map->GetId(),
        map->GetInstanceId(), placed, layout.size(), rng.GetSeed());
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
    PoolMap const* pool = _poolMaps.empty() ? nullptr : &_poolMaps.front();
    KitPiece const* start = pool ? KitFind(_poolKit, pool->Start) : nullptr;
    if (!start)
    {
        error = "No pool map is loaded (atlas_pool.sql not installed?).";
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

    // Arrivals stand at the start piece's entrance, which every layout has at its origin.
    KitPlacement const origin{ pool->Start, pool->Origin.X, pool->Origin.Y, pool->Origin.Z, pool->Origin.O };
    KitPoint const entrance = KitToWorld(origin, start->EntranceX, start->EntranceY, start->EntranceZ);

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
        if (sInstanceSaveMgr->PlayerGetBoundInstance(t->GetGUID(), pool->MapId, DUNGEON_DIFFICULTY_NORMAL))
            sInstanceSaveMgr->PlayerUnbindInstance(t->GetGUID(), pool->MapId, DUNGEON_DIFFICULTY_NORMAL, true, t);
        t->TeleportTo(pool->MapId, entrance.X, entrance.Y, entrance.Z, entrance.O);
    }
    LOG_INFO("module", "Atlas: spike — {} and {} bot(s) to pool map {}.", player->GetName(), travelers.size() - 1,
        pool->MapId);
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
