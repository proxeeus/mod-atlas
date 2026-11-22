// mod-atlas — module entry point
// Function name must match AC_ADD_SCRIPT_LOADER convention

#include "ScriptMgr.h"
#include "Log.h"

void AddSC_npc_atlas_chronicler();
void AddSC_atlas_player_script();
void AddSC_atlas_world_script();
void AddSC_atlas_allmap_script();
void AddSC_atlas_command_script();
void AddSC_atlas_unit_script();
void AddSC_atlas_allcreature_script();

void Addmod_atlasScripts()
{
    LOG_INFO("module", "Atlas: Registering scripts...");

    AddSC_npc_atlas_chronicler();
    AddSC_atlas_player_script();
    AddSC_atlas_world_script();
    AddSC_atlas_allmap_script();
    AddSC_atlas_command_script();
    AddSC_atlas_unit_script();
    AddSC_atlas_allcreature_script();
}
