/*
 * mod-atlas — AtlasScaling.cpp
 * Scaling: class/level base stats, creature level ownership, health and damage
 * multipliers, damage scales for session creatures and the environment.
 *
 * AtlasMgr is split across AtlasMgr.cpp, AtlasPopulation.cpp, AtlasScaling.cpp,
 * AtlasRewards.cpp and AtlasRecords.cpp; the class itself is in AtlasMgr.h.
 */

#include "AtlasMgr.h"
#include "AtlasBossSpawnQuery.h"
#include "RoguelikeMgr.h"
#include "AtlasConfig.h"
#include "Player.h"
#include "Group.h"
#include "Creature.h"
#include "CreatureAI.h"
#include "Map.h"
#include "MapMgr.h"
#include "GameObject.h"
#include "ObjectMgr.h"
#include "WorldSession.h"
#include "World.h"
#include "Chat.h"
#include "GameTime.h"
#include "DatabaseEnv.h"
#include "Log.h"
#include "MotionMaster.h"
#include "Mail.h"
#include "Item.h"
#include "LootMgr.h"
#include "SpellAuras.h"
#include "SpellAuraEffects.h"
#include "InstanceScript.h"
#include "InstanceSaveMgr.h"
#include "DisableMgr.h"
#include "CellImpl.h"
#include "GridNotifiers.h"
#include "GridNotifiersImpl.h"
#include "Random.h"
#include "StringFormat.h"
#include <algorithm>
#include <set>
#include <cmath>
#include <atomic>
#include "AtlasInternal.h"

