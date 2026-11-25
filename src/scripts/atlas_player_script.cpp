/*
 * mod-atlas — atlas_player_script.cpp
 * Player death handling: blocks spirit release, checks for wipe. Reward items
 * still waiting for their player at logout go by mail. A run fog saved by a
 * crash (a periodic save while it was on) is taken off at login outside the
 * run's map.
 */

#include "ScriptMgr.h"
#include "Player.h"
#include "Creature.h"
#include "AtlasMgr.h"
#include "AtlasConfig.h"

using namespace Atlas;

class atlas_player_script : public PlayerScript
{
public:
    atlas_player_script() : PlayerScript("atlas_player_script") {}

    void OnPlayerKilledByCreature(Creature* /*killer*/, Player* player) override
    {
        if (!sAtlasConfig->IsEnabled() || !player)
            return;

        Session* session = sAtlasMgr->GetSessionByPlayer(player->GetGUID());
        if (!session || !session->IsActive())
            return;

        if (player->GetMapId() != session->MapId)
            return;

        sAtlasMgr->HandlePlayerDeath(player, session);
    }

    // Generated maps have no graveyard of their own (the copied map row points to Westfall's).
    void OnPlayerJustDied(Player* player) override
    {
        if (player && sAtlasMgr->IsPoolMap(player->GetMapId()))
            sAtlasMgr->OnPoolDeath(player);
    }

    void OnPlayerLogin(Player* player) override
    {
        if (!player)
            return;
        Session* session = sAtlasMgr->GetSessionByPlayer(player->GetGUID());
        if (!session || player->GetMapId() != session->MapId)
            sAtlasMgr->ClearFogs(player);
    }

    void OnPlayerLogout(Player* player) override
    {
        sAtlasMgr->MailPendingRewards(player);
    }
};

void AddSC_atlas_player_script()
{
    new atlas_player_script();
}
