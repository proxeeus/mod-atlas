/*
 * mod-atlas — atlas_unit_script.cpp
 * Damage, healing and the affix events (engage, damage taken, death) around
 * run creatures, whatever their AI:
 *   - Creatures the run owns (spawned or added mid-run): melee is set at spawn
 *     from the role table, so it passes through; their spells and damage over
 *     time take the creature's spell scale, their heals its heal scale
 *     (AtlasScaling.cpp, DESIGN §9).
 *   - Anything else hitting a run player (hazards, traps, leftover stock
 *     scripts): scaled to the party level and capped at 3% max HP.
 */

#include "ScriptMgr.h"
#include "Player.h"
#include "Creature.h"
#include "Map.h"
#include "SpellInfo.h"
#include "AtlasMgr.h"
#include "AtlasConfig.h"
#include "AtlasAffixes.h"
#include <algorithm>
#include <cstdint>

using namespace Atlas;

static constexpr float ENV_DAMAGE_MAX_PCT = 0.03f;

class atlas_unit_script : public UnitScript
{
public:
    atlas_unit_script() : UnitScript("atlas_unit_script") {}

    void ModifyPeriodicDamageAurasTick(Unit* target, Unit* attacker, uint32& damage,
        SpellInfo const* spellInfo) override
    {
        ScaleDamage(target, attacker, damage, spellInfo, true);
    }

    void ModifySpellDamageTaken(Unit* target, Unit* attacker, int32& damage, SpellInfo const* spellInfo) override
    {
        if (damage <= 0) return;
        uint32 udmg = static_cast<uint32>(damage);
        ScaleDamage(target, attacker, udmg, spellInfo, true);
        damage = static_cast<int32>(std::min<uint32>(udmg, INT32_MAX));
    }

    void ModifyMeleeDamage(Unit* target, Unit* attacker, uint32& damage) override
    {
        ScaleDamage(target, attacker, damage, nullptr, false);
    }

    // A run creature's heals scale with its health, like its spells with melee.
    void ModifyHealReceived(Unit* /*target*/, Unit* healer, uint32& heal, SpellInfo const* /*spellInfo*/) override
    {
        if (!heal || !healer || !healer->IsCreature() || !AtlasMgr::HasOwnedCreatures()
            || !sAtlasConfig->IsEnabled())
            return;

        float const scale = sAtlasMgr->GetOwnedHealScale(healer->GetGUID());
        if (scale >= 0.0f)
            heal = static_cast<uint32>(std::min<double>(double(heal) * scale, UINT32_MAX));
    }

    // Combat-start affixes (Ice-shielded), for every run creature, whatever
    // its AI.
    void OnUnitEnterCombat(Unit* unit, Unit* /*victim*/) override
    {
        if (!unit || !AtlasMgr::HasOwnedCreatures() || !sAtlasConfig->IsEnabled())
            return;
        if (Creature* creature = unit->ToCreature())
            sAtlasMgr->OnRunCreatureEngaged(creature);
    }

    // Life-threshold affixes (Summoner).
    void OnDamage(Unit* /*attacker*/, Unit* victim, uint32& damage) override
    {
        if (!damage || !victim || !victim->IsCreature() || !AtlasMgr::HasOwnedCreatures()
            || !sAtlasConfig->IsEnabled() || !victim->GetMap() || !victim->GetMap()->IsDungeon())
            return;
        sAtlasMgr->OnRunCreatureDamaged(victim->ToCreature(), damage);
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

        // Killed by a player (or their pet), or by nothing a player owns
        // (an affix burst, a trap, a stock script): the instance's run.
        Session* session = nullptr;
        if (player)
            session = sAtlasMgr->GetSessionByPlayer(player->GetGUID());
        if (!session && creature->GetMap() && creature->GetMap()->IsDungeon())
            session = sAtlasMgr->GetSessionByInstance(creature->GetInstanceId());

        if (session && session->IsActive() && creature->GetMapId() == session->MapId)
            sAtlasMgr->HandleCreatureDeath(creature, session);

        // Death affixes (burst, desecrated ground, splitting): after the kill
        // is counted, outside the session lock.
        sAtlasMgr->OnRunCreatureDied(creature);
    }

private:
    static uint32 Scaled(uint32 damage, float factor)
    {
        return static_cast<uint32>(std::min<double>(double(damage) * factor, UINT32_MAX));
    }

    // A player (or a player's pet, totem or guardian) hitting a creature the
    // run owns: the Armoured and Warded affixes.
    static void ScaleDamageToRunCreature(Unit* target, Unit* attacker, uint32& damage, SpellInfo const* spellInfo,
        bool isSpell)
    {
        if (!target->IsCreature() || !attacker || !attacker->GetCharmerOrOwnerPlayerOrPlayerItself())
            return;

        AtlasMgr::CreatureAffixMods mods;
        float spellScale = 1.0f;
        if (!sAtlasMgr->GetOwnedCombat(target->GetGUID(), mods, spellScale))
            return;

        bool const physical = !isSpell || !spellInfo || (spellInfo->GetSchoolMask() & SPELL_SCHOOL_MASK_NORMAL);
        float const factor = physical ? mods.PhysTaken : mods.SpellTaken;
        if (factor != 1.0f)
            damage = std::max(1u, Scaled(damage, factor));
    }

    void ScaleDamage(Unit* target, Unit* attacker, uint32& damage, SpellInfo const* spellInfo, bool isSpell)
    {
        // Runs for every damage event on the server: bail out before taking
        // the session mutex unless a session exists and the target is in a
        // dungeon (sessions only ever run in dungeon instances).
        if (!sAtlasConfig->IsEnabled() || damage == 0 || !AtlasMgr::HasActiveSessions() || !target)
            return;

        if (!target->GetMap() || !target->GetMap()->IsDungeon())
            return;

        // An affix's own spells (extra hits, zones, bursts) carry the amount
        // computed when they were cast.
        if (spellInfo && sAtlasAffixes->IsAffixSpell(spellInfo->Id))
            return;

        Player* player = target->ToPlayer();
        if (!player)
        {
            if (AtlasMgr::HasOwnedCreatures())
                ScaleDamageToRunCreature(target, attacker, damage, spellInfo, isSpell);
            return;
        }

        if (attacker && attacker->ToPlayer())
            return;

        ObjectGuid playerGuid = player->GetGUID();
        if (!sAtlasMgr->GetSessionByPlayer(playerGuid))
            return;

        // A creature the run owns: melee already comes from its role stats
        // (plus Brutal, and the extra elemental hit of Searing and its kin);
        // spells and damage over time take its spell scale.
        if (attacker && attacker->IsCreature())
        {
            AtlasMgr::CreatureAffixMods mods;
            float spellScale = 1.0f;
            if (sAtlasMgr->GetOwnedCombat(attacker->GetGUID(), mods, spellScale))
            {
                if (isSpell)
                    damage = std::max(1u, Scaled(damage, spellScale));
                else
                {
                    if (mods.MeleeDealt != 1.0f)
                        damage = std::max(1u, Scaled(damage, mods.MeleeDealt));
                    if (mods.ExtraHitSpell && mods.ExtraHitPct > 0.0f)
                        AtlasMgr::QueueExtraHit(attacker, player, mods.ExtraHitSpell,
                            std::max(1, static_cast<int32>(damage * mods.ExtraHitPct)));
                }
                return;
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
