/*
 * mod-atlas — atlas_unit_script.cpp
 * Scales ALL incoming damage for session players:
 *   - Session boss spells/melee: scaled by level ratio (template level → session level)
 *   - Session trash: already scaled by custom AI melee, passed through
 *   - Environmental (non-session): capped at 3% max HP
 */

#include "ScriptMgr.h"
#include "Player.h"
#include "Creature.h"
#include "Map.h"
#include "SpellInfo.h"
#include "AtlasMgr.h"
#include "AtlasConfig.h"

using namespace Atlas;

static constexpr float ENV_DAMAGE_MAX_PCT = 0.03f;

class atlas_unit_script : public UnitScript
{
public:
    atlas_unit_script() : UnitScript("atlas_unit_script") {}

    void ModifyPeriodicDamageAurasTick(Unit* target, Unit* attacker, uint32& damage, SpellInfo const* /*spellInfo*/) override
    {
        ScaleDamage(target, attacker, damage);
    }

    void ModifySpellDamageTaken(Unit* target, Unit* attacker, int32& damage, SpellInfo const* /*spellInfo*/) override
    {
        if (damage <= 0) return;
        uint32 udmg = static_cast<uint32>(damage);
        ScaleDamage(target, attacker, udmg);
        damage = static_cast<int32>(udmg);
    }

    void ModifyMeleeDamage(Unit* target, Unit* attacker, uint32& damage) override
    {
        ScaleDamage(target, attacker, damage);
    }

    void OnUnitDeath(Unit* unit, Unit* killer) override
    {
        if (!sAtlasConfig->IsEnabled() || !unit || !AtlasMgr::HasActiveSessions())
            return;

        Creature* creature = unit->ToCreature();
        if (!creature)
            return;

        Player* player = nullptr;
        if (killer)
        {
            player = killer->ToPlayer();
            if (!player && killer->GetOwner())
                player = killer->GetOwner()->ToPlayer();
        }

        Session* session = nullptr;
        if (player)
            session = sAtlasMgr->GetSessionByPlayer(player->GetGUID());

        if (!session || !session->IsActive())
            return;

        if (creature->GetMapId() != session->MapId)
            return;

        sAtlasMgr->HandleCreatureDeath(creature, session);
    }

private:
    void ScaleDamage(Unit* target, Unit* attacker, uint32& damage)
    {
        // Runs for every damage event on the server: bail out before taking
        // the session mutex unless a session exists and the target is in a
        // dungeon (sessions only ever run in dungeon instances).
        if (!sAtlasConfig->IsEnabled() || damage == 0 || !AtlasMgr::HasActiveSessions())
            return;

        Player* player = target ? target->ToPlayer() : nullptr;
        if (!player || !player->GetMap() || !player->GetMap()->IsDungeon())
            return;

        if (attacker && attacker->ToPlayer())
            return;

        ObjectGuid playerGuid = player->GetGUID();

        if (!sAtlasMgr->GetSessionByPlayer(playerGuid))
            return;

        if (attacker)
        {
            ObjectGuid attackerGuid = attacker->GetGUID();

            // Session creature damage — scale bosses, pass through trash
            if (sAtlasMgr->IsSessionCreature(playerGuid, attackerGuid))
            {
                float scale = sAtlasMgr->GetSessionCreatureDamageScale(
                    playerGuid, attackerGuid);

                if (scale < 1.0f)
                    damage = std::max(1u, static_cast<uint32>(damage * scale));

                return;
            }

            // Adds summoned during the run: level-scaled like a boss, not
            // capped like a hazard (the cap made them deal at most 3% max HP).
            if (Creature const* creature = attacker->ToCreature())
            {
                float scale = sAtlasMgr->GetOwnedCreatureDamageScale(playerGuid, creature);
                if (scale >= 0.0f)
                {
                    if (scale < 1.0f)
                        damage = std::max(1u, static_cast<uint32>(damage * scale));

                    return;
                }
            }
        }

        // Anything else (no attacker, or a creature the run doesn't own:
        // environmental hazards, traps, leftover dungeon scripts)
        float envScale = sAtlasMgr->GetEnvironmentalDamageScale(playerGuid);
        if (envScale < 1.0f)
            damage = static_cast<uint32>(damage * envScale);

        uint32 maxHp = player->GetMaxHealth();
        uint32 cap   = std::max(1u, static_cast<uint32>(maxHp * ENV_DAMAGE_MAX_PCT));

        if (damage > cap)
            damage = cap;

        if (damage == 0)
            damage = 1;
    }
};

void AddSC_atlas_unit_script()
{
    new atlas_unit_script();
}
