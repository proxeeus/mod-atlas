/*
 * mod-atlas — AtlasPopulation.cpp
 * Population: creature and boss pools, dungeon geometry (entrances, spawn
 * points), clearing the stock instance and spawning a run.
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

// Creature-pool filters shared by the trash and boss queries (alias `ct` =
// creature_template). String literals so they concatenate into the queries.
//
// Display IDs whose CreatureDisplayInfoExtra record has an empty BakeName: the
// 3.3.5 client crashes (Error #132) when one of them is spawned in view. List
// taken from upstream issue #17, not re-verified against client data here.
#define ATLAS_CLIENT_CRASH_DISPLAY_FILTER \
    "AND ct.entry NOT IN (SELECT ctmx.CreatureID FROM creature_template_model ctmx " \
    "WHERE ctmx.CreatureDisplayID IN (15331,15332,15444,17116,20392,21951,23290,23296,23836,23886,29323)) "

// Models with no geometry render invisible but still fight (same rule the
// trash query already applied inline; now also used for the boss pool).
#define ATLAS_DEGENERATE_MODEL_FILTER \
    "AND ct.entry NOT IN (SELECT ctmd.CreatureID FROM creature_template_model ctmd " \
    "LEFT JOIN creature_model_info cmid ON cmid.DisplayID = ctmd.CreatureDisplayID " \
    "WHERE ctmd.CreatureDisplayID = 0 OR cmid.DisplayID IS NULL " \
    "OR cmid.BoundingRadius <= 0 OR cmid.CombatReach <= 0) "

namespace Atlas
{

// Aggressive AI for DM-spawned creatures; patrols 5 yd radius, active aggro, hooks JustDied for loot
class AtlasCreatureAI : public CreatureAI
{
public:
    explicit AtlasCreatureAI(Creature* creature)
        : CreatureAI(creature), _patrolStarted(false), _aggroScanTimer(0) {}

    // Active aggro detection — overrides the default which has many silent skips
    void MoveInLineOfSight(Unit* who) override
    {
        if (!who || !me->IsAlive() || me->IsInCombat() || me->HasReactState(REACT_PASSIVE))
            return;

        if (!who->IsPlayer())
            return;

        Player* player = who->ToPlayer();
        if (!player || !player->IsAlive() || player->IsGameMaster())
            return;

        float aggroRange = sAtlasConfig->GetAggroRadius();
        if (me->IsWithinDistInMap(player, aggroRange)
            && me->IsHostileTo(player)
            && me->IsWithinLOSInMap(player))
        {
            me->SetInCombatWith(player);
            player->SetInCombatWith(me);
            me->AddThreat(player, 1.0f);
            AttackStart(player);
        }
    }

    void UpdateAI(uint32 diff) override
    {
        if (!UpdateVictim())
        {
            // Start random patrol movement when idle
            if (!_patrolStarted && me->IsAlive())
            {
                me->GetMotionMaster()->MoveRandom(5.0f);
                _patrolStarted = true;
            }

            // Fallback aggro scan every 1 second for cases where MoveInLineOfSight
            // doesn't fire (inactive grids, summoned creature edge cases)
            _aggroScanTimer += diff;
            if (_aggroScanTimer >= 1000 && me->IsAlive())
            {
                _aggroScanTimer = 0;
                float aggroRange = sAtlasConfig->GetAggroRadius();

                Map::PlayerList const& players = me->GetMap()->GetPlayers();
                float closest = aggroRange;
                Player* target = nullptr;

                for (auto const& itr : players)
                {
                    Player* p = itr.GetSource();
                    if (!p || !p->IsAlive() || p->IsGameMaster())
                        continue;

                    float dist = me->GetDistance(p);
                    if (dist < closest
                        && me->IsHostileTo(p)
                        && me->IsWithinLOSInMap(p))
                    {
                        closest = dist;
                        target = p;
                    }
                }

                if (target)
                {
                    me->SetInCombatWith(target);
                    target->SetInCombatWith(me);
                    me->AddThreat(target, 1.0f);
                    AttackStart(target);
                }
            }
            return;
        }
        DoMeleeAttackIfReady();
    }

    // Atlas spawns creatures at generated points, frequently off the
    // navmesh or far from a home position the core considers reachable. The core
    // then fires EVADE_REASON_BOUNDARY / EVADE_REASON_NO_PATH mid-fight;
    // _EnterEvadeMode() drops combat and LoadCreaturesAddon(true) restores the
    // creature, so it walks home and returns at full health. In play this reads
    // as a mob that heals faster than the group can damage it.
    //
    // Honour genuine resets (empty threat list, sequence break) and refuse the
    // positional ones while the creature is alive and still engaged. Also
    // forwards `why`, which the previous override dropped.
    void EnterEvadeMode(EvadeReason why) override
    {
        if ((why == EVADE_REASON_BOUNDARY || why == EVADE_REASON_NO_PATH)
            && me && me->IsAlive() && me->IsInCombat())
        {
            return;
        }

        _patrolStarted = false;
        CreatureAI::EnterEvadeMode(why);
    }

    void JustDied(Unit* killer) override
    {
        CreatureAI::JustDied(killer);
        // NOTE: Do NOT call FillCreatureLoot here.  JustDied fires inside
        // setDeathState / Unit::Kill, and the core will clear creature->loot
        // and remove UNIT_DYNFLAG_LOOTABLE after we return.  Loot is filled
        // later from HandleCreatureDeath (OnUnitDeath hook) which fires
        // AFTER the core's death processing is finished.
        sAtlasMgr->OnCreatureDeathHook(me);
    }

private:
    bool   _patrolStarted;
    uint32 _aggroScanTimer;
};

// ---------------------------------------------------------------------------

// Load creature pools from world DB, split into trash (rank 0) and boss (rank 1/2/4)
void AtlasMgr::LoadCreaturePools()
{
    _creaturesByType.clear();
    _bossCreatures.clear();

    // Type for theming, rank for boss/trash split, ScriptName='' to avoid scripted mobs
    QueryResult result = WorldDatabase.Query(
        "SELECT ct.entry, ct.type, ct.minlevel, ct.maxlevel, ct.`rank` "
        "FROM creature_template ct "
        "LEFT JOIN creature_template_movement ctm ON ct.entry = ctm.CreatureId "
        "WHERE ct.type > 0 AND ct.type <= 10 AND ct.type != 8 "       // combat types, skip Critter
        "AND ct.minlevel > 0 AND ct.maxlevel <= 83 "
        "AND ct.`rank` != 3 "                                          // not World Boss
        "AND (ctm.Ground IS NULL OR ctm.Ground != 0) "                // no water-only creatures
        "AND ct.VehicleId = 0 "                                        // not a vehicle/chair/cannon
        "AND ct.ScriptName = '' "                                      // no C++ scripts (they override our scaling)
        "AND ct.npcflag = 0 "                                          // no vendors/quest givers/gossip NPCs
        "AND (ct.unit_flags & 2) = 0 "                                 // no NON_ATTACKABLE
        "AND (ct.unit_flags & 33554432) = 0 "                          // no NOT_SELECTABLE (invisible attackers, upstream #17/#25)
        "AND (ct.flags_extra & 128) = 0 "                              // no TRIGGER creatures (upstream #17)
        ATLAS_CLIENT_CRASH_DISPLAY_FILTER
        "AND (ct.subname = '' OR ct.subname IS NULL) "                 // no guild/title text under name
        "AND ct.name NOT LIKE '%[UNUSED]%' "
        "AND ct.name NOT LIKE '%[PH]%' "
        "AND ct.name NOT LIKE '%Test %' "
        "AND ct.name NOT LIKE '%Test_%' "
        "AND ct.name NOT LIKE '%DVREF%' "
        "AND ct.name NOT LIKE '%[DNT]%' "
        "AND ct.name NOT LIKE '%Trigger%' "
        "AND ct.name NOT LIKE '%Invisible%' "
        "AND ct.name NOT LIKE '%Dummy%' "
        "AND ct.name NOT LIKE '%(%' "                                  // skip (1), (2) variant entries
        "AND ct.name NOT LIKE '%Debug%' "
        "AND ct.name NOT LIKE '%Template%' "
        "AND ct.name NOT LIKE '%Copy of%' "
        "AND ct.name NOT LIKE '% - DNT' "
        "AND ct.name NOT LIKE '%Placeholder%' "
        "AND ct.name NOT LIKE '%Visual%' "
        "AND ct.name NOT LIKE '%Server%' "
        "AND ct.name NOT LIKE '%Quest%' "                              // quest scripted mobs
        "AND ct.name NOT LIKE '%zzOLD%' "
        // Exclude creatures whose display model has no geometry. A
        // creature_model_info row with BoundingRadius or CombatReach of 0 renders
        // as nothing client-side: the mob is targetable and fights back, but is
        // invisible. Roughly 2,100 otherwise-eligible entries in a stock 3.3.5a
        // world DB are affected, so an entry blacklist does not scale. An entry is
        // dropped if ANY of its models is degenerate, since the client picks among
        // them at spawn time.
        "AND ct.entry NOT IN ("
        "SELECT ctm2.CreatureID FROM creature_template_model ctm2 "
        "LEFT JOIN creature_model_info cmi2 ON cmi2.DisplayID = ctm2.CreatureDisplayID "
        "WHERE ctm2.CreatureDisplayID = 0 OR cmi2.DisplayID IS NULL "
        "OR cmi2.BoundingRadius <= 0 OR cmi2.CombatReach <= 0) "
        "ORDER BY ct.type, ct.minlevel, ct.entry");

    if (!result)
    {
        LOG_ERROR("module", "Atlas: creature_template query returned NO results — check your world DB!");
        return;
    }

    uint32 trashCount = 0, bossCount = 0;
    if (result)
    {
        do
        {
            Field* f = result->Fetch();
            CreaturePoolEntry e;
            e.Entry    = f[0].Get<uint32>();
            e.Type     = f[1].Get<uint32>();
            e.MinLevel = f[2].Get<uint8>();
            e.MaxLevel = f[3].Get<uint8>();
            uint8 rank = f[4].Get<uint8>();

            if (rank == 1 || rank == 2 || rank == 4)        // elite / rare-elite → boss pool
            {
                _bossCreatures[e.Type].push_back(e);
                ++bossCount;
            }
            else                                              // normal (rank 0) → trash pool
            {
                _creaturesByType[e.Type].push_back(e);
                ++trashCount;
            }
        } while (result->NextRow());
    }

    LOG_INFO("module", "Atlas: Loaded {} trash creatures, {} potential bosses.",
        trashCount, bossCount);

    static char const* typeNames[] = {
        "None", "Beast", "Dragonkin", "Demon", "Elemental",
        "Giant", "Undead", "Humanoid", "Critter", "Mechanical", "NotSpecified"
    };
    for (auto const& [type, vec] : _creaturesByType)
    {
        char const* name = (type <= 10) ? typeNames[type] : "Unknown";
        LOG_INFO("module", "Atlas:   Trash type {} ({}): {} entries",
            type, name, vec.size());
    }
    for (auto const& [type, vec] : _bossCreatures)
    {
        char const* name = (type <= 10) ? typeNames[type] : "Unknown";
        LOG_INFO("module", "Atlas:   Boss  type {} ({}): {} entries",
            type, name, vec.size());
    }
}

// Load real dungeon bosses (scripted elites from all dungeon maps)
void AtlasMgr::LoadDungeonBossPool()
{
    _dungeonBossPool.clear();

    // Build comma-separated list of all dungeon map IDs
    auto const& dungeons = sAtlasConfig->GetDungeons();
    if (dungeons.empty())
    {
        LOG_WARN("module", "Atlas: No dungeons configured — dungeon boss pool empty.");
        return;
    }

    std::string mapList;
    for (size_t i = 0; i < dungeons.size(); ++i)
    {
        if (i > 0) mapList += ",";
        mapList += std::to_string(dungeons[i].MapId);
    }

    // Query for scripted elite creatures that spawn in dungeon maps.
    QueryResult result = WorldDatabase.Query(
        "SELECT DISTINCT ct.entry, ct.name, ct.type, ct.minlevel, ct.maxlevel "
        "FROM creature_template ct "
        "JOIN creature c ON c.id = ct.entry "
        "LEFT JOIN creature_template_movement ctm ON ct.entry = ctm.CreatureId "
        "WHERE c.map IN ({}) "
        "AND ct.`rank` IN (1, 2) "
        "AND ct.ScriptName != '' "
        "AND ct.type > 0 AND ct.type <= 10 "
        "AND ct.minlevel > 0 "
        "AND ct.VehicleId = 0 "
        "AND (ctm.Ground IS NULL OR ctm.Ground != 0) "  // no water-only creatures
        "AND (ct.unit_flags & 2) = 0 "       // not NON_ATTACKABLE
        "AND (ct.unit_flags & 33554432) = 0 " // not NOT_SELECTABLE
        "AND (ct.flags_extra & 128) = 0 "    // not a TRIGGER
        ATLAS_CLIENT_CRASH_DISPLAY_FILTER
        ATLAS_DEGENERATE_MODEL_FILTER
        "AND ct.name NOT LIKE '%Trigger%' "
        "AND ct.name NOT LIKE '%Invisible%' "
        "AND ct.name NOT LIKE '%Dummy%' "
        "AND ct.name NOT LIKE '%Visual%' "
        "AND ct.name NOT LIKE '%Server%' "
        "ORDER BY ct.type, ct.minlevel, ct.entry",
        mapList);

    if (!result)
    {
        LOG_WARN("module", "Atlas: Dungeon boss pool query returned no results.");
        return;
    }

    uint32 count = 0;
    do
    {
        Field* f = result->Fetch();
        CreaturePoolEntry e;
        e.Entry    = f[0].Get<uint32>();
        // f[1] = name (for logging only)
        e.Type     = f[2].Get<uint32>();
        e.MinLevel = f[3].Get<uint8>();
        e.MaxLevel = f[4].Get<uint8>();

        _dungeonBossPool[e.Type].push_back(e);
        ++count;

        LOG_DEBUG("module", "Atlas: Dungeon boss: {} (entry {}, type {}, level {}-{})",
            f[1].Get<std::string>(), e.Entry, e.Type, e.MinLevel, e.MaxLevel);
    } while (result->NextRow());

    LOG_INFO("module", "Atlas: Loaded {} real dungeon bosses into boss pool.", count);

    static char const* typeNames[] = {
        "None", "Beast", "Dragonkin", "Demon", "Elemental",
        "Giant", "Undead", "Humanoid", "Critter", "Mechanical", "NotSpecified"
    };
    for (auto const& [type, vec] : _dungeonBossPool)
    {
        char const* name = (type <= 10) ? typeNames[type] : "Unknown";
        LOG_INFO("module", "Atlas:   Dungeon boss type {} ({}): {} entries",
            type, name, vec.size());
    }
}

// Layouts come from world DB tables that don't change while the server runs:
// StockLayoutProvider builds them once (startup, `.atlas reload`) instead of
// querying the world DB every time a run starts.
void AtlasMgr::LoadDungeonGeometry()
{
    _stockLayouts.Load(sAtlasConfig->GetDungeons());
}

std::shared_ptr<Layout const> AtlasMgr::GetLayout(uint32 mapId) const
{
    return _stockLayouts.GetLayout(mapId);
}

Position AtlasMgr::GetDungeonEntrance(uint32 mapId)
{
    std::shared_ptr<Layout const> layout = GetLayout(mapId);
    return layout ? layout->Entrance : Position(0.0f, 0.0f, 0.0f, 0.0f);
}

// V0's spawning still walks a flat list of spawn points: anchors in their
// order, then one boss position per arena. Same order as before the layout
// seam, so the draws of a seed don't change.
static std::vector<SpawnPoint> ToSpawnPoints(Layout const& layout)
{
    std::vector<SpawnPoint> points;
    points.reserve(layout.Anchors.size() + layout.Arenas.size());
    for (LayoutAnchor const& anchor : layout.Anchors)
    {
        SpawnPoint sp;
        sp.Pos = anchor.Pos;
        sp.DistanceFromEntrance = anchor.DistanceFromEntrance;
        points.push_back(sp);
    }
    for (LayoutArena const& arena : layout.Arenas)
    {
        SpawnPoint sp;
        sp.Pos = arena.Centre;
        sp.DistanceFromEntrance = arena.DistanceFromEntrance;
        sp.IsBossPosition = true;
        points.push_back(sp);
    }
    return points;
}

// Instance population
void AtlasMgr::ClearDungeonCreatures(InstanceMap* map)
{
    if (!map) return;

    uint32 npcEntry = sAtlasConfig->GetNpcEntry();
    uint32 totalRemoved = 0;

    // Phase 1: despawn our tracked creatures
    uint32 instanceId = map->GetInstanceId();
    auto guidIt = _instanceCreatureGuids.find(instanceId);
    if (guidIt != _instanceCreatureGuids.end())
    {
        for (ObjectGuid const& guid : guidIt->second)
        {
            Creature* c = map->GetCreature(guid);
            if (c && c->IsInWorld())
            {
                c->DespawnOrUnsummon();
                ++totalRemoved;
            }
        }
        guidIt->second.clear();
    }

    uint32 dbRemoved = 0;

    // Phase 2: despawn DB-spawned creatures
    std::vector<Creature*> dbCreatures;
    auto const& store = map->GetCreatureBySpawnIdStore();
    for (auto const& pair : store)
    {
        Creature* c = pair.second;
        if (c && c->IsInWorld() && !c->IsPet() && !c->IsGuardian()
            && !c->IsTotem() && c->GetEntry() != npcEntry)
        {
            dbCreatures.push_back(c);
        }
    }

    for (Creature* c : dbCreatures)
    {
        if (c && c->IsInWorld() && !c->IsPet() && !c->IsGuardian()
            && !c->IsTotem() && c->GetEntry() != npcEntry)
        {
            c->SetRespawnTime(7 * DAY);
            c->DespawnOrUnsummon();
            ++dbRemoved;
        }
    }

    // Phase 3: grid sweep for script-spawned creatures
    uint32 gridRemoved = 0;
    Map::PlayerList const& players = map->GetPlayers();
    for (auto const& itr : players)
    {
        Player* p = itr.GetSource();
        if (!p || !p->IsInWorld()) continue;

        std::list<Creature*> gridCreatures;
        p->GetCreatureListWithEntryInGrid(gridCreatures, 0, 5000.0f);

        for (Creature* c : gridCreatures)
        {
            if (!c || !c->IsInWorld()) continue;
            if (c->IsPet() || c->IsGuardian() || c->IsTotem()) continue;
            if (c->GetEntry() == npcEntry) continue;

            c->SetRespawnTime(7 * DAY);
            c->DespawnOrUnsummon();
            ++gridRemoved;
        }

        // Only need one player's grid sweep (the grid search radius
        // is large enough to cover the entire dungeon)
        break;
    }

    LOG_INFO("module", "Atlas: Cleared {} tracked + {} DB + {} grid creatures from map {} (inst {})",
        totalRemoved, dbRemoved, gridRemoved, map->GetId(), instanceId);
}

void AtlasMgr::OpenAllDoors(InstanceMap* map)
{
    if (!map) return;

    std::vector<GameObject*> doors;
    auto const& store = map->GetGameObjectBySpawnIdStore();
    for (auto const& pair : store)
    {
        GameObject* go = pair.second;
        if (!go || !go->IsInWorld()) continue;
        if (go->GetGoType() == GAMEOBJECT_TYPE_DOOR || go->GetGoType() == GAMEOBJECT_TYPE_BUTTON)
            doors.push_back(go);
    }

    for (GameObject* go : doors)
        if (go && go->IsInWorld())
            go->Delete();

    LOG_DEBUG("module", "Atlas: Removed {} doors from instance.", doors.size());
}

// Populate dungeon with themed creatures and bosses
void AtlasMgr::PopulateDungeon(Session* session, InstanceMap* map)
{
    if (!session || !map) return;

    LOG_INFO("module", "Atlas: PopulateDungeon ENTRY — session {} map {} instId {} mobs {} bosses {}",
        session->SessionId, session->MapId, map->GetInstanceId(),
        session->TotalMobs, session->TotalBosses);

    // Remember the instance before emptying it, so a restart mid-run can
    // release it (see ReleaseInstancesLeftOpen).
    RecordRunActive(*session, map->GetInstanceId(), map->GetId());

    DifficultyTier const* diff  = sAtlasConfig->GetDifficulty(session->DifficultyId);
    Theme const*          theme = sAtlasConfig->GetTheme(session->ThemeId);
    if (!diff || !theme) return;

    ClearDungeonCreatures(map);
    OpenAllDoors(map);

    // Mark all boss encounters as DONE so scripts don't interfere
    if (InstanceScript* script = map->GetInstanceScript())
    {
        uint32 bossesNeutralized = 0;
        // Loop through boss indices.  GetBossState returns TO_BE_DECIDED (5)
        // for indices past the end of the encounter list — use as sentinel.
        for (uint32 i = 0; i < 25; ++i)
        {
            EncounterState state = script->GetBossState(i);
            if (state == TO_BE_DECIDED)
                break;   // Past the last boss — stop

            if (state != DONE)
            {
                script->SetBossState(i, DONE);
                ++bossesNeutralized;
            }
        }
        if (bossesNeutralized > 0)
            LOG_INFO("module", "Atlas: Neutralized {} InstanceScript boss encounter(s) in map {}",
                bossesNeutralized, map->GetId());
    }

    // Purge lingering debuffs from despawned creatures
    for (auto const& pd : session->Players)
    {
        Player* p = ObjectAccessor::FindPlayer(pd.PlayerGuid);
        if (!p || !p->IsInWorld()) continue;

        std::vector<uint32> toRemove;
        for (auto const& pair : p->GetAppliedAuras())
        {
            if (Aura* aura = pair.second->GetBase())
            {
                // Caster is gone (despawned) and it wasn't self-cast — lingering debuff
                if (!aura->GetCaster() && aura->GetCasterGUID() != p->GetGUID())
                    toRemove.push_back(aura->GetId());
            }
        }
        for (uint32 spellId : toRemove)
            p->RemoveAura(spellId);

        if (!toRemove.empty())
            LOG_DEBUG("module", "Atlas: Purged {} lingering debuff(s) from {}",
                toRemove.size(), p->GetName());
    }

    if (std::shared_ptr<Layout const> layout = GetLayout(session->MapId))
        session->SpawnPoints = ToSpawnPoints(*layout);
    if (session->SpawnPoints.empty())
    {
        LOG_ERROR("module", "Atlas: No spawn points for map {}", session->MapId);
        return;
    }

    float hpMult  = CalculateHealthMultiplier(session);
    float dmgMult = CalculateDamageMultiplier(session);

    uint8 bandMin = session->LevelBandMin;
    uint8 bandMax = session->LevelBandMax;
    uint8 targetLevel = session->EffectiveLevel;

    uint32 instanceId = map->GetInstanceId();
    auto& guidList = _instanceCreatureGuids[instanceId];
    guidList.clear();

    LOG_INFO("module", "Atlas: Populating session {} — theme '{}', band {}-{}, target lvl {}, HP x{:.2f}, DMG x{:.2f}",
        session->SessionId, theme->Name, bandMin, bandMax, targetLevel, hpMult, dmgMult);

    // Force-scale creature to target level
    // Compute a boss-specific damage multiplier that only includes party scaling,
    // NOT the difficulty tier's DamageMultiplier (to avoid double-stacking).
    float bossOnlyDmgMult;
    {
        uint32 n = session->Players.size();
        if (n <= 1) bossOnlyDmgMult = sAtlasConfig->GetSoloMultiplier();
        else        bossOnlyDmgMult = 1.0f + (n - 1) * sAtlasConfig->GetPerPlayerDamageMult();
        if (session->RoguelikeRunId != 0)
            bossOnlyDmgMult *= sRoguelikeMgr->GetTierDamageMultiplier(session->RoguelikeRunId);
    }

    auto applyLevelAndStats = [&](Creature* c, float extraHpMult, float extraDmgMult, bool isBoss)
    {
        // Drop auras the template applied at spawn (creature_template_addon,
        // e.g. 28006 Grand Necrolord Antiok's 50494): they were tuned for the
        // original encounter and can make a creature unkillable here
        // (upstream #24, cause not confirmed in game).
        c->RemoveAllAuras();

        c->SetLevel(targetLevel);

        if (isBoss)
        {
            // The gold dragon frame comes from creature_template.rank, which the
            // client caches per entry — it is not settable at runtime. The boss
            // pool is already filtered to `rank IN (1,2)`, so every boss draws the
            // elite frame on its own and nothing is needed here.
            //
            // The previous SetByteValue(UNIT_FIELD_BYTES_0, 2, 1) did not set rank:
            // byte 2 of that field is gender (Unit.h; byte 0=race, 1=class,
            // 2=gender, 3=power type — there is no classification byte). Its only
            // effect was to render every boss as gender 1.
            c->SetObjectScale(1.3f);                      // 30% larger than normal
        }

        uint8 unitClass = c->GetCreatureTemplate()->unit_class;
        ClassLevelStatEntry const* baseStats = GetBaseStatsForLevel(unitClass, targetLevel);

        float finalHP;
        if (baseStats)
            finalHP = static_cast<float>(baseStats->BaseHP) * hpMult * extraHpMult;
        else
            finalHP = c->GetMaxHealth() * hpMult * extraHpMult;

        uint32 hp = std::max(1u, static_cast<uint32>(finalHP));
        c->SetMaxHealth(hp);
        c->SetHealth(hp);

        // For bosses, use party-only scaling (bossOnlyDmgMult) instead of the full
        // tier+party dmgMult to prevent double-stacking tier DamageMultiplier with BossDamageMult
        float effectiveDmgMult = isBoss ? bossOnlyDmgMult : dmgMult;

        if (baseStats)
        {
            float dmgBase  = baseStats->BaseDamage;
            float apBonus  = static_cast<float>(baseStats->AttackPower) / 14.0f;
            float atkTime  = static_cast<float>(c->GetCreatureTemplate()->BaseAttackTime) / 1000.0f;
            if (atkTime <= 0.0f) atkTime = 2.0f;

            float minDmg = (dmgBase + apBonus) * atkTime * effectiveDmgMult * extraDmgMult;
            float maxDmg = ((dmgBase * 1.15f) + apBonus) * atkTime * effectiveDmgMult * extraDmgMult;

            minDmg = std::max(1.0f, minDmg);
            maxDmg = std::max(minDmg, maxDmg);

            c->SetBaseWeaponDamage(BASE_ATTACK, MINDAMAGE, minDmg);
            c->SetBaseWeaponDamage(BASE_ATTACK, MAXDAMAGE, maxDmg);
            c->UpdateDamagePhysical(BASE_ATTACK);
        }

        // --- Armor (from classlevelstats for the TARGET level) ---
        if (baseStats && baseStats->BaseArmor > 0)
            c->SetArmor(baseStats->BaseArmor);

        // --- Roguelike: additional armor scaling from tier progression ---
        if (session->RoguelikeRunId != 0)
        {
            float armorMult = sRoguelikeMgr->GetTierArmorMultiplier(session->RoguelikeRunId);
            if (armorMult > 1.0f)
                c->SetArmor(static_cast<uint32>(c->GetArmor() * armorMult));
        }

        // --- Clear ALL spell resistances (original template values are for original level) ---
        for (uint8 school = SPELL_SCHOOL_HOLY; school < MAX_SPELL_SCHOOL; ++school)
            c->SetResistance(SpellSchools(school), 0);

        // --- Clear mechanic immunities ---
        for (uint32 mech = 1; mech < MAX_MECHANIC; ++mech)
            c->ApplySpellImmune(0, IMMUNITY_MECHANIC, mech, false);

        // --- Clear spell immunities that might come from the template ---
        c->ApplySpellImmune(0, IMMUNITY_SCHOOL, SPELL_SCHOOL_MASK_ALL, false);

        // --- Movement ---
        if (isBoss)
        {
            // Bosses stay at their spawn point until engaged.
            c->SetWanderDistance(0.0f);
            c->SetDefaultMovementType(IDLE_MOTION_TYPE);
        }
        else
        {
            // Trash mobs patrol a 5 yd radius around their spawn point
            c->SetWanderDistance(5.0f);
            c->SetDefaultMovementType(RANDOM_MOTION_TYPE);
            c->GetMotionMaster()->MoveRandom(5.0f);
        }

        // --- Install custom AI ---
        // All spawned creatures (trash and bosses alike) use AtlasCreatureAI,
        // which handles aggro scanning, patrol movement, and death hooks.
        // Bosses rely on auto-attack only — no scripted spell rotations.
        // AIM_Initialize deletes the creature's previous AI; Unit::SetAI only
        // swaps the pointer, which leaked one AI object per spawned creature.
        // Before deleting it, cancel the creature's pending events: stock
        // scripts queue lambdas on me->m_Events that capture their AI (e.g.
        // Murmur, Keleseth, Novos schedule them in Reset at spawn), and those
        // would fire into the deleted AI a few seconds later (use-after-free).
        c->m_Events.KillAllEvents(false);
        CreatureAI* dmAI = new AtlasCreatureAI(c);
        if (!c->AIM_Initialize(dmAI))
            delete dmAI;

        // Force visibility refresh or client won't see the creature
        c->UpdateObjectVisibility(true);

        // Track this GUID for future cleanup
        guidList.push_back(c->GetGUID());

        // Remember the assigned level so it can be re-asserted if anything
        // reverts it.
        RegisterOwnedCreature(c->GetGUID(), targetLevel, hp, instanceId);
    };

    // Spawn trash mobs
    uint32 spawnedMobs = 0;
    for (auto& sp : session->SpawnPoints)
    {
        if (sp.IsBossPosition) continue;

        // Every draw for this spawn point happens before the summon, so a summon
        // that fails doesn't shift the draws of the points after it (C8).
        uint32 entry = SelectCreatureForTheme(theme, false, session->Rng, session);
        if (!entry) continue;

        bool isElite = session->Rng.Chance(sAtlasConfig->GetEliteChance());

        // Roguelike affix multipliers for trash
        float affixHpMult = 1.0f, affixDmgMult = 1.0f, affixEliteMult = 1.0f;
        if (session->RoguelikeRunId != 0)
        {
            sRoguelikeMgr->GetAffixMultipliers(session->RoguelikeRunId,
                false, isElite, affixHpMult, affixDmgMult, affixEliteMult);
            // Savage affix: boosted elite chance
            if (affixEliteMult > 1.0f && !isElite)
            {
                uint32 boostedChance = static_cast<uint32>(sAtlasConfig->GetEliteChance() * affixEliteMult);
                isElite = session->Rng.Chance(boostedChance);
            }
        }

        Creature* c = map->SummonCreature(entry, sp.Pos);
        if (!c) continue;

        c->SetFaction(14);               // hostile to all
        c->SetReactState(REACT_AGGRESSIVE);
        float baseScale = c->GetObjectScale();
        float typeScale = sAtlasConfig->GetVisualScaleForCreatureType(c->GetCreatureTemplate()->type);
        c->SetObjectScale(std::clamp(baseScale * typeScale, 0.55f, 1.0f));
        c->SetCorpseDelay(300);          // 5 min corpse before despawn
        c->RemoveFlag(UNIT_FIELD_FLAGS, UNIT_FLAG_NON_ATTACKABLE | UNIT_FLAG_IMMUNE_TO_PC
                                        | UNIT_FLAG_IMMUNE_TO_NPC | UNIT_FLAG_PACIFIED
                                        | UNIT_FLAG_STUNNED | UNIT_FLAG_FLEEING
                                        | UNIT_FLAG_NOT_SELECTABLE);
        c->SetUInt32Value(UNIT_FIELD_FLAGS_2, 0);
        c->SetImmuneToPC(false);
        c->SetImmuneToNPC(false);
        c->setActive(true);             // Keep creature in grid update cycle for aggro detection

        float eliteHpMult  = isElite ? sAtlasConfig->GetEliteHealthMult() : 1.0f;
        float eliteDmgMult = isElite ? 1.5f : 1.0f;

        applyLevelAndStats(c, eliteHpMult * affixHpMult, eliteDmgMult * affixDmgMult, false);

        SpawnedCreature sc;
        sc.Guid = c->GetGUID(); sc.Entry = entry;
        sc.IsElite = isElite; sc.IsBoss = false;
        session->SpawnedCreatures.push_back(sc);
        if (uint32 recentLimit = sAtlasConfig->GetRecentCreatureEntries(); recentLimit > 0)
        {
            session->RecentCreatureEntries.push_back(entry);
            if (session->RecentCreatureEntries.size() > recentLimit)
                session->RecentCreatureEntries.erase(session->RecentCreatureEntries.begin());
        }

        ++spawnedMobs;
    }
    session->TotalMobs = spawnedMobs;

    // --- Rare spawn (configurable chance, max 1 per run) ---
    if (sAtlasConfig->GetRareSpawnChance() > 0 &&
        session->Rng.Chance(sAtlasConfig->GetRareSpawnChance()))
    {
        // Pick non-boss spawn points for rare placement (prefer middle of dungeon)
        std::vector<size_t> validRarePoints;
        for (size_t i = 0; i < session->SpawnPoints.size(); ++i)
            if (!session->SpawnPoints[i].IsBossPosition)
                validRarePoints.push_back(i);

        if (!validRarePoints.empty())
        {
            size_t startIdx = validRarePoints.size() / 3;
            size_t endIdx   = std::max(startIdx, validRarePoints.size() * 2 / 3);
            if (endIdx >= validRarePoints.size()) endIdx = validRarePoints.size() - 1;
            size_t pickIdx  = validRarePoints[session->Rng.Range(uint32(startIdx), uint32(endIdx))];
            SpawnPoint& rareSP = session->SpawnPoints[pickIdx];

            uint32 rareEntry = SelectCreatureForTheme(theme, true, session->Rng);
            if (rareEntry)
            {
                Creature* r = map->SummonCreature(rareEntry, rareSP.Pos);
                if (r)
                {
                    r->SetFaction(14);
                    r->SetReactState(REACT_AGGRESSIVE);
                    r->SetCorpseDelay(300);
                    r->RemoveFlag(UNIT_FIELD_FLAGS, UNIT_FLAG_NON_ATTACKABLE | UNIT_FLAG_IMMUNE_TO_PC
                                                    | UNIT_FLAG_IMMUNE_TO_NPC | UNIT_FLAG_PACIFIED
                                                    | UNIT_FLAG_STUNNED | UNIT_FLAG_FLEEING
                                                    | UNIT_FLAG_NOT_SELECTABLE);
                    r->SetUInt32Value(UNIT_FIELD_FLAGS_2, 0);
                    r->SetImmuneToPC(false);
                    r->SetImmuneToNPC(false);
                    r->setActive(true);

                    // As above: byte 2 is gender, not rank, and 4 is not a valid
                    // gender value. Rares come from the same `rank IN (1,2)` pool,
                    // so they already carry an elite frame; size is the extra tell.
                    r->SetObjectScale(1.15f);

                    float rareHpMult  = sAtlasConfig->GetRareHealthMult();
                    float rareDmgMult = sAtlasConfig->GetRareDamageMult();

                    // Apply roguelike affix multipliers to rares
                    float affixHpM = 1.0f, affixDmgM = 1.0f, affixEliteM = 1.0f;
                    if (session->RoguelikeRunId != 0)
                        sRoguelikeMgr->GetAffixMultipliers(session->RoguelikeRunId,
                            false, true, affixHpM, affixDmgM, affixEliteM);

                    // Also installs the custom AI (rare = enhanced trash).
                    applyLevelAndStats(r, rareHpMult * affixHpM, rareDmgMult * affixDmgM, false);

                    SpawnedCreature sc;
                    sc.Guid = r->GetGUID(); sc.Entry = rareEntry;
                    sc.IsElite = true; sc.IsBoss = false; sc.IsRare = true;
                    session->SpawnedCreatures.push_back(sc);
                    guidList.push_back(r->GetGUID());

                    for (auto const& pd : session->Players)
                        if (Player* p = ObjectAccessor::FindPlayer(pd.PlayerGuid))
                            if (p->GetSession())
                                ChatHandler(p->GetSession()).PSendSysMessage(
                                    "{} Something |cFFFF8800rare|r has slipped into this echo from another timeline.",
                                    CHAT_PREFIX);

                    LOG_INFO("module", "Atlas: Rare creature spawned — entry {} at ({:.1f}, {:.1f}, {:.1f})",
                        rareEntry, rareSP.Pos.GetPositionX(), rareSP.Pos.GetPositionY(), rareSP.Pos.GetPositionZ());
                }
            }
        }
    }

    // Spawn bosses (real dungeon bosses)
    uint32 bossesSpawned = 0;
    for (auto& sp : session->SpawnPoints)
    {
        if (!sp.IsBossPosition || bossesSpawned >= sAtlasConfig->GetBossCount())
            continue;

        uint32 entry = SelectDungeonBoss(theme, session->Rng);
        if (!entry) { LOG_WARN("module", "Atlas: No boss candidate."); continue; }

        Creature* b = map->SummonCreature(entry, sp.Pos);
        if (!b) continue;

        b->SetFaction(14);
        b->SetReactState(REACT_AGGRESSIVE);
        b->SetCorpseDelay(600);          // 10 min corpse for bosses
        b->RemoveFlag(UNIT_FIELD_FLAGS, UNIT_FLAG_NON_ATTACKABLE | UNIT_FLAG_IMMUNE_TO_PC
                                        | UNIT_FLAG_IMMUNE_TO_NPC | UNIT_FLAG_PACIFIED
                                        | UNIT_FLAG_STUNNED | UNIT_FLAG_FLEEING
                                        | UNIT_FLAG_NOT_SELECTABLE);
        b->SetUInt32Value(UNIT_FIELD_FLAGS_2, 0);
        b->SetImmuneToPC(false);
        b->SetImmuneToNPC(false);
        b->setActive(true);             // Keep creature in grid update cycle for aggro detection

        // Roguelike affix multipliers for bosses
        float bossAffixHpMult = 1.0f, bossAffixDmgMult = 1.0f, _unused = 1.0f;
        if (session->RoguelikeRunId != 0)
            sRoguelikeMgr->GetAffixMultipliers(session->RoguelikeRunId,
                true, true, bossAffixHpMult, bossAffixDmgMult, _unused);

        applyLevelAndStats(b,
            sAtlasConfig->GetBossHealthMult() * bossAffixHpMult,
            sAtlasConfig->GetBossDamageMult() * bossAffixDmgMult, true);

        SpawnedCreature sc;
        sc.Guid = b->GetGUID(); sc.Entry = entry;
        sc.IsElite = true; sc.IsBoss = true;
        session->SpawnedCreatures.push_back(sc);
        ++bossesSpawned;

        LOG_INFO("module", "Atlas: Boss spawned — entry {}, name '{}', "
            "AI: AtlasCreatureAI (auto-attack), ReactState: {}, Level: {}",
            b->GetEntry(), b->GetName(),
            static_cast<int>(b->GetReactState()),
            b->GetLevel());
    }
    session->TotalBosses = bossesSpawned;

    LOG_INFO("module", "Atlas: Session {} — {} mobs, {} bosses spawned.",
        session->SessionId, session->TotalMobs, session->TotalBosses);

    // --- Reset encounter states to NOT_STARTED so boss AIs can engage properly ---
    // We set all encounters to DONE earlier (line ~1049) to clear original dungeon
    // bosses. Now that our custom bosses are spawned, reset encounters so their
    // ScriptName AIs do not think the encounter is already defeated.
    if (InstanceScript* script = map->GetInstanceScript())
    {
        uint32 encountersReset = 0;
        for (uint32 i = 0; i < 25; ++i)
        {
            EncounterState state = script->GetBossState(i);
            if (state == TO_BE_DECIDED)
                break;
            if (state == DONE)
            {
                script->SetBossState(i, NOT_STARTED);
                ++encountersReset;
            }
        }
        if (encountersReset > 0)
            LOG_INFO("module", "Atlas: Reset {} encounter(s) to NOT_STARTED for boss AI activation.",
                encountersReset);
    }

    // --- Spawn roguelike vendor NPC at entrance ---
    if (session->RoguelikeRunId != 0 && sAtlasConfig->IsRoguelikeVendorEnabled())
    {
        static constexpr uint32 ATLAS_VENDOR_NPC_ENTRY = 500001;

        // Small offset from entrance so vendor doesn't overlap player spawn point
        float vendorX = session->EntrancePos.GetPositionX() + 3.0f;
        float vendorY = session->EntrancePos.GetPositionY() + 2.0f;
        float vendorZ = session->EntrancePos.GetPositionZ();
        float vendorO = session->EntrancePos.GetOrientation();

        Creature* vendor = map->SummonCreature(ATLAS_VENDOR_NPC_ENTRY,
            { vendorX, vendorY, vendorZ, vendorO });
        if (vendor)
        {
            vendor->SetFaction(35);           // friendly to all
            vendor->SetFlag(UNIT_FIELD_FLAGS, UNIT_FLAG_NON_ATTACKABLE);
            vendor->SetImmuneToPC(true);
            vendor->SetImmuneToNPC(true);
            vendor->SetWanderDistance(0.0f);
            vendor->SetDefaultMovementType(IDLE_MOTION_TYPE);
            vendor->GetMotionMaster()->MoveIdle();
            vendor->setActive(true);
            vendor->UpdateObjectVisibility(true);

            // Track for cleanup — ClearDungeonCreatures() will despawn it
            guidList.push_back(vendor->GetGUID());

            LOG_INFO("module", "Atlas: Spawned roguelike vendor at ({:.1f}, {:.1f}, {:.1f}) for session {}",
                vendorX, vendorY, vendorZ, session->SessionId);
        }
        else
        {
            LOG_WARN("module", "Atlas: Failed to spawn roguelike vendor for session {}",
                session->SessionId);
        }
    }
}

// Select a creature matching the theme
uint32 AtlasMgr::SelectCreatureForTheme(Theme const* theme, bool isBoss, RunRng& rng, Session const* session)
{
    if (!theme) return 0;

    std::set<uint32> types;
    bool anyType = false;
    for (uint32 t : theme->CreatureTypes)
    {
        if (t == uint32(-1)) anyType = true;
        else types.insert(t);
    }

    auto typeMatch = [&](uint32 cType) -> bool
    {
        return anyType || types.count(cType);
    };

    std::vector<uint32> candidates;

    if (isBoss)
    {
        // --- Try themed elites first ---
        for (auto const& [type, vec] : _bossCreatures)
        {
            if (!typeMatch(type)) continue;
            for (auto const& e : vec)
                candidates.push_back(e.Entry);
        }

        // --- Fallback: promote themed trash to boss (stats will be scaled up) ---
        if (candidates.empty())
        {
            for (auto const& [type, vec] : _creaturesByType)
            {
                if (!typeMatch(type)) continue;
                for (auto const& e : vec)
                    candidates.push_back(e.Entry);
            }
        }
    }
    else
    {
        // --- Themed trash ---
        for (auto const& [type, vec] : _creaturesByType)
        {
            if (!typeMatch(type)) continue;
            for (auto const& e : vec)
                candidates.push_back(e.Entry);
        }
    }

    // Fallback: any type
    if (candidates.empty() && !anyType)
    {
        LOG_WARN("module", "Atlas: No '{}' creatures found — falling back to any type.",
            theme->Name);

        if (isBoss)
        {
            for (auto const& [type, vec] : _bossCreatures)
                for (auto const& e : vec)
                    candidates.push_back(e.Entry);
        }

        if (candidates.empty())
        {
            for (auto const& [type, vec] : _creaturesByType)
                for (auto const& e : vec)
                    candidates.push_back(e.Entry);
        }
    }

    // Avoid repeating recently selected trash entries when alternatives exist.
    if (!isBoss && session && sAtlasConfig->GetRecentCreatureEntries() > 0
        && !session->RecentCreatureEntries.empty())
    {
        std::vector<uint32> filtered;
        filtered.reserve(candidates.size());
        for (uint32 entry : candidates)
            if (std::find(session->RecentCreatureEntries.begin(), session->RecentCreatureEntries.end(), entry)
                == session->RecentCreatureEntries.end())
                filtered.push_back(entry);
        if (!filtered.empty())
            candidates.swap(filtered);
    }

    if (!candidates.empty())
    {
        LOG_DEBUG("module", "Atlas: {} candidates for theme '{}' (boss={})",
            candidates.size(), theme->Name, isBoss);
        return rng.Pick(candidates);
    }

    LOG_ERROR("module", "Atlas: ZERO candidates for theme '{}' (boss={})",
        theme->Name, isBoss);
    return 0;
}

uint32 AtlasMgr::SelectDungeonBoss(Theme const* theme, RunRng& rng)
{
    if (!theme) return 0;

    std::set<uint32> types;
    bool anyType = false;
    for (uint32 t : theme->CreatureTypes)
    {
        if (t == uint32(-1)) anyType = true;
        else types.insert(t);
    }

    auto typeMatch = [&](uint32 cType) -> bool
    {
        return anyType || types.count(cType);
    };

    // Prefer themed dungeon bosses
    std::vector<uint32> candidates;
    for (auto const& [type, vec] : _dungeonBossPool)
    {
        if (!typeMatch(type)) continue;
        for (auto const& e : vec)
            candidates.push_back(e.Entry);
    }

    // Fallback: any dungeon boss
    if (candidates.empty())
    {
        LOG_DEBUG("module", "Atlas: No themed dungeon boss for '{}' — using any dungeon boss.",
            theme->Name);
        for (auto const& [type, vec] : _dungeonBossPool)
            for (auto const& e : vec)
                candidates.push_back(e.Entry);
    }

    // Last resort: generic boss pool
    if (candidates.empty())
    {
        LOG_WARN("module", "Atlas: Dungeon boss pool empty — falling back to generic boss selection.");
        return SelectCreatureForTheme(theme, true, rng);
    }

    uint32 entry = rng.Pick(candidates);
    LOG_DEBUG("module", "Atlas: Selected dungeon boss entry {} from {} candidates (theme '{}')",
        entry, candidates.size(), theme->Name);
    return entry;
}

} // namespace Atlas
