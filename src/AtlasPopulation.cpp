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
#include "AtlasAffixes.h"
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
#include "TemporarySummon.h"

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

    std::shared_ptr<Layout const> layout = GetLayout(session->MapId);
    if (!layout || (layout->Anchors.empty() && layout->Arenas.empty()))
    {
        LOG_ERROR("module", "Atlas: No layout for map {}", session->MapId);
        return;
    }

    uint8 bandMin = session->LevelBandMin;
    uint8 bandMax = session->LevelBandMax;
    uint8 targetLevel = session->EffectiveLevel;

    uint32 instanceId = map->GetInstanceId();
    auto& guidList = _instanceCreatureGuids[instanceId];
    guidList.clear();

    // Corruption (DESIGN §8): native, Scourge-touched and Scourge packs by
    // the run's tier; a Scourge timeline is Scourge throughout.
    uint32 const scourgeThemeId = sAtlasConfig->GetScourgeThemeId();
    CorruptionShares const shares = theme->Id == scourgeThemeId
        ? CorruptionShares{ 0, 0, 100 }
        : sAtlasConfig->GetCorruptionShares(CorruptionLevel(session->Tier));

    LOG_INFO("module", "Atlas: Populating session {} — theme '{}', band {}-{}, target lvl {}, tier {}, "
        "health x{:.2f}, damage x{:.2f}, packs {}/{}/{} native/touched/Scourge",
        session->SessionId, theme->Name, bandMin, bandMax, targetLevel, session->Tier,
        SessionHealthFactor(*session), SessionDamageFactor(*session), shares.Native, shares.Touched, shares.Scourge);

    // Every summon below is scaled here; keep the add-to-world hook out of it.
    OwnSpawnScope const ownSpawns;
    ScalingContext const ctx = ContextOf(*session);
    AffixEffects const& fx = session->Effects;

    // The plague's mark on Scourge-touched creatures: an aura and an affix.
    uint32 const touchedAura = sAtlasConfig->GetTouchedAura();
    std::string unknownTouched;
    std::vector<ActiveAffix> const touchedAffixes =
        sAtlasAffixes->Parse(sAtlasConfig->GetTouchedAffixCode(), session->Tier, unknownTouched);

    // Level, role stats (DESIGN §9), the run's affixes (and the creature's
    // own) and the AI for every spawn.
    auto applyLevelAndStats = [&](Creature* c, CreatureRole role, bool isBoss, std::vector<ActiveAffix> const& own)
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
            // client caches per entry; the boss pools hold elites, so it shows.
            c->SetObjectScale(1.3f);                      // 30% larger than normal
        }

        // Health, melee, armor and spell/heal scales from the role table, the
        // run's affixes; also clears the template's spell resistances and
        // records ownership.
        ScaleCreature(c, ctx, role, own);

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

        // --- AI ---
        // SmartAI creatures keep their own AI and spells (KeepsStockAI);
        // everything else, and every boss, gets AtlasCreatureAI: aggro
        // scanning, patrols, no positional evades, melee only.
        // AIM_Initialize deletes the creature's previous AI; Unit::SetAI only
        // swaps the pointer, which leaked one AI object per spawned creature.
        // Before deleting it, cancel the creature's pending events: stock
        // scripts queue lambdas on me->m_Events that capture their AI (e.g.
        // Murmur, Keleseth, Novos schedule them in Reset at spawn), and those
        // would fire into the deleted AI a few seconds later (use-after-free).
        if (isBoss || !KeepsStockAI(c))
        {
            c->m_Events.KillAllEvents(false);
            CreatureAI* dmAI = new AtlasCreatureAI(c);
            if (!c->AIM_Initialize(dmAI))
                delete dmAI;
        }

        // Force visibility refresh or client won't see the creature
        c->UpdateObjectVisibility(true);

        // Track this GUID for future cleanup
        guidList.push_back(c->GetGUID());
    };

    auto prepareHostile = [](Creature* c)
    {
        c->SetFaction(14);               // hostile to all
        c->SetReactState(REACT_AGGRESSIVE);
        c->RemoveFlag(UNIT_FIELD_FLAGS, UNIT_FLAG_NON_ATTACKABLE | UNIT_FLAG_IMMUNE_TO_PC
                                        | UNIT_FLAG_IMMUNE_TO_NPC | UNIT_FLAG_PACIFIED
                                        | UNIT_FLAG_STUNNED | UNIT_FLAG_FLEEING
                                        | UNIT_FLAG_NOT_SELECTABLE);
        c->SetUInt32Value(UNIT_FIELD_FLAGS_2, 0);
        c->SetImmuneToPC(false);
        c->SetImmuneToNPC(false);
        c->setActive(true);             // Keep creature in grid update cycle for aggro detection
    };

    auto rankRole = [](uint32 entry)
    {
        CreatureTemplate const* t = sObjectMgr->GetCreatureTemplate(entry);
        return t && t->rank > 0 ? CreatureRole::Elite : CreatureRole::Trash;
    };

    // A pack member's creature: the Scourge pool, the chosen pool, the
    // dungeon's own inhabitant at that spot, or a creature of the chosen type.
    auto pickEntry = [&](PackKind kind, LayoutSpot const* spot, std::size_t index, CreatureRole& role) -> uint32
    {
        if (kind == PackKind::Scourge || theme->Kind == ThemeKind::Pool)
        {
            PackRole const packRole = PickMemberRole(index, session->Rng);
            uint32 const entry = PickPoolEntry(kind == PackKind::Scourge ? scourgeThemeId : theme->Id, packRole,
                session->Rng);
            role = packRole == PackRole::Leader ? CreatureRole::Elite : rankRole(entry);
            return entry;
        }
        if (theme->Kind == ThemeKind::Native)
        {
            uint32 const entry = spot ? spot->NativeEntry : 0;
            if (!entry || GetCuration(entry) == CurationStatus::Blocked)
                return 0;
            role = rankRole(entry);
            return entry;
        }
        role = CreatureRole::Trash;
        return SelectCreatureForTheme(theme, false, session->Rng, session);
    };

    uint32 const maxCreatures = sAtlasConfig->GetMaxRunCreatures();
    uint32 const baseElite = std::min<uint32>(100,
        sAtlasConfig->GetEliteChance() + static_cast<uint32>(fx.EliteChanceBonus));
    uint32 const promoteElite = std::min<uint32>(100, static_cast<uint32>(fx.EliteChanceBonus));
    uint32 const rareChance = std::min<uint32>(100,
        static_cast<uint32>(sAtlasConfig->GetRareSpawnChance() * fx.RareChanceMult));
    uint32 spawnedMobs = 0;
    uint32 raresSpawned = 0;
    std::vector<std::string> rareNotes;

    auto spawnMember = [&](PackKind kind, LayoutSpot const* spot, Position const& pos, std::size_t index,
        bool asRare, bool mayBeElite)
    {
        // Every draw for a member happens before the summon, so a summon that
        // fails doesn't shift the draws after it (C8).
        CreatureRole role = CreatureRole::Trash;
        uint32 const entry = pickEntry(kind, spot, index, role);
        bool const typeTheme = kind != PackKind::Scourge && theme->Kind == ThemeKind::Type;
        bool const eliteRoll = session->Rng.Chance(typeTheme ? baseElite : promoteElite);
        std::vector<ActiveAffix> own;
        if (asRare && entry)
            own = sAtlasAffixes->RollRareModifiers(session->Tier, session->Rng);
        if (!entry)
            return;

        if (mayBeElite && eliteRoll && role == CreatureRole::Trash)
            role = CreatureRole::Elite;
        if (asRare)
            role = CreatureRole::Rare;
        bool const touched = kind == PackKind::Touched;
        if (touched)
            own.insert(own.end(), touchedAffixes.begin(), touchedAffixes.end());

        Creature* c = map->SummonCreature(entry, pos);
        if (!c)
            return;

        prepareHostile(c);
        float baseScale = c->GetObjectScale();
        float typeScale = sAtlasConfig->GetVisualScaleForCreatureType(c->GetCreatureTemplate()->type);
        c->SetObjectScale(std::clamp(baseScale * typeScale * (asRare ? 1.15f : 1.0f), 0.55f, 1.15f));
        c->SetCorpseDelay(300);          // 5 min corpse before despawn

        applyLevelAndStats(c, role, false, own);
        if (touched && touchedAura)
            c->AddAura(touchedAura, c);

        SpawnedCreature sc;
        sc.Guid = c->GetGUID(); sc.Entry = entry;
        sc.IsElite = role != CreatureRole::Trash; sc.IsBoss = false; sc.IsRare = asRare;
        session->SpawnedCreatures.push_back(sc);
        if (uint32 recentLimit = sAtlasConfig->GetRecentCreatureEntries(); recentLimit > 0)
        {
            session->RecentCreatureEntries.push_back(entry);
            if (session->RecentCreatureEntries.size() > recentLimit)
                session->RecentCreatureEntries.erase(session->RecentCreatureEntries.begin());
        }
        if (asRare)
        {
            ++raresSpawned;
            rareNotes.push_back(Acore::StringFormat("|cFFFF8800{}|r ({})", c->GetName(),
                sAtlasAffixes->Describe(own, false)));
            LOG_INFO("module", "Atlas: Rare {} (entry {}) at ({:.1f}, {:.1f}, {:.1f}): {}", c->GetName(), entry,
                pos.GetPositionX(), pos.GetPositionY(), pos.GetPositionZ(), sAtlasAffixes->Codes(own));
        }
        ++spawnedMobs;
    };

    // Packs, nearest first: a kind per pack, then each member at its spot,
    // then the members the run's affixes add (Teeming, Swarming), up to the
    // live-creature cap. A pack's leader may be a rare.
    for (LayoutAnchor const& anchor : layout->Anchors)
    {
        if (spawnedMobs >= maxCreatures)
            break;

        PackKind const kind = RollPackKind(shares, session->Rng);
        bool const rareLeader = rareChance && session->Rng.Chance(rareChance)
            && raresSpawned < sAtlasConfig->GetMaxRares();

        for (std::size_t i = 0; i < anchor.Spots.size() && spawnedMobs < maxCreatures; ++i)
            spawnMember(kind, &anchor.Spots[i], anchor.Spots[i].Pos, i, rareLeader && i == 0, true);

        bool const extraPack  = fx.PackExtraPct  > 0.0f && session->Rng.Chance(uint32(fx.PackExtraPct));
        bool const extraSwarm = fx.SwarmExtraPct > 0.0f && session->Rng.Chance(uint32(fx.SwarmExtraPct));
        LayoutSpot const* lead = anchor.Spots.empty() ? nullptr : &anchor.Spots.front();
        if (extraPack && spawnedMobs < maxCreatures)
            spawnMember(kind, lead, anchor.Pos, anchor.Spots.size(), false, true);
        if (extraSwarm && spawnedMobs < maxCreatures)
            spawnMember(kind, lead, anchor.Pos, anchor.Spots.size() + 1, false, false);
    }
    session->TotalMobs = spawnedMobs;

    if (!rareNotes.empty())
    {
        std::string notes;
        for (std::string const& n : rareNotes)
            notes += (notes.empty() ? "" : ", ") + n;
        for (auto const& pd : session->Players)
            if (Player* p = ObjectAccessor::FindPlayer(pd.PlayerGuid))
                if (p->GetSession())
                    ChatHandler(p->GetSession()).PSendSysMessage(
                        "{} Something |cFFFF8800rare|r has slipped into this echo from another timeline: {}.",
                        CHAT_PREFIX, notes);
    }

    // Bosses: the arena's own master (native), the same touched by the
    // plague, or a Scourge champion, by the same corruption table.
    uint32 bossesSpawned = 0;
    for (LayoutArena const& arena : layout->Arenas)
    {
        if (bossesSpawned >= sAtlasConfig->GetBossCount())
            break;

        PackKind const kind = RollPackKind(shares, session->Rng);
        uint32 entry = 0;
        if (kind == PackKind::Scourge || theme->Kind == ThemeKind::Pool)
            entry = PickPoolEntry(kind == PackKind::Scourge ? scourgeThemeId : theme->Id, PackRole::Boss,
                session->Rng);
        else if (theme->Kind == ThemeKind::Native && arena.NativeEntry
            && GetCuration(arena.NativeEntry) != CurationStatus::Blocked)
            entry = arena.NativeEntry;
        if (!entry)
            entry = SelectDungeonBoss(theme, session->Rng);
        if (!entry) { LOG_WARN("module", "Atlas: No boss candidate."); continue; }

        bool const touched = kind == PackKind::Touched;
        Creature* b = map->SummonCreature(entry, arena.Centre);
        if (!b) continue;

        prepareHostile(b);
        b->SetCorpseDelay(600);          // 10 min corpse for bosses

        applyLevelAndStats(b, CreatureRole::Boss, true, touched ? touchedAffixes : std::vector<ActiveAffix>());
        if (touched && touchedAura)
            b->AddAura(touchedAura, b);

        SpawnedCreature sc;
        sc.Guid = b->GetGUID(); sc.Entry = entry;
        sc.IsElite = true; sc.IsBoss = true;
        session->SpawnedCreatures.push_back(sc);
        ++bossesSpawned;

        LOG_INFO("module", "Atlas: Boss spawned — entry {}, name '{}', {}, AI: AtlasCreatureAI (auto-attack), "
            "Level: {}", b->GetEntry(), b->GetName(),
            kind == PackKind::Scourge ? "Scourge" : touched ? "Scourge-touched" : "native", b->GetLevel());
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

// ---------------------------------------------------------------------------
// Themes, pools and curation (DESIGN §8, SCOURGE appendix)
// ---------------------------------------------------------------------------

char const* AtlasMgr::CurationName(CurationStatus status)
{
    switch (status)
    {
        case CurationStatus::Allowed: return "allowed";
        case CurationStatus::Blocked: return "blocked";
        default:                      return "untested";
    }
}

AtlasMgr::CurationStatus AtlasMgr::ParseCuration(std::string_view name, bool& ok)
{
    ok = true;
    if (name == "allowed")
        return CurationStatus::Allowed;
    if (name == "blocked")
        return CurationStatus::Blocked;
    if (name != "untested")
        ok = false;
    return CurationStatus::Untested;
}

void AtlasMgr::LoadThemes()
{
    std::vector<Theme> themes;
    if (QueryResult r = WorldDatabase.Query(
            "SELECT id, kind, name, description, sort_order FROM atlas_theme WHERE enabled = 1 ORDER BY id"))
    {
        do
        {
            Field* f = r->Fetch();
            Theme t;
            t.Id = f[0].Get<uint32>();
            std::string const kind = f[1].Get<std::string>();
            t.Kind = kind == "native" ? ThemeKind::Native : ThemeKind::Pool;
            t.Name = f[2].Get<std::string>();
            t.Description = f[3].Get<std::string>();
            t.SortOrder = f[4].Get<int32>();
            if (t.Id >= 100)
            {
                LOG_ERROR("module", "Atlas: atlas_theme {} skipped: theme IDs must stay below 100 (gossip menus).",
                    t.Id);
                continue;
            }
            themes.push_back(std::move(t));
        } while (r->NextRow());
    }
    else
        LOG_ERROR("module", "Atlas: table atlas_theme is empty or missing (module SQL not applied?): "
            "only the config themes are available.");

    std::map<uint32, ThemePool> pools;
    uint32 poolRows = 0;
    uint32 unfit = 0;
    if (QueryResult r = WorldDatabase.Query(
            "SELECT theme_id, entry, pack_role FROM atlas_theme_creature ORDER BY theme_id, entry, pack_role"))
    {
        do
        {
            Field* f = r->Fetch();
            uint32 const themeId = f[0].Get<uint32>();
            uint32 const entry = f[1].Get<uint32>();
            PackRole const role = ParsePackRole(f[2].Get<std::string>());
            if (role == PackRole::Count)
                continue;
            if (!IsFitToFight(entry, false))
            {
                ++unfit;
                continue;
            }
            pools[themeId].ByRole[static_cast<std::size_t>(role)].push_back(entry);
            ++poolRows;
        } while (r->NextRow());
    }

    std::unordered_map<uint32, std::pair<CurationStatus, std::string>> curation;
    if (QueryResult r = WorldDatabase.Query("SELECT entry, status, note FROM atlas_creature_curation"))
    {
        do
        {
            Field* f = r->Fetch();
            bool ok = false;
            CurationStatus const status = ParseCuration(f[1].Get<std::string>(), ok);
            curation[f[0].Get<uint32>()] = { status, f[2].Get<std::string>() };
        } while (r->NextRow());
    }

    std::size_t const themeCount = themes.size();
    std::size_t const curationCount = curation.size();
    {
        std::lock_guard<std::mutex> lock(_themeMutex);
        _themePools = std::move(pools);
        _curation = std::move(curation);
    }
    sAtlasConfig->SetDatabaseThemes(std::move(themes));

    LOG_INFO("module", "Atlas: {} database themes, {} pool entries ({} unfit to spawn, skipped), {} curation rows.",
        themeCount, poolRows, unfit, curationCount);
}

AtlasMgr::CurationStatus AtlasMgr::GetCuration(uint32 entry, std::string* note) const
{
    std::lock_guard<std::mutex> lock(_themeMutex);
    auto it = _curation.find(entry);
    if (it == _curation.end())
        return CurationStatus::Untested;
    if (note)
        *note = it->second.second;
    return it->second.first;
}

void AtlasMgr::SetCuration(uint32 entry, CurationStatus status, std::string const& note)
{
    {
        std::lock_guard<std::mutex> lock(_themeMutex);
        _curation[entry] = { status, note };
    }
    std::string safeNote = note;
    WorldDatabase.EscapeString(safeNote);
    WorldDatabase.Execute("REPLACE INTO atlas_creature_curation (entry, status, note) VALUES ({}, '{}', '{}')",
        entry, CurationName(status), safeNote);
}

std::string AtlasMgr::DescribePoolMembership(uint32 entry) const
{
    static char const* const roles[] = { "leader", "heavy", "melee", "caster", "swarm", "add", "rare", "boss" };
    std::lock_guard<std::mutex> lock(_themeMutex);
    std::string out;
    for (auto const& [themeId, pool] : _themePools)
        for (std::size_t r = 0; r < pool.ByRole.size(); ++r)
            if (std::find(pool.ByRole[r].begin(), pool.ByRole[r].end(), entry) != pool.ByRole[r].end())
                out += (out.empty() ? "" : " ") + std::to_string(themeId) + ":" + roles[r];
    return out;
}

uint32 AtlasMgr::PickPoolEntry(uint32 themeId, PackRole role, RunRng& rng) const
{
    // Fallbacks when the pool has nobody for a role, nearest first.
    static constexpr PackRole fallbacks[] = { PackRole::Melee, PackRole::Caster, PackRole::Heavy, PackRole::Leader,
        PackRole::Swarm, PackRole::Rare, PackRole::Boss };

    std::lock_guard<std::mutex> lock(_themeMutex);
    auto it = _themePools.find(themeId);
    if (it == _themePools.end())
        return 0;

    auto candidates = [&](PackRole r)
    {
        std::vector<uint32> out;
        for (uint32 entry : it->second.ByRole[static_cast<std::size_t>(r)])
        {
            auto cur = _curation.find(entry);
            if (cur == _curation.end() || cur->second.first != CurationStatus::Blocked)
                out.push_back(entry);
        }
        return out;
    };

    std::vector<uint32> list = candidates(role);
    for (PackRole r : fallbacks)
    {
        if (!list.empty())
            break;
        if (r != role)
            list = candidates(r);
    }
    return list.empty() ? 0 : rng.Pick(list);
}

bool AtlasMgr::KeepsStockAI(Creature const* creature)
{
    if (!creature || !sAtlasConfig->KeepsSmartAI())
        return false;
    CreatureTemplate const* t = creature->GetCreatureTemplate();
    return t && t->AIName == "SmartAI" && !t->ScriptID;
}

Creature* AtlasMgr::SpawnCurationSample(Player* gm, uint32 entry, CreatureRole role)
{
    if (!gm || !gm->GetMap())
        return nullptr;

    ScalingContext ctx;
    ctx.Level      = gm->GetLevel();
    ctx.PartySize  = 5;
    ctx.InstanceId = gm->GetMap()->GetInstanceId();
    return SpawnRunAdd(gm->GetMap(), entry, gm->GetNearPosition(5.0f, 0.0f), ctx, role, 1.0f, nullptr);
}

Creature* AtlasMgr::SpawnRunAdd(Map* map, uint32 entry, Position const& pos, ScalingContext const& ctx,
    CreatureRole role, float visualScale, Unit* target)
{
    if (!map || !entry)
        return nullptr;

    OwnSpawnScope const own;
    TempSummon* add = map->SummonCreature(entry, pos);
    if (!add)
        return nullptr;

    add->RemoveAllAuras();
    add->SetFaction(14);
    add->SetReactState(REACT_AGGRESSIVE);
    float const typeScale = sAtlasConfig->GetVisualScaleForCreatureType(add->GetCreatureTemplate()->type);
    add->SetObjectScale(std::clamp(add->GetObjectScale() * typeScale * visualScale, 0.4f, 1.0f));
    add->RemoveFlag(UNIT_FIELD_FLAGS, UNIT_FLAG_NON_ATTACKABLE | UNIT_FLAG_IMMUNE_TO_PC
                                    | UNIT_FLAG_IMMUNE_TO_NPC | UNIT_FLAG_PACIFIED
                                    | UNIT_FLAG_STUNNED | UNIT_FLAG_FLEEING
                                    | UNIT_FLAG_NOT_SELECTABLE);
    add->SetUInt32Value(UNIT_FIELD_FLAGS_2, 0);
    add->SetImmuneToPC(false);
    add->SetImmuneToNPC(false);
    add->setActive(true);

    ScaleCreature(add, ctx, role);

    for (uint32 mech = 1; mech < MAX_MECHANIC; ++mech)
        add->ApplySpellImmune(0, IMMUNITY_MECHANIC, mech, false);
    add->ApplySpellImmune(0, IMMUNITY_SCHOOL, SPELL_SCHOOL_MASK_ALL, false);

    // Same AI policy as every run creature (see PopulateDungeon for why the
    // old events are cancelled first).
    if (!KeepsStockAI(add))
    {
        add->m_Events.KillAllEvents(false);
        CreatureAI* ai = new AtlasCreatureAI(add);
        if (!add->AIM_Initialize(ai))
            delete ai;
    }

    add->UpdateObjectVisibility(true);
    if (target && target->IsAlive() && add->AI())
        add->AI()->AttackStart(target);
    return add;
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
