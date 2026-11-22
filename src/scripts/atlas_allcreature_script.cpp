/*
 * mod-atlas - atlas_allcreature_script.cpp
 *
 * Keeps Atlas creature levels from being reverted, and clamps adds
 * summoned by Atlas creatures.
 *
 * Two independent mechanisms revert an assigned level:
 *
 *   1. Creature::SelectLevel() re-rolls urand(minlevel, maxlevel) from
 *      creature_template. It runs on the respawn path and from
 *      UpdateEntry(updateAI = true). OnBeforeCreatureSelectLevel lets the module
 *      supply the owned level instead.
 *
 *   2. Another module calling SetLevel() from its own update hook. mod-autobalance
 *      is the common case: it hooks OnPlayerLevelChanged, marks every creature on
 *      the map stale, and ResetCreatureIfNeeded() then calls
 *      SetLevel(UnmodifiedLevel) — the template value. This is why Atlas
 *      mobs sprout a skull the moment a party member levels up.
 *
 * Case 2 is a plain SetLevel() from outside this module, so no core hook can
 * intercept it. The level is therefore re-asserted on this module's own update
 * tick, which makes the fix self-healing and independent of script registration
 * order — it behaves the same whether or not AutoBalance is present. Correcting
 * on the following tick is imperceptible in play.
 */

#include "ScriptMgr.h"
#include "Creature.h"
#include "Map.h"
#include "Player.h"
#include "TemporarySummon.h"
#include "AtlasMgr.h"
#include "AtlasConfig.h"

using namespace Atlas;

class atlas_allcreature_script : public AllCreatureScript
{
public:
    atlas_allcreature_script() : AllCreatureScript("atlas_allcreature_script") {}

    // Runs inside Creature::SelectLevel(), before the rolled level is applied.
    void OnBeforeCreatureSelectLevel(CreatureTemplate const* /*cinfo*/, Creature* creature, uint8& level) override
    {
        if (!creature || !sAtlasConfig->IsEnabled() || !AtlasMgr::HasOwnedCreatures())
            return;

        uint8  owned   = 0;
        uint32 ownedHp = 0;
        if (sAtlasMgr->GetOwnedCreature(creature->GetGUID(), owned, ownedHp) && owned)
            level = owned;
    }

    // Runs at the end of Creature::SelectLevel(), which has just recomputed the
    // stats from the template: put the role stats back (DESIGN §9).
    void OnCreatureSelectLevel(CreatureTemplate const* /*cinfo*/, Creature* creature) override
    {
        if (!creature || !sAtlasConfig->IsEnabled() || !AtlasMgr::HasOwnedCreatures())
            return;

        sAtlasMgr->ReapplyOwnedStats(creature, false);
    }

    // Clamp adds summoned by Atlas creatures. Player pets, totems and
    // guardians are left alone — they belong to the player, not the dungeon.
    void OnCreatureAddWorld(Creature* creature) override
    {
        if (!creature)
            return;

        // The city NPC spawns come from world DB rows, so they exist even while
        // the module is disabled. Hide them and drop the gossip flag so a
        // disabled module leaves nothing behind in the cities (upstream #4).
        if (!sAtlasConfig->IsEnabled())
        {
            if (creature->GetEntry() == sAtlasConfig->GetNpcEntry())
            {
                creature->SetVisible(false);
                creature->RemoveNpcFlag(UNIT_NPC_FLAG_GOSSIP);
            }
            return;
        }

        if (!AtlasMgr::HasActiveSessions())
            return;

        // The module's own spawns are scaled by the code summoning them, which
        // may hold the session lock this hook would take next.
        if (AtlasMgr::IsSpawningOwn())
            return;

        Map* map = creature->GetMap();
        if (!map || !map->IsDungeon())
            return;

        uint32 instanceId = map->GetInstanceId();
        if (!instanceId)
            return;

        if (creature->IsPet() || creature->IsTotem() || creature->IsGuardian())
            return;

        if (creature->GetCharmerOrOwnerPlayerOrPlayerItself())
            return;

        // Already owned (the module's own spawn path registers during scaling).
        uint8  owned   = 0;
        uint32 ownedHp = 0;
        if (sAtlasMgr->GetOwnedCreature(creature->GetGUID(), owned, ownedHp))
            return;

        Session* session = sAtlasMgr->GetSessionByInstance(instanceId);
        if (!session || !session->IsActive())
            return;

        if (creature->GetLevel() == session->EffectiveLevel)
            return;

        sAtlasMgr->ScaleSummonedCreature(creature, session);
    }

    // Re-assert against silent external SetLevel() calls.
    void OnAllCreatureUpdate(Creature* creature, uint32 /*diff*/) override
    {
        if (!creature || !sAtlasConfig->IsEnabled() || !AtlasMgr::HasOwnedCreatures())
            return;

        // Cheap gate: creatures are only ever owned inside instanced dungeons.
        Map* map = creature->GetMap();
        if (!map || !map->IsDungeon() || !map->GetInstanceId())
            return;

        uint8  owned   = 0;
        uint32 ownedHp = 0;
        if (!sAtlasMgr->GetOwnedCreature(creature->GetGUID(), owned, ownedHp) || !owned)
            return;

        if (creature->GetLevel() == owned)
            return;

        // The reverting module recomputed the stats for the template level too:
        // put the level and role stats back, keeping the health share.
        sAtlasMgr->ReapplyOwnedStats(creature, true);
    }

    void OnCreatureRemoveWorld(Creature* creature) override
    {
        if (!creature || !AtlasMgr::HasOwnedCreatures())
            return;

        sAtlasMgr->ForgetOwnedCreature(creature->GetGUID());
    }
};

void AddSC_atlas_allcreature_script()
{
    new atlas_allcreature_script();
}