namespace Atlas
{

// Cache creature_classlevelstats for force-scaling
void AtlasMgr::LoadClassLevelStats()
{
    _classLevelStats.clear();

    QueryResult result = WorldDatabase.Query(
        "SELECT level, class, basehp0, damage_base, basearmor, attackpower "
        "FROM creature_classlevelstats "
        "WHERE level > 0 AND level <= 83 "
        "ORDER BY class, level");

    if (!result)
    {
        LOG_WARN("module", "Atlas: creature_classlevelstats not found — "
                 "creature scaling will use template defaults.");
        return;
    }

    uint32 count = 0;
    do
    {
        Field* f = result->Fetch();
        uint8  level     = f[0].Get<uint8>();
        uint8  unitClass = f[1].Get<uint8>();
        ClassLevelStatEntry e;
        e.BaseHP       = std::max(1u, f[2].Get<uint32>());
        e.BaseDamage   = std::max(1.0f, f[3].Get<float>());
        e.BaseArmor    = f[4].Get<uint32>();
        e.AttackPower  = f[5].Get<uint32>();
        _classLevelStats[{unitClass, level}] = e;
        ++count;
    } while (result->NextRow());

    LOG_INFO("module", "Atlas: {} class-level stat entries cached.", count);
}

// Look up cached base stats
ClassLevelStatEntry const* AtlasMgr::GetBaseStatsForLevel(
    uint8 unitClass, uint8 level) const
{
    auto it = _classLevelStats.find({unitClass, level});
    if (it != _classLevelStats.end())
        return &it->second;

    // Fallback: try Warrior (class 1) at this level
    it = _classLevelStats.find({1, level});
    if (it != _classLevelStats.end())
        return &it->second;

    return nullptr;
}

// ---------------------------------------------------------------------------
// Creature level ownership
//
// Cheap global gate so the AllCreatureScript hooks cost nothing on a server with
// no active Atlas session.
// ---------------------------------------------------------------------------
static std::atomic<uint32> g_dmOwnedCount{0};

bool AtlasMgr::HasOwnedCreatures()
{
    return g_dmOwnedCount.load(std::memory_order_relaxed) != 0;
}

void AtlasMgr::RegisterOwnedCreature(ObjectGuid guid, uint8 level, uint32 maxHealth, uint32 instanceId)
{
    if (!guid || !level)
        return;

    std::lock_guard<std::mutex> lock(_ownedMutex);

    OwnedCreature& oc = _ownedCreatures[guid];
    if (oc.Level == 0)
        g_dmOwnedCount.fetch_add(1, std::memory_order_relaxed);

    oc.Level      = level;
    oc.MaxHealth  = maxHealth;
    oc.InstanceId = instanceId;
}

bool AtlasMgr::GetOwnedCreature(ObjectGuid guid, uint8& level, uint32& maxHealth) const
{
    std::lock_guard<std::mutex> lock(_ownedMutex);

    auto it = _ownedCreatures.find(guid);
    if (it == _ownedCreatures.end())
        return false;

    level     = it->second.Level;
    maxHealth = it->second.MaxHealth;
    return true;
}

void AtlasMgr::ForgetOwnedCreature(ObjectGuid guid)
{
    std::lock_guard<std::mutex> lock(_ownedMutex);

    if (_ownedCreatures.erase(guid))
        g_dmOwnedCount.fetch_sub(1, std::memory_order_relaxed);
}

void AtlasMgr::ForgetOwnedCreaturesForInstance(uint32 instanceId)
{
    if (!instanceId)
        return;

    std::lock_guard<std::mutex> lock(_ownedMutex);

    for (auto it = _ownedCreatures.begin(); it != _ownedCreatures.end(); )
    {
        if (it->second.InstanceId == instanceId)
        {
            it = _ownedCreatures.erase(it);
            g_dmOwnedCount.fetch_sub(1, std::memory_order_relaxed);
        }
        else
            ++it;
    }
}

// Scale an add summoned by an Atlas creature. SelectCreatureForTheme()
// matches on creature type only and no summon hook was installed, so an add
// would otherwise enter the instance at its own template level — e.g. a level-69
// Fel Imp inside a level-25 run, which the client marks with a skull.
void AtlasMgr::ScaleSummonedCreature(Creature* creature, Session* session)
{
    if (!creature || !session)
        return;

    uint8 targetLevel = session->EffectiveLevel;
    if (!targetLevel)
        return;

    creature->SetLevel(targetLevel);

    uint8 unitClass = creature->GetCreatureTemplate()->unit_class;
    ClassLevelStatEntry const* baseStats = GetBaseStatsForLevel(unitClass, targetLevel);

    float hpMult  = CalculateHealthMultiplier(session);
    float dmgMult = CalculateDamageMultiplier(session);

    float finalHP = baseStats
        ? static_cast<float>(baseStats->BaseHP) * hpMult
        : creature->GetMaxHealth() * hpMult;

    uint32 hp = std::max(1u, static_cast<uint32>(finalHP));
    creature->SetMaxHealth(hp);
    creature->SetHealth(hp);

    if (baseStats)
    {
        float dmgBase = baseStats->BaseDamage;
        float apBonus = static_cast<float>(baseStats->AttackPower) / 14.0f;
        float atkTime = static_cast<float>(creature->GetCreatureTemplate()->BaseAttackTime) / 1000.0f;
        if (atkTime <= 0.0f)
            atkTime = 2.0f;

        float minDmg = (dmgBase + apBonus) * atkTime * dmgMult;
        float maxDmg = ((dmgBase * 1.15f) + apBonus) * atkTime * dmgMult;

        minDmg = std::max(1.0f, minDmg);
        maxDmg = std::max(minDmg, maxDmg);

        creature->SetBaseWeaponDamage(BASE_ATTACK, MINDAMAGE, minDmg);
        creature->SetBaseWeaponDamage(BASE_ATTACK, MAXDAMAGE, maxDmg);
        creature->UpdateDamagePhysical(BASE_ATTACK);
    }

    if (baseStats && baseStats->BaseArmor > 0)
        creature->SetArmor(baseStats->BaseArmor);

    for (uint8 school = SPELL_SCHOOL_HOLY; school < MAX_SPELL_SCHOOL; ++school)
        creature->SetResistance(SpellSchools(school), 0);

    RegisterOwnedCreature(creature->GetGUID(), targetLevel, hp, session->InstanceId);
}

// Scaling multipliers
float AtlasMgr::CalculateHealthMultiplier(Session const* s) const
{
    if (!s) return 1.0f;
    DifficultyTier const* d = sAtlasConfig->GetDifficulty(s->DifficultyId);
    if (!d) return 1.0f;

    float base = d->HealthMultiplier;
    uint32 n   = s->Players.size();
    float mult;
    if (n <= 1) mult = base * sAtlasConfig->GetSoloMultiplier();
    else        mult = base * (1.0f + (n - 1) * sAtlasConfig->GetPerPlayerHealthMult());

    // Roguelike tier scaling
    if (s->RoguelikeRunId != 0)
        mult *= sRoguelikeMgr->GetTierHealthMultiplier(s->RoguelikeRunId);

    return mult;
}

float AtlasMgr::CalculateDamageMultiplier(Session const* s) const
{
    if (!s) return 1.0f;
    DifficultyTier const* d = sAtlasConfig->GetDifficulty(s->DifficultyId);
    if (!d) return 1.0f;

    float base = d->DamageMultiplier;
    uint32 n   = s->Players.size();
    float mult;
    if (n <= 1) mult = base * sAtlasConfig->GetSoloMultiplier();
    else        mult = base * (1.0f + (n - 1) * sAtlasConfig->GetPerPlayerDamageMult());

    // Roguelike tier scaling
    if (s->RoguelikeRunId != 0)
        mult *= sRoguelikeMgr->GetTierDamageMultiplier(s->RoguelikeRunId);

    return mult;
}

// Check if creature belongs to an active session
bool AtlasMgr::IsSessionCreature(ObjectGuid playerGuid, ObjectGuid creatureGuid)
{
    std::lock_guard<std::mutex> lock(_sessionMutex);
    auto pit = _playerToSession.find(playerGuid);
    if (pit == _playerToSession.end())
        return false;

    auto sit = _activeSessions.find(pit->second);
    if (sit == _activeSessions.end())
        return false;

    return sit->second.IsSessionCreature(creatureGuid);
}

// Check if creature is a session boss
bool AtlasMgr::IsSessionBoss(ObjectGuid playerGuid, ObjectGuid creatureGuid)
{
    std::lock_guard<std::mutex> lock(_sessionMutex);
    auto pit = _playerToSession.find(playerGuid);
    if (pit == _playerToSession.end())
        return false;

    auto sit = _activeSessions.find(pit->second);
    if (sit == _activeSessions.end())
        return false;

    for (auto const& sc : sit->second.SpawnedCreatures)
        if (sc.Guid == creatureGuid)
            return sc.IsBoss;

    return false;
}

// Compute damage scale for a session creature attacking a session player.
// Spell/ability damage is hard-coded in DBC at the boss's original design level.
// This returns a multiplier to bring that damage in line with the session's level.
float AtlasMgr::GetSessionCreatureDamageScale(
    ObjectGuid playerGuid, ObjectGuid creatureGuid)
{
    std::lock_guard<std::mutex> lock(_sessionMutex);
    auto pit = _playerToSession.find(playerGuid);
    if (pit == _playerToSession.end())
        return 1.0f;

    auto sit = _activeSessions.find(pit->second);
    if (sit == _activeSessions.end())
        return 1.0f;

    Session const& session = sit->second;

    // Verify this creature belongs to the session
    bool isBoss = false;
    bool found  = false;
    for (auto const& sc : session.SpawnedCreatures)
    {
        if (sc.Guid == creatureGuid)
        {
            found  = true;
            isBoss = sc.IsBoss;
            break;
        }
    }
    if (!found)
        return 1.0f;

    // Trash mobs use our custom AI — melee is already scaled, no spells.
    if (!isBoss)
        return 1.0f;

    // For bosses: compare session target level to the boss's original template
    // level (ComputeLevelDamageScale). Find a player reference to resolve the GUID.
    Creature* creature = nullptr;
    for (auto const& pd : session.Players)
    {
        Player* p = ObjectAccessor::FindPlayer(pd.PlayerGuid);
        if (p && p->IsInWorld())
        {
            creature = ObjectAccessor::GetCreature(*p, creatureGuid);
            if (creature) break;
        }
    }

    if (!creature)
        return 1.0f;

    return ComputeLevelDamageScale(session, creature);
}

// Adds that appear during a run (summons) are registered as owned and get
// level-scaled melee on creation, but their spells keep template damage. Scale
// those like a boss's. Returns a negative value if the creature is not owned,
// so the caller falls back to the environmental cap.
float AtlasMgr::GetOwnedCreatureDamageScale(ObjectGuid playerGuid, Creature const* creature)
{
    uint8  ownedLevel = 0;
    uint32 ownedHp    = 0;
    if (!creature || !GetOwnedCreature(creature->GetGUID(), ownedLevel, ownedHp))
        return -1.0f;

    std::lock_guard<std::mutex> lock(_sessionMutex);
    auto pit = _playerToSession.find(playerGuid);
    if (pit == _playerToSession.end())
        return -1.0f;

    auto sit = _activeSessions.find(pit->second);
    if (sit == _activeSessions.end())
        return -1.0f;

    return ComputeLevelDamageScale(sit->second, creature);
}

// Template-level spell damage → session-level damage, from the
// creature_classlevelstats base damage ratio. Clamped to [0.03, 1].
float AtlasMgr::ComputeLevelDamageScale(Session const& session, Creature const* creature) const
{
    uint8 targetLevel   = session.EffectiveLevel;
    uint8 templateLevel = creature->GetCreatureTemplate()->maxlevel;

    // If session level >= template level, boss is upscaled — no reduction needed.
    if (targetLevel >= templateLevel)
        return 1.0f;

    // Use classlevelstats to get the proper damage ratio between levels.
    uint8 unitClass = creature->GetCreatureTemplate()->unit_class;
    ClassLevelStatEntry const* targetStats   = GetBaseStatsForLevel(unitClass, targetLevel);
    ClassLevelStatEntry const* templateStats = GetBaseStatsForLevel(unitClass, templateLevel);

    float scale;
    if (targetStats && templateStats && templateStats->BaseDamage > 1.0f)
    {
        scale = targetStats->BaseDamage / templateStats->BaseDamage;
    }
    else
    {
        // Fallback: level ratio squared (spell damage scales ~quadratically)
        float lvlRatio = static_cast<float>(targetLevel) / static_cast<float>(templateLevel);
        scale = lvlRatio * lvlRatio;
    }

    // Apply solo/party multiplier so boss spells feel consistent with melee
    uint32 n = session.Players.size();
    if (n <= 1)
        scale *= sAtlasConfig->GetSoloMultiplier();

    // Clamp: never fully negate, never amplify
    scale = std::max(0.03f, std::min(1.0f, scale));

    LOG_DEBUG("module", "Atlas: Boss spell damage scale for session {} — "
        "targetLvl={}, templateLvl={}, scale={:.3f}",
        session.SessionId, targetLevel, templateLevel, scale);

    return scale;
}

// Scale environmental damage to party level
float AtlasMgr::GetEnvironmentalDamageScale(ObjectGuid playerGuid)
{
    std::lock_guard<std::mutex> lock(_sessionMutex);
    auto pit = _playerToSession.find(playerGuid);
    if (pit == _playerToSession.end())
        return 1.0f;

    auto sit = _activeSessions.find(pit->second);
    if (sit == _activeSessions.end())
        return 1.0f;

    Session const& session = sit->second;

    if (!session.ScaleToParty)
        return 1.0f;

    DungeonInfo const* dg = sAtlasConfig->GetDungeon(session.MapId);
    if (!dg)
        return 1.0f;

    uint8 dungeonLevel = dg->MaxLevel;
    uint8 partyLevel   = session.EffectiveLevel;

    if (partyLevel >= dungeonLevel)
        return 1.0f;

    float ratio = static_cast<float>(partyLevel) / static_cast<float>(dungeonLevel);
    float scale = std::pow(ratio, 1.5f);
    if (scale < 0.05f)
        scale = 0.05f;

    return scale;
}

} // namespace Atlas
