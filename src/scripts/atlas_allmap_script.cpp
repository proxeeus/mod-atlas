/*
 * mod-atlas — atlas_allmap_script.cpp
 * Triggers dungeon population when a player enters the instance map, and
 * summons a map run's rifts and cache from that map's own update.
 */

#include "ScriptMgr.h"
#include "Map.h"
#include "Player.h"
#include "AtlasMgr.h"
#include "AtlasConfig.h"
#include "Chat.h"
#include "Log.h"

using namespace Atlas;

class atlas_allmap_script : public AllMapScript
{
public:
    atlas_allmap_script() : AllMapScript("atlas_allmap_script") {}

    // Cheap for every other map: an atomic count says whether anything waits.
    void OnMapUpdate(Map* map, uint32 /*diff*/) override
    {
        sAtlasMgr->ProcessMapSpawns(map);
        sAtlasMgr->ProcessPoolMap(map);
    }

    void OnPlayerEnterAll(Map* map, Player* player) override
    {
        if (!sAtlasConfig->IsEnabled() || !map || !player)
            return;

        // Only care about dungeon maps
        if (!map->IsDungeon())
            return;

        // The generated-map spike: no session, a kit piece to wait for.
        if (sAtlasMgr->IsPoolMap(map->GetId()))
        {
            sAtlasMgr->OnPlayerEnteredPool(player);
            return;
        }

        Session* session = sAtlasMgr->GetSessionByPlayer(player->GetGUID());
        if (!session)
        {
            LOG_DEBUG("module", "Atlas: OnPlayerEnterAll — {} entered map {} but has no session",
                player->GetName(), map->GetId());
            return;
        }

        LOG_INFO("module", "Atlas: OnPlayerEnterAll — {} entered map {} (session {} state {} mapId {} mobs {} bosses {})",
            player->GetName(), map->GetId(), session->SessionId,
            static_cast<int>(session->State), session->MapId,
            session->TotalMobs, session->TotalBosses);

        // The run's sky and fog, on every arrival (PLAN 1.6).
        if (map->GetId() == session->MapId)
            sAtlasMgr->OnPlayerEnteredRunMap(player);

        if (session->State != SessionState::InProgress)
            return;

        if (map->GetId() != session->MapId)
            return;

        // Only populate once — guard against duplicate triggers.
        // The Update tick also triggers populate as a reliable fallback.
        if (session->TotalMobs > 0 || session->TotalBosses > 0)
            return;

        InstanceMap* instance = map->ToInstanceMap();
        if (!instance)
            return;

        session->InstanceId = instance->GetInstanceId();

        ChatHandler(player->GetSession()).PSendSysMessage(
            "{} The echo takes shape around you...", CHAT_PREFIX);

        sAtlasMgr->PopulateDungeon(session, instance);

        LOG_INFO("module", "Atlas: Session {} — populated via OnPlayerEnterAll (player {}, map {}, mobs {}, bosses {})",
            session->SessionId, player->GetName(), map->GetId(),
            session->TotalMobs, session->TotalBosses);

        ChatHandler(player->GetSession()).SendSysMessage(AtlasMgr::PopulationNotice(*session));
    }
};

void AddSC_atlas_allmap_script()
{
    new atlas_allmap_script();
}
