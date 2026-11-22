/*
 * mod-atlas — atlas_world_script.cpp
 * Server lifecycle hooks: config load, startup, update tick, shutdown.
 */

#include "ScriptMgr.h"
#include "AtlasMgr.h"
#include "RoguelikeMgr.h"
#include "AtlasConfig.h"
#include "SpellMgr.h"
#include "SpellInfo.h"
#include "Log.h"

using namespace Atlas;

class atlas_world_script : public WorldScript
{
public:
    atlas_world_script() : WorldScript("atlas_world_script") {}

    void OnAfterConfigLoad(bool reload) override
    {
        sAtlasConfig->LoadConfig(reload);
    }

    void OnStartup() override
    {
        // Even when disabled: a run interrupted by the last shutdown must not
        // leave anyone saved to an emptied dungeon instance.
        sAtlasMgr->ReleaseInstancesLeftOpen();

        if (!sAtlasConfig->IsEnabled())
        {
            LOG_INFO("module", "Atlas: Disabled in configuration.");
            return;
        }

        // No global spell changes here. The roguelike buff used to patch
        // Greater Blessing of Kings (25898) to StackAmount 255 for the whole
        // server, which let every paladin (bots included) stack +10% stats per
        // recast (upstream #23). It now uses the module's own spell.
        if (!sSpellMgr->GetSpellInfo(RoguelikeMgr::BUFF_SPELL_ID))
            LOG_ERROR("module", "Atlas: roguelike buff spell {} is missing from spell_dbc "
                "(atlas_world_setup.sql not applied?) - floors will grant no stat buff.",
                RoguelikeMgr::BUFF_SPELL_ID);

        sAtlasMgr->Initialize();
        sRoguelikeMgr->Initialize();

        LOG_INFO("module", "===============================================");
        LOG_INFO("module", " mod-atlas (V0) — Ready");
        LOG_INFO("module", " {} difficulties | {} themes | {} dungeons",
            sAtlasConfig->GetDifficulties().size(),
            sAtlasConfig->GetThemes().size(),
            sAtlasConfig->GetDungeons().size());
        LOG_INFO("module", " Level band: +/-{} | Max concurrent: {}",
            sAtlasConfig->GetLevelBand(), sAtlasConfig->GetMaxConcurrentRuns());
        LOG_INFO("module", "===============================================");
    }

    void OnShutdown() override
    {
        if (!sAtlasConfig->IsEnabled()) return;
        LOG_INFO("module", "Atlas: Shutdown — {} sessions active.",
            sAtlasMgr->GetActiveSessionCount());
    }

    void OnUpdate(uint32 diff) override
    {
        if (sAtlasConfig->IsEnabled())
        {
            sAtlasMgr->Update(diff);
            sRoguelikeMgr->Update(diff);
        }
    }
};

void AddSC_atlas_world_script()
{
    new atlas_world_script();
}
