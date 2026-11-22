/*
 * mod-dungeon-master — dm_world_script.cpp
 * Server lifecycle hooks: config load, startup, update tick, shutdown.
 */

#include "ScriptMgr.h"
#include "DungeonMasterMgr.h"
#include "RoguelikeMgr.h"
#include "DMConfig.h"
#include "SpellMgr.h"
#include "SpellInfo.h"
#include "Log.h"

using namespace DungeonMaster;

class dm_world_script : public WorldScript
{
public:
    dm_world_script() : WorldScript("dm_world_script") {}

    void OnAfterConfigLoad(bool reload) override
    {
        sDMConfig->LoadConfig(reload);
    }

    void OnStartup() override
    {
        // Even when disabled: a run interrupted by the last shutdown must not
        // leave anyone saved to an emptied dungeon instance.
        sDungeonMasterMgr->ReleaseInstancesLeftOpen();

        if (!sDMConfig->IsEnabled())
        {
            LOG_INFO("module", "DungeonMaster: Disabled in configuration.");
            return;
        }

        // No global spell changes here. The roguelike buff used to patch
        // Greater Blessing of Kings (25898) to StackAmount 255 for the whole
        // server, which let every paladin (bots included) stack +10% stats per
        // recast (upstream #23). It now uses the module's own spell.
        if (!sSpellMgr->GetSpellInfo(RoguelikeMgr::BUFF_SPELL_ID))
            LOG_ERROR("module", "DungeonMaster: roguelike buff spell {} is missing from spell_dbc "
                "(dm_setup.sql not applied?) - floors will grant no stat buff.",
                RoguelikeMgr::BUFF_SPELL_ID);

        sDungeonMasterMgr->Initialize();
        sRoguelikeMgr->Initialize();

        LOG_INFO("module", "===============================================");
        LOG_INFO("module", " Dungeon Master Module — Ready");
        LOG_INFO("module", " {} difficulties | {} themes | {} dungeons",
            sDMConfig->GetDifficulties().size(),
            sDMConfig->GetThemes().size(),
            sDMConfig->GetDungeons().size());
        LOG_INFO("module", " Level band: +/-{} | Max concurrent: {}",
            sDMConfig->GetLevelBand(), sDMConfig->GetMaxConcurrentRuns());
        LOG_INFO("module", "===============================================");
    }

    void OnShutdown() override
    {
        if (!sDMConfig->IsEnabled()) return;
        LOG_INFO("module", "DungeonMaster: Shutdown — {} sessions active.",
            sDungeonMasterMgr->GetActiveSessionCount());
    }

    void OnUpdate(uint32 diff) override
    {
        if (sDMConfig->IsEnabled())
        {
            sDungeonMasterMgr->Update(diff);
            sRoguelikeMgr->Update(diff);
        }
    }
};

void AddSC_dm_world_script()
{
    new dm_world_script();
}
