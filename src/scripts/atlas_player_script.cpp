/*
 * mod-atlas — atlas_player_script.cpp
 * Player death handling: blocks spirit release, checks for wipe.
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
};

void AddSC_atlas_player_script()
{
    new atlas_player_script();
}
