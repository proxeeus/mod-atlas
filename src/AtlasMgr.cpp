/*
 * mod-atlas — AtlasMgr.cpp
 * Central session manager: creature pools, spawning, scaling, rewards, update loop.
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

// RNG: AC's helpers (Random.h), thread-safe. RandInt keeps the call sites'
// integer types; every range used here fits in uint32.
template<typename T>
static T RandInt(T lo, T hi)
{
    return static_cast<T>(urand(static_cast<uint32>(lo), static_cast<uint32>(hi)));
}

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

// Session helper implementations (declared in AtlasTypes.h)
uint32 Session::GetAlivePlayerCount() const
{
    uint32 n = 0;
    for (auto const& pd : Players)
    {
        Player* p = ObjectAccessor::FindPlayer(pd.PlayerGuid);
        if (p && p->IsAlive()) ++n;
    }
    return n;
}

bool Session::IsPartyWiped()    const { return GetAlivePlayerCount() == 0; }

bool Session::IsGroupInCombat() const
{
    for (auto const& pd : Players)
    {
        Player* p = ObjectAccessor::FindPlayer(pd.PlayerGuid);
        if (p && p->IsAlive() && p->IsInCombat())
            return true;
    }
    return false;
}

static std::atomic<uint32> g_dmActiveSessionCount{0};

bool AtlasMgr::HasActiveSessions()
{
    return g_dmActiveSessionCount.load(std::memory_order_relaxed) != 0;
}

// Singleton
AtlasMgr::AtlasMgr()  = default;
AtlasMgr::~AtlasMgr() = default;

AtlasMgr* AtlasMgr::Instance()
{
    static AtlasMgr inst;
    return &inst;
}

// Initialization
void AtlasMgr::Initialize()
{
    LOG_INFO("module", "Atlas: Initializing...");
    LoadFromDB();
    LOG_INFO("module", "Atlas: Ready — {} creature types, {} bosses, {} dungeon bosses, {} reward items, {} loot items.",
        _creaturesByType.size(), _bossCreatures.size(), _dungeonBossPool.size(), _rewardItems.size(), _lootPool.size());
}

void AtlasMgr::LoadFromDB()
{
    LoadCreaturePools();
    LoadDungeonBossPool();
    LoadClassLevelStats();
    LoadRewardItems();
    LoadLootPool();
    LoadAllPlayerStats();
    LoadLeaderboard();
    LoadDungeonGeometry();
}

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
        "ORDER BY ct.type, ct.minlevel");

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
        "ORDER BY ct.type, ct.minlevel",
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

// Cache equippable reward items (green/blue/purple)
void AtlasMgr::LoadRewardItems()
{
    _rewardItems.clear();

    QueryResult result = WorldDatabase.Query(
        "SELECT entry, RequiredLevel, Quality, InventoryType, class, subclass, "
        "AllowableClass, ItemLevel "
        "FROM item_template "
        "WHERE Quality >= 2 AND Quality <= 4 "
        "AND RequiredLevel > 0 AND RequiredLevel <= 80 "
        "AND InventoryType > 0 AND InventoryType <= 26 "
        "AND InventoryType NOT IN (18, 19, 24) "
        "AND class IN (2, 4) AND (Flags & 0x8) = 0 "
        "AND AllowableClass != 0 "
        "AND RequiredReputationFaction = 0 "
        "AND RequiredHonorRank = 0 "
        "AND name NOT LIKE '%Test%' "
        "AND name NOT LIKE '%Deprecated%' "
        "AND name NOT LIKE '%[PH]%' "
        "AND name NOT LIKE '%OLD%' "
        "AND name NOT LIKE '%Monster -%' "
        "AND name NOT LIKE '%zzOLD%' "
        "ORDER BY RequiredLevel, Quality");

    if (result)
    {
        do
        {
            Field* f = result->Fetch();
            RewardItem ri;
            ri.Entry         = f[0].Get<uint32>();
            ri.MinLevel      = f[1].Get<uint8>();
            ri.MaxLevel      = ri.MinLevel + 5;
            ri.Quality       = f[2].Get<uint8>();
            ri.InventoryType = f[3].Get<uint32>();
            ri.Class         = f[4].Get<uint32>();
            ri.SubClass      = f[5].Get<uint32>();
            ri.AllowableClass = f[6].Get<int32>();
            ri.ItemLevel     = f[7].Get<uint16>();
            _rewardItems.push_back(ri);
        } while (result->NextRow());
    }

    LOG_INFO("module", "Atlas: {} reward items cached.", _rewardItems.size());
}

// Cache items for mob loot drops
void AtlasMgr::LoadLootPool()
{
    _lootPool.clear();

    // Grey junk, white consumables, green/blue/purple equipment
    QueryResult result = WorldDatabase.Query(
        "SELECT entry, RequiredLevel, Quality, class, subclass, AllowableClass, ItemLevel "
        "FROM item_template "
        "WHERE Quality <= 4 "
        "AND ItemLevel <= 300 "
        "AND SellPrice > 0 "
        "AND class IN (0, 2, 4, 7, 15) "
        "AND (Flags & 0x8) = 0 "
        "AND AllowableClass != 0 "
        "AND RequiredReputationFaction = 0 "
        "AND RequiredHonorRank = 0 "
        "AND (RequiredLevel > 0 OR class NOT IN (2, 4)) "               // equipment must have a required level
        "AND name NOT LIKE '%Test%' "
        "AND name NOT LIKE '%Deprecated%' "
        "AND name NOT LIKE '%[PH]%' "
        "AND name NOT LIKE '%OLD%' "
        "AND name NOT LIKE '%Monster -%' "
        "AND name NOT LIKE '%zzOLD%' "
        "AND name NOT LIKE '%Debug%' "
        "ORDER BY RequiredLevel, Quality");

    if (result)
    {
        do
        {
            Field* f = result->Fetch();
            LootPoolItem li;
            li.Entry          = f[0].Get<uint32>();
            li.MinLevel       = f[1].Get<uint8>();
            li.Quality        = f[2].Get<uint8>();
            li.ItemClass      = f[3].Get<uint8>();
            li.SubClass       = f[4].Get<uint8>();
            li.AllowableClass = f[5].Get<int32>();
            li.ItemLevel      = f[6].Get<uint16>();
            _lootPool.push_back(li);
        } while (result->NextRow());
    }

    uint32 counts[5] = {};
    for (auto const& li : _lootPool)
        if (li.Quality <= 4) ++counts[li.Quality];

    LOG_INFO("module", "Atlas: {} mob loot items cached "
        "(grey={}, white={}, green={}, blue={}, epic={}).",
        _lootPool.size(), counts[0], counts[1], counts[2], counts[3], counts[4]);
}

// Compute group average level
uint8 AtlasMgr::ComputeEffectiveLevel(Player* leader) const
{
    if (!leader)
        return 1;

    Group* group = leader->GetGroup();
    if (!group)
        return leader->GetLevel();

    uint32 totalLevel = 0;
    uint32 count      = 0;

    for (GroupReference* ref = group->GetFirstMember(); ref; ref = ref->next())
    {
        Player* m = ref->GetSource();
        if (m && m->IsInWorld())
        {
            totalLevel += m->GetLevel();
            ++count;
        }
    }

    return count > 0
        ? static_cast<uint8>(totalLevel / count)
        : leader->GetLevel();
}

// SESSION LIFECYCLE

Session* AtlasMgr::CreateSession(Player* leader, uint32 difficultyId,
                                          uint32 themeId, uint32 mapId,
                                          bool scaleToParty)
{
    DifficultyTier const* diff  = sAtlasConfig->GetDifficulty(difficultyId);
    Theme const*          theme = sAtlasConfig->GetTheme(themeId);
    DungeonInfo const*    dg    = sAtlasConfig->GetDungeon(mapId);
    if (!diff || !theme || !dg)
        return nullptr;

    std::lock_guard<std::mutex> lock(_sessionMutex);

    // Check capacity under the lock to avoid race conditions
    if (!CanCreateNewSession())
        return nullptr;

    Session s;
    s.SessionId    = _nextSessionId++;
    s.LeaderGuid   = leader->GetGUID();
    s.State        = SessionState::Preparing;
    s.DifficultyId = difficultyId;
    s.ThemeId      = themeId;
    s.MapId        = mapId;
    s.ScaleToParty = scaleToParty;
    s.StartTime    = GameTime::GetGameTime().count();

    if (sAtlasConfig->IsTimeLimitEnabled())
        s.TimeLimit = sAtlasConfig->GetTimeLimitMinutes() * 60;

    if (scaleToParty)
    {
        // Scale to party: creatures match the player/group level,
        // clamped to the difficulty tier's range.
        s.EffectiveLevel = ComputeEffectiveLevel(leader);

        uint8 band = sAtlasConfig->GetLevelBand();
        s.LevelBandMin = (s.EffectiveLevel > band) ? (s.EffectiveLevel - band) : 1;
        s.LevelBandMax = std::min<uint8>(s.EffectiveLevel + band, 83);

        // Clamp to tier so the correct creature templates are selected
        s.LevelBandMin = std::max(s.LevelBandMin, diff->MinLevel);
        s.LevelBandMax = std::min(s.LevelBandMax, diff->MaxLevel);
    }
    else
    {
        // Use tier's natural level range — no party scaling.
        // EffectiveLevel = midpoint of the tier; band = full tier range.
        s.EffectiveLevel = static_cast<uint8>((uint16(diff->MinLevel) + uint16(diff->MaxLevel)) / 2);
        s.LevelBandMin   = diff->MinLevel;
        s.LevelBandMax   = diff->MaxLevel;
    }

    // Ensure min <= max after clamping (edge case: player level far outside tier)
    if (s.LevelBandMin > s.LevelBandMax)
        s.LevelBandMin = s.LevelBandMax;

    PlayerSessionData ld;
    ld.PlayerGuid  = leader->GetGUID();
    ld.ReturnMapId = leader->GetMapId();
    ld.ReturnPosition = { leader->GetPositionX(), leader->GetPositionY(),
                          leader->GetPositionZ(), leader->GetOrientation() };
    s.Players.push_back(ld);

    if (Group* g = leader->GetGroup())
    {
        for (GroupReference* ref = g->GetFirstMember(); ref; ref = ref->next())
        {
            Player* m = ref->GetSource();
            if (m && m != leader && m->IsInWorld())
            {
                PlayerSessionData md;
                md.PlayerGuid  = m->GetGUID();
                md.ReturnMapId = m->GetMapId();
                md.ReturnPosition = { m->GetPositionX(), m->GetPositionY(),
                                      m->GetPositionZ(), m->GetOrientation() };
                s.Players.push_back(md);
            }
        }
    }

    _activeSessions[s.SessionId] = s;
    g_dmActiveSessionCount.store(static_cast<uint32>(_activeSessions.size()), std::memory_order_relaxed);
    for (auto const& pd : s.Players)
        _playerToSession[pd.PlayerGuid] = s.SessionId;

    LOG_INFO("module", "Atlas: Session {} — leader {}, party {}, diff {}, level band {}-{}, scale={}",
        s.SessionId, leader->GetName(), s.Players.size(),
        diff->Name, s.LevelBandMin, s.LevelBandMax, scaleToParty ? "party" : "tier");

    return &_activeSessions[s.SessionId];
}

Session* AtlasMgr::GetSession(uint32 id)
{
    std::lock_guard<std::mutex> lock(_sessionMutex);
    auto it = _activeSessions.find(id);
    return it != _activeSessions.end() ? &it->second : nullptr;
}

Session* AtlasMgr::GetSessionByInstance(uint32 instId)
{
    std::lock_guard<std::mutex> lock(_sessionMutex);
    auto it = _instanceToSession.find(instId);
    if (it != _instanceToSession.end())
    {
        auto sit = _activeSessions.find(it->second);
        return sit != _activeSessions.end() ? &sit->second : nullptr;
    }
    return nullptr;
}

Session* AtlasMgr::GetSessionByPlayer(ObjectGuid guid)
{
    std::lock_guard<std::mutex> lock(_sessionMutex);
    auto it = _playerToSession.find(guid);
    if (it != _playerToSession.end())
    {
        auto sit = _activeSessions.find(it->second);
        return sit != _activeSessions.end() ? &sit->second : nullptr;
    }
    return nullptr;
}

// StartDungeon / TeleportPartyIn / TeleportPartyOut

// Every account this session can bind: the players, plus the group leader
// (in a group, a new instance is chosen from the leader's bind).
static std::set<ObjectGuid> CollectBindOwners(Session const* session)
{
    std::set<ObjectGuid> guids;
    for (auto const& pd : session->Players)
        guids.insert(pd.PlayerGuid);

    if (Player* leader = ObjectAccessor::FindPlayer(session->LeaderGuid))
        if (Group* g = leader->GetGroup())
            guids.insert(g->GetLeaderGUID());

    return guids;
}

static void NotifySession(Session const* session, std::string const& msg)
{
    for (auto const& pd : session->Players)
        if (Player* p = ObjectAccessor::FindPlayer(pd.PlayerGuid))
            if (p->GetSession())
                ChatHandler(p->GetSession()).SendSysMessage(msg);
}

// Before a run: make sure the party lands in a fresh instance of the stock
// dungeon instead of gutting one they (or their leader) are saved to.
// Temporary Normal binds are released like a manual "Reset all instances";
// a bind to an instance someone is still inside, or a permanent bind, blocks
// the run instead.
bool AtlasMgr::ReleaseBindsBeforeRun(Session* session)
{
    std::set<ObjectGuid> const owners = CollectBindOwners(session);

    for (ObjectGuid const& guid : owners)
    {
        InstancePlayerBind* bind = sInstanceSaveMgr->PlayerGetBoundInstance(
            guid, session->MapId, DUNGEON_DIFFICULTY_NORMAL);
        if (!bind || !bind->save)
            continue;

        Map* inUse = sMapMgr->FindMap(session->MapId, bind->save->GetInstanceId());
        if (bind->perm || (inUse && inUse->HavePlayers()))
        {
            NotifySession(session, Acore::StringFormat(
                "{} Someone in your group is still saved to this dungeon, and its instance is in use. "
                "Leave it and reset your instances first.", CHAT_PREFIX));
            return false;
        }
    }

    for (ObjectGuid const& guid : owners)
        if (sInstanceSaveMgr->PlayerGetBoundInstance(guid, session->MapId, DUNGEON_DIFFICULTY_NORMAL))
            sInstanceSaveMgr->PlayerUnbindInstance(guid, session->MapId, DUNGEON_DIFFICULTY_NORMAL,
                true, ObjectAccessor::FindConnectedPlayer(guid));

    return true;
}

// Startup: release instances that runs left open because the server stopped
// mid-run (sessions are in memory only). No instance map is loaded yet, so AC
// deletes each instance's saved state as soon as its last bind is removed.
void AtlasMgr::ReleaseInstancesLeftOpen()
{
    QueryResult result = CharacterDatabase.Query("SELECT instance_id FROM atlas_open_instance");
    if (!result)
        return;

    uint32 released = 0;
    do
    {
        if (InstanceSave* save = sInstanceSaveMgr->GetInstanceSave(result->Fetch()[0].Get<uint32>()))
        {
            sInstanceSaveMgr->UnbindAllFor(save);
            ++released;
        }
    } while (result->NextRow());

    CharacterDatabase.Execute("DELETE FROM atlas_open_instance");

    if (released)
        LOG_INFO("module", "Atlas: Released {} dungeon instance(s) left open by runs before the last shutdown.",
            released);
}

// After a run: drop every bind to the instance the run used. Once the instance
// map unloads, AC then deletes its saved state (instance row, encounter data,
// creature/gameobject respawn rows), so the gutted instance is never reused.
void AtlasMgr::ReleaseRunInstanceBinds(Session const& session)
{
    if (!session.InstanceId)
        return;

    CharacterDatabase.Execute("DELETE FROM atlas_open_instance WHERE instance_id = {}", session.InstanceId);

    for (ObjectGuid const& guid : CollectBindOwners(&session))
    {
        InstancePlayerBind* bind = sInstanceSaveMgr->PlayerGetBoundInstance(
            guid, session.MapId, DUNGEON_DIFFICULTY_NORMAL);
        if (bind && bind->save && bind->save->GetInstanceId() == session.InstanceId)
            sInstanceSaveMgr->PlayerUnbindInstance(guid, session.MapId, DUNGEON_DIFFICULTY_NORMAL,
                true, ObjectAccessor::FindConnectedPlayer(guid));
    }
}

bool AtlasMgr::StartDungeon(Session* session)
{
    if (!session) return false;

    // Normal difficulty only. In a heroic instance, killing a summoned stock
    // boss that carries CREATURE_FLAG_EXTRA_INSTANCE_BIND perm-binds the whole
    // group (Unit::Kill), i.e. a real heroic lockout from a challenge run.
    for (auto const& pd : session->Players)
    {
        Player* p = ObjectAccessor::FindPlayer(pd.PlayerGuid);
        if (p && p->GetDungeonDifficulty() != DUNGEON_DIFFICULTY_NORMAL)
        {
            NotifySession(session, Acore::StringFormat(
                "{} An echo cannot hold a heroic timeline. Set the dungeon difficulty to Normal "
                "and speak to me again.", CHAT_PREFIX));
            return false;
        }
    }

    if (!ReleaseBindsBeforeRun(session))
        return false;

    session->EntrancePos = GetDungeonEntrance(session->MapId);
    if (session->EntrancePos.GetPositionX() == 0 &&
        session->EntrancePos.GetPositionY() == 0 &&
        session->EntrancePos.GetPositionZ() == 0)
    {
        LOG_ERROR("module", "Atlas: No entrance coords for map {}", session->MapId);
        return false;
    }
    return true;
}

bool AtlasMgr::TeleportPartyIn(Session* session)
{
    if (!session) return false;
    DungeonInfo const* dg = sAtlasConfig->GetDungeon(session->MapId);
    if (!dg) return false;

    Position ent = session->EntrancePos;
    uint32 ok = 0;

    for (auto& pd : session->Players)
    {
        Player* p = ObjectAccessor::FindPlayer(pd.PlayerGuid);
        if (!p) continue;

        pd.ReturnMapId    = p->GetMapId();
        pd.ReturnPosition = { p->GetPositionX(), p->GetPositionY(),
                              p->GetPositionZ(), p->GetOrientation() };

        // Resurrect dead players so they can teleport (roguelike floor transitions, etc.)
        if (!p->IsAlive())
        {
            p->RemoveFlag(PLAYER_FIELD_BYTES, PLAYER_FIELD_BYTE_NO_RELEASE_WINDOW);
            p->ResurrectPlayer(1.0f);
            p->SpawnCorpseBones();
        }

        if (p->TeleportTo(session->MapId, ent.GetPositionX(), ent.GetPositionY(),
                          ent.GetPositionZ(), ent.GetOrientation()))
        {
            ++ok;
            LOG_INFO("module", "Atlas: TeleportTo queued for {} → map {} ({:.1f}, {:.1f}, {:.1f})",
                p->GetName(), session->MapId, ent.GetPositionX(), ent.GetPositionY(), ent.GetPositionZ());
            ChatHandler(p->GetSession()).PSendSysMessage(
                "{} You step into an echo of |cFFFFFFFF{}|r. Defeat its master to mend it.",
                CHAT_PREFIX, dg->Name);

            if (session->RoguelikeRunId != 0 && sRoguelikeMgr->HasActiveAffixes(session->RoguelikeRunId))
                ChatHandler(p->GetSession()).PSendSysMessage("{} This echo is unstable: {}",
                    CHAT_PREFIX_ENDLESS, sRoguelikeMgr->GetActiveAffixNames(session->RoguelikeRunId));
        }
        else
        {
            LOG_ERROR("module", "Atlas: TeleportTo FAILED for {} → map {} ({:.1f}, {:.1f}, {:.1f})",
                p->GetName(), session->MapId, ent.GetPositionX(), ent.GetPositionY(), ent.GetPositionZ());
            ChatHandler(p->GetSession()).PSendSysMessage(
                "{} The timeways would not carry you. You may lack access to this dungeon.", CHAT_PREFIX);
        }
    }

    if (ok > 0)
    {
        session->State = SessionState::InProgress;
        // InstanceId is set when a player actually arrives on the map
        // (via the allmap script or the Update tick populate logic).
        return true;
    }
    return false;
}

void AtlasMgr::TeleportPartyOut(Session* session)
{
    if (!session) return;
    for (auto const& pd : session->Players)
    {
        Player* p = ObjectAccessor::FindPlayer(pd.PlayerGuid);
        if (!p || !p->IsInWorld()) continue;
        p->RemoveFlag(PLAYER_FIELD_BYTES, PLAYER_FIELD_BYTE_NO_RELEASE_WINDOW);
        if (!p->IsAlive()) { p->ResurrectPlayer(1.0f); p->SpawnCorpseBones(); }
        p->TeleportTo(pd.ReturnMapId, pd.ReturnPosition.GetPositionX(),
                      pd.ReturnPosition.GetPositionY(), pd.ReturnPosition.GetPositionZ(),
                      pd.ReturnPosition.GetOrientation());
    }
}

// Entrances and spawn points come from world DB tables that don't change while
// the server runs: load them once (startup, `.atlas reload`) instead of querying
// the world DB every time a run starts.
void AtlasMgr::LoadDungeonGeometry()
{
    std::unordered_map<uint32, Position> entrances;
    std::unordered_map<uint32, std::vector<SpawnPoint>> spawnPoints;

    for (auto const& dg : sAtlasConfig->GetDungeons())
    {
        entrances[dg.MapId]   = QueryDungeonEntrance(dg.MapId);
        spawnPoints[dg.MapId] = BuildSpawnPointsForMap(dg.MapId);
    }

    std::lock_guard<std::mutex> lock(_geometryMutex);
    _entrances   = std::move(entrances);
    _spawnPoints = std::move(spawnPoints);

    LOG_INFO("module", "Atlas: Cached entrances and spawn points for {} dungeons.", _entrances.size());
}

Position AtlasMgr::GetDungeonEntrance(uint32 mapId)
{
    std::lock_guard<std::mutex> lock(_geometryMutex);
    auto itr = _entrances.find(mapId);
    return itr != _entrances.end() ? itr->second : Position(0.0f, 0.0f, 0.0f, 0.0f);
}

std::vector<SpawnPoint> AtlasMgr::GetSpawnPointsForMap(uint32 mapId)
{
    std::lock_guard<std::mutex> lock(_geometryMutex);
    auto itr = _spawnPoints.find(mapId);
    return itr != _spawnPoints.end() ? itr->second : std::vector<SpawnPoint>();
}

// Dungeon entrance lookup from areatrigger_teleport
Position AtlasMgr::QueryDungeonEntrance(uint32 mapId)
{
    if (QueryResult r = WorldDatabase.Query(
            "SELECT target_position_x, target_position_y, target_position_z, target_orientation "
            "FROM areatrigger_teleport WHERE target_map = {} LIMIT 1", mapId))
    {
        Field* f = r->Fetch();
        return { f[0].Get<float>(), f[1].Get<float>(), f[2].Get<float>(), f[3].Get<float>() };
    }
    LOG_WARN("module", "Atlas: No areatrigger_teleport for map {}", mapId);
    return { 0, 0, 0, 0 };
}

// Spawn-point collection
std::vector<SpawnPoint> AtlasMgr::BuildSpawnPointsForMap(uint32 mapId)
{
    std::vector<SpawnPoint> pts;

    QueryResult result = WorldDatabase.Query(
        "SELECT position_x, position_y, position_z, orientation FROM creature WHERE map = {}", mapId);
    if (!result) return pts;

    Position ent = QueryDungeonEntrance(mapId);
    float ex = ent.GetPositionX(), ey = ent.GetPositionY(), ez = ent.GetPositionZ();

    do
    {
        Field* f = result->Fetch();
        float x = f[0].Get<float>(), y = f[1].Get<float>(),
              z = f[2].Get<float>(), o = f[3].Get<float>();

        SpawnPoint sp;
        sp.Pos.Relocate(x, y, z, o);
        float dx = x - ex, dy = y - ey, dz = z - ez;
        sp.DistanceFromEntrance = std::sqrt(dx*dx + dy*dy + dz*dz);
        pts.push_back(sp);
    } while (result->NextRow());

    // Sort near → far
    std::sort(pts.begin(), pts.end(),
        [](SpawnPoint const& a, SpawnPoint const& b)
        { return a.DistanceFromEntrance < b.DistanceFromEntrance; });

    // Find boss positions from creature data
    bool bossFound = false;

    std::string bossQuery = BuildBossSpawnPointQuery(mapId);
    QueryResult bossResult = WorldDatabase.Query(bossQuery.c_str());

    if (bossResult)
    {
        // Collect all boss candidates, pick the farthest from entrance
        struct BossCandidate {
            float x, y, z, o;
            float dist;
            uint64 mechanicsMask;
            uint32 entry;
        };
        std::vector<BossCandidate> bosses;

        do
        {
            Field* f = bossResult->Fetch();
            BossCandidate bc;
            bc.x = f[0].Get<float>();
            bc.y = f[1].Get<float>();
            bc.z = f[2].Get<float>();
            bc.o = f[3].Get<float>();
            bc.mechanicsMask = f[4].Get<uint64>();
            bc.entry = f[6].Get<uint32>();

            float dx = bc.x - ex, dy = bc.y - ey, dz = bc.z - ez;
            bc.dist = std::sqrt(dx*dx + dy*dy + dz*dz);
            bosses.push_back(bc);
        } while (bossResult->NextRow());

        if (!bosses.empty())
        {
            // The "last boss" is the farthest boss-type creature from the entrance.
            std::sort(bosses.begin(), bosses.end(),
                [](BossCandidate const& a, BossCandidate const& b)
                { return a.dist > b.dist; });

            BossCandidate const& lastBoss = bosses[0];

            LOG_INFO("module", "Atlas: Map {} — found {} boss candidate(s). "
                "Last boss spawn entry {} at ({:.1f}, {:.1f}, {:.1f}), MechanicsMask={}, dist={:.1f}",
                mapId, bosses.size(), lastBoss.entry,
                lastBoss.x, lastBoss.y, lastBoss.z,
                lastBoss.mechanicsMask, lastBoss.dist);

            // Create boss spawn point(s) at the actual boss location(s).
            uint32 bc = sAtlasConfig->GetBossCount();
            for (uint32 i = 0; i < bc && i < bosses.size(); ++i)
            {
                SpawnPoint bsp;
                bsp.Pos.Relocate(bosses[i].x, bosses[i].y, bosses[i].z, bosses[i].o);
                bsp.DistanceFromEntrance = bosses[i].dist;
                bsp.IsBossPosition = true;
                pts.push_back(bsp);
            }
            bossFound = true;
        }
    }

    // Fallback: if no actual boss found in DB, use farthest spawn point(s)
    if (!bossFound)
    {
        LOG_WARN("module", "Atlas: Map {} — no boss creatures found in DB, "
            "falling back to farthest spawn points.", mapId);

        // Re-sort since we may have added boss points
        std::sort(pts.begin(), pts.end(),
            [](SpawnPoint const& a, SpawnPoint const& b)
            { return a.DistanceFromEntrance < b.DistanceFromEntrance; });

        uint32 bc = sAtlasConfig->GetBossCount();
        for (uint32 i = 0; i < bc && i < pts.size(); ++i)
            pts[pts.size() - 1 - i].IsBossPosition = true;
    }

    return pts;
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
    CharacterDatabase.Execute("REPLACE INTO atlas_open_instance (instance_id, map_id) VALUES ({}, {})",
        map->GetInstanceId(), map->GetId());

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

    session->SpawnPoints = GetSpawnPointsForMap(session->MapId);
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

        uint32 entry = SelectCreatureForTheme(theme, false, session);
        if (!entry) continue;

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

        bool isElite = (RandInt<uint32>(1, 100) <= sAtlasConfig->GetEliteChance());

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
                isElite = (RandInt<uint32>(1, 100) <= boostedChance);
            }
        }

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
        RandInt<uint32>(1, 100) <= sAtlasConfig->GetRareSpawnChance())
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
            size_t pickIdx  = validRarePoints[RandInt<size_t>(startIdx, endIdx)];
            SpawnPoint& rareSP = session->SpawnPoints[pickIdx];

            uint32 rareEntry = SelectCreatureForTheme(theme, true);
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

        uint32 entry = SelectDungeonBoss(theme);
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
uint32 AtlasMgr::SelectCreatureForTheme(Theme const* theme, bool isBoss, Session const* session)
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
        return candidates[RandInt<size_t>(0, candidates.size() - 1)];
    }

    LOG_ERROR("module", "Atlas: ZERO candidates for theme '{}' (boss={})",
        theme->Name, isBoss);
    return 0;
}

uint32 AtlasMgr::SelectDungeonBoss(Theme const* theme)
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
        return SelectCreatureForTheme(theme, true);
    }

    uint32 entry = candidates[RandInt<size_t>(0, candidates.size() - 1)];
    LOG_DEBUG("module", "Atlas: Selected dungeon boss entry {} from {} candidates (theme '{}')",
        entry, candidates.size(), theme->Name);
    return entry;
}

// Death handling
void AtlasMgr::HandleCreatureDeath(Creature* creature, Session* session)
{
    if (!creature || !session || !session->IsActive())
        return;

    LOG_DEBUG("module", "Atlas: HandleCreatureDeath called for {} (GUID: {}) in session {}",
        creature->GetName(), creature->GetGUID().GetCounter(), session->SessionId);

    for (auto& sc : session->SpawnedCreatures)
    {
        if (sc.Guid == creature->GetGUID())
        {
            // Mark dead if not already (boss-AI path via OnUnitDeath may arrive
            // here first when creatures don't use our custom AI).
            if (!sc.IsDead)
                sc.IsDead = true;

            LOG_DEBUG("module",
                "Atlas: Processing death for {} (Boss: {}, Elite: {}, LootFilled: {}, KillCredited: {})",
                creature->GetName(), sc.IsBoss, sc.IsElite, sc.LootFilled, sc.KillCredited);

            // ---- Loot: always fill here (OnUnitDeath fires AFTER core death processing) ----
            if (!sc.LootFilled)
            {
                sc.LootFilled = true;
                FillCreatureLoot(creature, session, sc.IsBoss);
            }

            // ---- Kill credit: only once ----
            if (!sc.KillCredited)
            {
                sc.KillCredited = true;
                GiveKillXP(session, sc.IsBoss, sc.IsElite);

                if (sc.IsBoss)
                {
                    PendingPhaseCheck ppc;
                    ppc.DeathPos   = { creature->GetPositionX(), creature->GetPositionY(),
                                       creature->GetPositionZ(), creature->GetOrientation() };
                    ppc.DeathTime  = GameTime::GetGameTime().count();
                    ppc.OrigEntry  = creature->GetEntry();
                    ppc.Resolved   = false;
                    session->PendingPhaseChecks.push_back(ppc);

                    LOG_DEBUG("module", "Atlas: Boss '{}' died — deferring kill count for phase check",
                        creature->GetName());
                }
                else
                {
                    ++session->MobsKilled;
                    for (auto& pd : session->Players)
                        ++pd.MobsKilled;
                }
            }
            break;
        }
    }

    // Completion is now handled by the phase check system in Update()
}

void AtlasMgr::HandleBossDeath(Session* session)
{
    if (!session) return;
    for (auto const& pd : session->Players)
        if (Player* p = ObjectAccessor::FindPlayer(pd.PlayerGuid))
        {
            if (session->BossesKilled < session->TotalBosses && p->GetSession())
            {
                ChatHandler(p->GetSession()).PSendSysMessage(
                    "{} A master of this echo has fallen. |cFFFFFFFF{}|r remaining.",
                    CHAT_PREFIX, session->TotalBosses - session->BossesKilled);
            }
        }
}

    // Called from JustDied hook — fills loot before corpse is opened
void AtlasMgr::OnCreatureDeathHook(Creature* creature)
{
    if (!creature) return;

    LOG_DEBUG("module", "Atlas: OnCreatureDeathHook called for {} (GUID: {})",
        creature->GetName(), creature->GetGUID().GetCounter());

    std::lock_guard<std::mutex> lock(_sessionMutex);

    for (auto& [sid, session] : _activeSessions)
    {
        if (!session.IsActive())
            continue;
        if (creature->GetMapId() != session.MapId)
            continue;

        for (auto& sc : session.SpawnedCreatures)
        {
            if (sc.Guid == creature->GetGUID())
            {
                if (sc.IsDead)
                {
                    LOG_WARN("module", "Atlas: OnCreatureDeathHook - creature {} already marked as dead",
                        creature->GetGUID().GetCounter());
                    return;
                }

                sc.IsDead = true;
                LOG_DEBUG("module", "Atlas: OnCreatureDeathHook processing death for {} (Boss: {}, Elite: {})",
                    creature->GetName(), sc.IsBoss, sc.IsElite);

                // ----------------------------------------------------------
                // IMPORTANT: Do NOT call FillCreatureLoot here!
                // This hook fires from JustDied, which runs INSIDE
                // Creature::setDeathState / Unit::Kill.  After JustDied
                // returns, the core clears creature->loot and removes
                // UNIT_DYNFLAG_LOOTABLE for creatures with no template loot
                // table, wiping everything we added.
                //
                // Loot is filled in HandleCreatureDeath (OnUnitDeath hook)
                // which fires AFTER the core's death processing completes.
                // ----------------------------------------------------------

                // Credit kill XP now (safe — doesn't depend on loot timing)
                if (!sc.KillCredited)
                {
                    sc.KillCredited = true;
                    GiveKillXP(&session, sc.IsBoss, sc.IsElite);

                    if (sc.IsBoss)
                    {
                        PendingPhaseCheck ppc;
                        ppc.DeathPos   = { creature->GetPositionX(), creature->GetPositionY(),
                                           creature->GetPositionZ(), creature->GetOrientation() };
                        ppc.DeathTime  = GameTime::GetGameTime().count();
                        ppc.OrigEntry  = creature->GetEntry();
                        ppc.Resolved   = false;
                        session.PendingPhaseChecks.push_back(ppc);

                        LOG_DEBUG("module", "Atlas: Boss '{}' died — deferring kill count for phase check (entry {})",
                            creature->GetName(), creature->GetEntry());
                    }
                    else
                    {
                        ++session.MobsKilled;
                        for (auto& pd : session.Players)
                            ++pd.MobsKilled;
                    }
                }

                LOG_DEBUG("module", "Atlas: Creature {} (entry {}) death handled via hook "
                    "(session {}, boss={}).  Loot deferred to OnUnitDeath.",
                    creature->GetGUID().ToString(), creature->GetEntry(),
                    sid, sc.IsBoss);
                return;
            }
        }
    }
}

void AtlasMgr::HandlePlayerDeath(Player* player, Session* session)
{
    if (!player || !session) return;

    if (PlayerSessionData* pd = session->GetPlayerData(player->GetGUID()))
        ++pd->Deaths;

    // Block release-spirit; auto-rez instead
    player->SetFlag(PLAYER_FIELD_BYTES, PLAYER_FIELD_BYTE_NO_RELEASE_WINDOW);
    player->RemoveFlag(PLAYER_FIELD_BYTES, PLAYER_FIELD_BYTE_RELEASE_TIMER);

    if (session->IsPartyWiped())
    {
        ++session->Wipes;

        // --- Roguelike: delegate wipe handling to RoguelikeMgr ---
        if (session->RoguelikeRunId != 0)
        {
            session->State   = SessionState::Failed;
            session->EndTime = GameTime::GetGameTime().count();
            sRoguelikeMgr->OnPartyWipe(session->RoguelikeRunId);
            return;
        }

        session->State   = SessionState::Failed;
        session->EndTime = GameTime::GetGameTime().count();

        for (auto const& psd : session->Players)
        {
            Player* p = ObjectAccessor::FindPlayer(psd.PlayerGuid);
            if (!p) continue;
            p->RemoveFlag(PLAYER_FIELD_BYTES, PLAYER_FIELD_BYTE_NO_RELEASE_WINDOW);
            if (!p->IsAlive()) { p->ResurrectPlayer(1.0f); p->SpawnCorpseBones(); }
            if (p->GetSession())
                ChatHandler(p->GetSession()).PSendSysMessage(
                    "{} Your whole party has fallen. The echo collapses around you.", CHAT_PREFIX);
            p->TeleportTo(psd.ReturnMapId, psd.ReturnPosition.GetPositionX(),
                psd.ReturnPosition.GetPositionY(), psd.ReturnPosition.GetPositionZ(),
                psd.ReturnPosition.GetOrientation());
        }
    }
    else
    {
        if (player->GetSession())
            ChatHandler(player->GetSession()).PSendSysMessage(
                "{} You have fallen. The echo will return you once your group leaves combat.", CHAT_PREFIX);
    }
}

// Rewards
void AtlasMgr::DistributeRewards(Session* session)
{
    if (!session) return;
    DifficultyTier const* diff = sAtlasConfig->GetDifficulty(session->DifficultyId);
    if (!diff) return;

    uint8  lvl       = session->EffectiveLevel;
    uint32 baseGold  = AtlasConfig::ScaleGoldForLevel(sAtlasConfig->GetBaseGold(), lvl);
    uint32 mobGold   = session->MobsKilled   * AtlasConfig::ScaleGoldForLevel(sAtlasConfig->GetGoldPerMob(), lvl);
    uint32 bossGold  = session->BossesKilled * AtlasConfig::ScaleGoldForLevel(sAtlasConfig->GetGoldPerBoss(), lvl);
    uint32 total     = static_cast<uint32>((baseGold + mobGold + bossGold) * diff->RewardMultiplier);
    uint32 perPlayer = total / std::max<uint32>(1, session->Players.size());

    uint8 rewardLevel = std::min<uint8>(lvl, 80);

    LOG_INFO("module", "Atlas: DistributeRewards — EffectiveLevel={}, rewardLevel={}, "
        "rewardPool={} items, players={}",
        lvl, rewardLevel, _rewardItems.size(), session->Players.size());

    for (auto const& pd : session->Players)
    {
        Player* p = ObjectAccessor::FindPlayer(pd.PlayerGuid);
        if (!p || !p->IsInWorld())
        {
            LOG_WARN("module", "Atlas: Player {} not found/not in world for rewards", pd.PlayerGuid.GetCounter());
            continue;
        }

        // Gold goes directly to wallet
        GiveGoldReward(p, perPlayer);

        // Completion item (Atlas.Rewards.ItemChance): roll epic first, then
        // rare, fallback green. Goes to the bags, by mail if they are full.
        if (RandInt<uint32>(1, 100) > sAtlasConfig->GetItemChance())
            continue;

        uint8 quality = 2;  // green baseline
        if (RandInt<uint32>(1, 100) <= sAtlasConfig->GetEpicChance())
            quality = 4;
        else if (RandInt<uint32>(1, 100) <= sAtlasConfig->GetRareChance())
            quality = 3;

        GiveItemReward(p, rewardLevel, quality);
    }
}

void AtlasMgr::GiveKillXP(Session* session, bool isBoss, bool isElite)
{
    if (!session) return;

    for (auto const& pd : session->Players)
    {
        Player* p = ObjectAccessor::FindPlayer(pd.PlayerGuid);
        if (!p || !p->IsAlive()) continue;
        if (p->GetLevel() >= sWorld->getIntConfig(CONFIG_MAX_PLAYER_LEVEL)) continue;

        uint32 baseXP = (p->GetLevel() * 5) + 45;

        float mult = 1.0f;
        if (isBoss)       mult = 10.0f;
        else if (isElite) mult = 2.0f;

        uint32 xp = static_cast<uint32>(baseXP * mult * sAtlasConfig->GetXPMultiplier());
        p->GiveXP(xp, nullptr);
    }
}

void AtlasMgr::GiveGoldReward(Player* player, uint32 amount)
{
    if (!player || !amount) return;
    player->ModifyMoney(amount);

    if (player->GetSession())
    {
        ChatHandler(player->GetSession()).PSendSysMessage(
            "{} The Keepers of Time reward you: |cFFFFD700{}|rg |cFFC0C0C0{}|rs |cFFB87333{}|rc",
            CHAT_PREFIX, amount / 10000, (amount % 10000) / 100, amount % 100);
    }
}

void AtlasMgr::GiveItemReward(Player* player, uint8 level, uint8 quality)
{
    uint32 playerClass = player->getClass();
    uint32 itemEntry = SelectRewardItem(level, quality, playerClass);

    // Quality fallback: if requested quality isn't found, try lower qualities
    // but still maintain level appropriateness
    if (!itemEntry && quality > 2)
    {
        LOG_WARN("module", "Atlas: No quality {} items for level {}, class {}. Trying lower quality...",
            quality, level, playerClass);
        for (uint8 q = quality - 1; q >= 2 && !itemEntry; --q)
            itemEntry = SelectRewardItem(level, q, playerClass);
    }

    if (!itemEntry)
    {
        LOG_ERROR("module", "Atlas: No suitable reward item for player {} (level {}, class {}, quality {}). "
            "Reward pool has {} items total. Gold only.",
            player->GetName(), level, playerClass, quality, _rewardItems.size());
        if (player->GetSession())
            ChatHandler(player->GetSession()).PSendSysMessage(
                "{} No gear suits your level and class this time; gold only.", CHAT_PREFIX);
        return;
    }

    LOG_DEBUG("module", "Atlas: Giving item {} to {} (level {}, quality {}, class {})",
        itemEntry, player->GetName(), level, quality, playerClass);

    ItemPosCountVec dest;
    if (player->CanStoreNewItem(NULL_BAG, NULL_SLOT, dest, itemEntry, 1) == EQUIP_ERR_OK)
    {
        if (Item* item = player->StoreNewItem(dest, itemEntry, true))
        {
            player->SendNewItem(item, 1, true, false);
            if (ItemTemplate const* t = sObjectMgr->GetItemTemplate(itemEntry))
            {
                if (player->GetSession())
                {
                    ChatHandler(player->GetSession()).PSendSysMessage(
                        "{} The Keepers of Time reward you: |cFFFFFFFF{}|r", CHAT_PREFIX, t->Name1);
                }
            }
        }
    }
    else
    {
        // Bags full — mail the item instead.
        Item* mailItem = Item::CreateItem(itemEntry, 1, player);
        if (mailItem)
        {
            CharacterDatabaseTransaction trans = CharacterDatabase.BeginTransaction();
            MailDraft("A reward from the Keepers of Time",
                "Your bags were full, so we sent it this way. The timeways thank you.")
                .AddItem(mailItem)
                .SendMailTo(trans,
                    MailReceiver(player, player->GetGUID().GetCounter()),
                    MailSender(MAIL_NORMAL, 0, MAIL_STATIONERY_GM));
            CharacterDatabase.CommitTransaction(trans);
            if (player->GetSession())
                ChatHandler(player->GetSession()).PSendSysMessage(
                    "{} Your bags are full; your reward was sent by mail.", CHAT_PREFIX);
        }
        else
        {
            LOG_ERROR("module", "Atlas: Failed to create mail item {} for {}", itemEntry, player->GetName());
        }
    }
}

    // Mail a reward item to player
void AtlasMgr::MailItemReward(Player* player, uint8 level, uint8 quality,
                                       std::string const& subject, std::string const& body)
{
    if (!player || !player->IsInWorld()) return;

    uint32 playerClass = player->getClass();
    uint32 itemEntry = SelectRewardItem(level, quality, playerClass);

    // Quality fallback
    if (!itemEntry && quality > 2)
    {
        for (uint8 q = quality - 1; q >= 2 && !itemEntry; --q)
            itemEntry = SelectRewardItem(level, q, playerClass);
    }

    // Level window fallback
    if (!itemEntry)
    {
        uint8 windows[] = { 15, 25, 80 };
        for (uint8 w : windows)
        {
            uint8 lo = (level > w) ? level - w : 1;
            uint8 hi = std::min<uint8>(level + w, 80);
            for (auto const& ri : _rewardItems)
            {
                if (ri.Quality < 2 || ri.Quality > 4) continue;
                if (ri.MinLevel < lo || ri.MinLevel > hi) continue;
                if (ri.AllowableClass != -1 && !(ri.AllowableClass & (1 << (playerClass - 1))))
                    continue;
                itemEntry = ri.Entry;
                break;
            }
            if (itemEntry) break;
        }
    }

    if (!itemEntry)
    {
        LOG_ERROR("module", "Atlas: No reward item found for mail to {} (level {}, class {})",
            player->GetName(), level, playerClass);
        return;
    }

    Item* mailItem = Item::CreateItem(itemEntry, 1, player);
    if (mailItem)
    {
        // Must use a real transaction (nullptr crashes SendMailTo)
        // inside SendMailTo when it calls trans->Append() on the null ptr.
        CharacterDatabaseTransaction trans = CharacterDatabase.BeginTransaction();

        MailDraft(subject, body)
            .AddItem(mailItem)
            .SendMailTo(trans,
                MailReceiver(player, player->GetGUID().GetCounter()),
                MailSender(MAIL_NORMAL, 0, MAIL_STATIONERY_GM));

        CharacterDatabase.CommitTransaction(trans);

        if (ItemTemplate const* t = sObjectMgr->GetItemTemplate(itemEntry))
        {
            if (player->GetSession())
            {
                ChatHandler(player->GetSession()).PSendSysMessage(
                    "{} Reward sent by mail: |cFFFFFFFF{}|r", CHAT_PREFIX, t->Name1);
            }
        }
    }
    else
    {
        LOG_ERROR("module", "Atlas: Failed to create mail item {} for {}", itemEntry, player->GetName());
    }
}

void AtlasMgr::DistributeRoguelikeRewards(uint32 tier, uint8 effectiveLevel,
                                                    std::vector<ObjectGuid> const& playerGuids)
{
    uint8 rewardLevel = static_cast<uint8>(std::min<uint32>(effectiveLevel, 80));

    uint32 baseGold = AtlasConfig::ScaleGoldForLevel(sAtlasConfig->GetBaseGold(), effectiveLevel);
    uint32 tierGold = baseGold * tier;

    // Epic chance scales with tier
    uint32 epicChance = std::min<uint32>(5 + (tier * 5), 80);

    uint32 blueItems  = 1;
    uint32 greenItems = 0;
    uint32 epicItems  = 0;

    if (tier >= 9)      { blueItems = 3; epicItems = 1; }
    else if (tier >= 7) { blueItems = 2; epicItems = 1; }
    else if (tier >= 5) { blueItems = 2; }
    else if (tier >= 3) { blueItems = 1; greenItems = 1; }

    LOG_INFO("module", "Atlas: DistributeRoguelikeRewards — tier={}, level={}, "
        "blue={}, green={}, epic={}, epicChance={}%, gold={}",
        tier, rewardLevel, blueItems, greenItems, epicItems, epicChance, tierGold);

    for (auto const& guid : playerGuids)
    {
        Player* p = ObjectAccessor::FindPlayer(guid);
        if (!p || !p->IsInWorld()) continue;

        // Gold
        GiveGoldReward(p, tierGold);

        // Items go directly to inventory (mail fallback if bags full)

        // Guaranteed epic items
        for (uint32 i = 0; i < epicItems; ++i)
            GiveItemReward(p, rewardLevel, 4);

        // Roll for bonus epics
        if (epicItems == 0 && RandInt<uint32>(1, 100) <= epicChance)
            GiveItemReward(p, rewardLevel, 4);
        else if (epicItems > 0 && tier >= 9 && RandInt<uint32>(1, 100) <= 25)
            GiveItemReward(p, rewardLevel, 4);

        // Blue items
        for (uint32 i = 0; i < blueItems; ++i)
            GiveItemReward(p, rewardLevel, 3);

        // Green items
        for (uint32 i = 0; i < greenItems; ++i)
            GiveItemReward(p, rewardLevel, 2);

        if (p->GetSession())
            ChatHandler(p->GetSession()).PSendSysMessage(
                "{} The Keepers of Time reward your descent. Check your bags.", CHAT_PREFIX_ENDLESS);
    }
}

// Armor type by class
static uint8 GetMaxArmorSubclass(uint32 playerClass)
{
    switch (playerClass)
    {
        case 5: case 8: case 9:              return 1;  // cloth: Priest, Mage, Warlock
        case 4: case 11:                     return 2;  // leather: Rogue, Druid
        case 3: case 7:                      return 3;  // mail: Hunter, Shaman
        case 1: case 2: case 6:              return 4;  // plate: Warrior, Paladin, DK
        default:                             return 4;
    }
}

static uint32 GetClassBitmask(uint32 playerClass)
{
    if (playerClass == 0 || playerClass > 11) return 0x7FF;  // all classes
    return 1 << (playerClass - 1);
}

// Primary stat for class-based reward weighting
// Returns: ITEM_MOD_AGILITY(3), ITEM_MOD_STRENGTH(4), ITEM_MOD_INTELLECT(5)
static uint32 GetPrimaryStatForClass(uint32 playerClass)
{
    switch (playerClass)
    {
        case 1:  return 4;  // Warrior  -> STR
        case 2:  return 4;  // Paladin  -> STR
        case 3:  return 3;  // Hunter   -> AGI
        case 4:  return 3;  // Rogue    -> AGI
        case 5:  return 5;  // Priest   -> INT
        case 6:  return 4;  // DK       -> STR
        case 7:  return 5;  // Shaman   -> INT
        case 8:  return 5;  // Mage     -> INT
        case 9:  return 5;  // Warlock  -> INT
        case 11: return 3;  // Druid    -> AGI
        default: return 4;
    }
}

// Score item stat alignment with player class (0.0 = bad, 1.0 = perfect match)
static float ScoreItemForClass(uint32 itemEntry, uint32 playerClass)
{
    ItemTemplate const* proto = sObjectMgr->GetItemTemplate(itemEntry);
    if (!proto) return 0.0f;

    uint32 primaryStat = GetPrimaryStatForClass(playerClass);
    float totalStats = 0.0f;
    float primaryTotal = 0.0f;

    for (uint8 i = 0; i < MAX_ITEM_PROTO_STATS; ++i)
    {
        int32 val = proto->ItemStat[i].ItemStatValue;
        uint32 type = proto->ItemStat[i].ItemStatType;
        if (val <= 0) continue;

        totalStats += static_cast<float>(val);
        if (type == primaryStat)
            primaryTotal += static_cast<float>(val);
    }

    if (totalStats <= 0.0f) return 0.5f;  // No stats (trinket, etc.) = neutral
    return primaryTotal / totalStats;
}

// Atlas.Rewards.MaxItemLevel: keeps raid-tier gear (ICC, arena) out of the pools.
// 0 disables the ceiling.
static bool IsUnderItemLevelCeiling(uint16 itemLevel)
{
    uint16 const ceiling = sAtlasConfig->GetMaxItemLevel();
    return ceiling == 0 || itemLevel <= ceiling;
}

uint32 AtlasMgr::SelectRewardItem(uint8 level, uint8 quality, uint32 playerClass)
{
    uint8  maxArmor   = GetMaxArmorSubclass(playerClass);
    uint32 classMask  = GetClassBitmask(playerClass);

    // Try progressively wider level windows, but always prefer closer to player level
    struct { uint8 below; uint8 above; } windows[] = {
        { 3, 0 },    // strict: [level-3, level]
        { 8, 0 },    // medium: [level-8, level]
        { 15, 0 },   // wide: [level-15, level]
        { 25, 0 },   // very wide: [level-25, level]
        { 80, 0 },   // last resort: [1, level] (never items above player level)
    };

    for (auto const& win : windows)
    {
        std::vector<uint32> cands;
        uint8 lo = (level > win.below) ? (level - win.below) : 1;
        uint8 hi = level;  // Never give items above player level

        for (auto const& ri : _rewardItems)
        {
            // Quality filter
            if (ri.Quality != quality) continue;
            if (!IsUnderItemLevelCeiling(ri.ItemLevel)) continue;

            // Level filter: item RequiredLevel must be within window
            if (ri.MinLevel < lo || ri.MinLevel > hi) continue;

            // Class restriction: AllowableClass bitmask check
            if (ri.AllowableClass != -1 && !(ri.AllowableClass & classMask))
                continue;

            // Armor subclass: player can only wear their class's max armor or lower
            if (ri.Class == 4 && ri.SubClass > 0 && ri.SubClass <= 4)
            {
                if (ri.SubClass > maxArmor) continue;
            }

            cands.push_back(ri.Entry);
        }

        if (!cands.empty())
        {
            LOG_DEBUG("module", "Atlas: SelectRewardItem(level={}, quality={}, class={}) "
                "-> {} candidates in window [{}, {}]",
                level, quality, playerClass, cands.size(), lo, hi);

            // 75% chance: bias toward items with matching primary stat
            if (cands.size() > 3 && playerClass > 0 && RandInt<uint32>(1, 100) <= 75)
            {
                std::vector<std::pair<uint32, float>> scored;
                scored.reserve(cands.size());
                for (uint32 entry : cands)
                    scored.push_back({entry, ScoreItemForClass(entry, playerClass)});

                std::sort(scored.begin(), scored.end(),
                    [](auto const& a, auto const& b) { return a.second > b.second; });

                // Pick from the top third (at least 3 items)
                size_t topN = std::max<size_t>(3, scored.size() / 3);
                return scored[RandInt<size_t>(0, topN - 1)].first;
            }

            // 25% chance: purely random from valid pool
            return cands[RandInt<size_t>(0, cands.size() - 1)];
        }
    }

    LOG_WARN("module", "Atlas: SelectRewardItem(level={}, quality={}, class={}) "
        "-> NO candidates found in reward pool ({} items total)",
        level, quality, playerClass, _rewardItems.size());

    return 0;
}

uint32 AtlasMgr::SelectLootItem(uint8 level, uint8 minQuality, uint8 maxQuality,
                                        bool equipmentOnly, uint32 playerClass)
{
    // Expected ItemLevel range for this level
    uint16 expectedMaxIlvl = static_cast<uint16>(level) * 2 + 10;

    uint8  maxArmor  = playerClass ? GetMaxArmorSubclass(playerClass) : 4;
    uint32 classMask = playerClass ? GetClassBitmask(playerClass) : 0x7FF;

    // Progressively widen level windows, always preferring items closer to player level
    struct { uint8 below; uint8 above; } windows[] = {
        { 3, 1 },   // strict: RequiredLevel in [level-3, level+1]
        { 5, 2 },   // medium
        { 8, 3 },   // wide
        { 15, 5 },  // very wide
        { 25, 8 },  // extremely wide (last resort)
    };

    for (auto const& win : windows)
    {
        uint8 lo = (level > win.below) ? (level - win.below) : 0;
        uint8 hi = std::min<uint16>(level + win.above, 83);

        std::vector<uint32> cands;
        for (auto const& li : _lootPool)
        {
            if (li.Quality < minQuality || li.Quality > maxQuality) continue;
            if (!IsUnderItemLevelCeiling(li.ItemLevel)) continue;
            if (equipmentOnly && li.ItemClass != 2 && li.ItemClass != 4) continue;

            // Level filter for items with RequiredLevel > 0
            if (li.MinLevel > 0)
            {
                if (li.MinLevel < lo || li.MinLevel > hi) continue;
            }
            else
            {
                // RequiredLevel = 0: use ItemLevel as a sanity check
                if (li.ItemLevel > expectedMaxIlvl) continue;
            }

            // Class restriction for equipment items
            if (equipmentOnly || (li.ItemClass == 2 || li.ItemClass == 4))
            {
                // AllowableClass bitmask check
                if (li.AllowableClass != -1 && !(li.AllowableClass & classMask))
                    continue;

                // Armor subclass check (only for armor, not weapons)
                if (li.ItemClass == 4 && li.SubClass > 0 && li.SubClass <= 4)
                    if (li.SubClass > maxArmor) continue;
            }

            cands.push_back(li.Entry);
        }

        if (!cands.empty())
        {
            LOG_DEBUG("module", "Atlas: SelectLootItem(level={}, quality={}-{}, eqOnly={}, class={}) "
                "-> {} candidates in window [{}, {}]",
                level, minQuality, maxQuality, equipmentOnly, playerClass, cands.size(), lo, hi);

            // Bias equipment loot toward matching primary stat (75% chance)
            if (equipmentOnly && playerClass > 0 && cands.size() > 3
                && RandInt<uint32>(1, 100) <= 75)
            {
                std::vector<std::pair<uint32, float>> scored;
                scored.reserve(cands.size());
                for (uint32 entry : cands)
                    scored.push_back({entry, ScoreItemForClass(entry, playerClass)});

                std::sort(scored.begin(), scored.end(),
                    [](auto const& a, auto const& b) { return a.second > b.second; });

                size_t topN = std::max<size_t>(3, scored.size() / 3);
                return scored[RandInt<size_t>(0, topN - 1)].first;
            }

            return cands[RandInt<size_t>(0, cands.size() - 1)];
        }
    }

    LOG_WARN("module", "Atlas: SelectLootItem(level={}, quality={}-{}, eqOnly={}, class={}) "
        "-> NO candidates found in loot pool ({} items total)",
        level, minQuality, maxQuality, equipmentOnly, playerClass, _lootPool.size());

    return 0;
}

void AtlasMgr::FillCreatureLoot(Creature* creature, Session* session, bool isBoss)
{
    if (!creature || !session) return;

    Loot& loot = creature->loot;
    loot.clear();

    uint8 level = session->EffectiveLevel;

    // Pick a random party member's class for loot filtering
    uint32 lootClass = 0;
    if (!session->Players.empty())
    {
        // Try to pick a random alive player's class
        std::vector<uint32> classes;
        for (auto const& pd : session->Players)
        {
            Player* p = ObjectAccessor::FindPlayer(pd.PlayerGuid);
            if (p && p->IsAlive())
                classes.push_back(p->getClass());
        }
        if (classes.empty())
        {
            // All dead? Just pick from any player
            for (auto const& pd : session->Players)
            {
                Player* p = ObjectAccessor::FindPlayer(pd.PlayerGuid);
                if (p) { classes.push_back(p->getClass()); break; }
            }
        }
        if (!classes.empty())
            lootClass = classes[RandInt<size_t>(0, classes.size() - 1)];
    }

    // Gold drop
    uint32 baseGold = isBoss ? (level * 2000u) : (level * 200u);
    loot.gold = std::max(500u, baseGold + RandInt<uint32>(0, baseGold / 3));

    // Item drops
    uint32 itemsAdded = 0;
    auto addItem = [&](uint8 minQ, uint8 maxQ, bool eqOnly) -> bool
    {
        uint32 entry = SelectLootItem(level, minQ, maxQ, eqOnly, eqOnly ? lootClass : 0);
        if (!entry)
        {
            LOG_WARN("module", "Atlas: FillCreatureLoot failed to find item (level={}, quality={}-{}, eqOnly={}, class={})",
                level, minQ, maxQ, eqOnly, lootClass);
            return false;
        }

        LootStoreItem storeItem(entry, 0, 100.0f, false, 1, 0, 1, 1);
        loot.AddItem(storeItem);
        ++itemsAdded;
        LOG_DEBUG("module", "Atlas: Added loot item {} (quality {}-{}) to {} (boss={})",
            entry, minQ, maxQ, creature->GetName(), isBoss);
        return true;
    };

    if (isBoss)
    {
        // Boss: 2 guaranteed rare (blue) equipment pieces
        if (!addItem(3, 3, true))
            addItem(2, 3, true);   // fallback to green/blue if no rare at this level
        if (!addItem(3, 3, true))
            addItem(2, 3, true);
    }
    else
    {
        bool isElite = false;
        bool isRare  = false;
        for (auto const& sc : session->SpawnedCreatures)
        {
            if (sc.Guid == creature->GetGUID())
            {
                isElite = sc.IsElite;
                isRare  = sc.IsRare;
                break;
            }
        }

        if (isRare)
        {
            // Rare spawn: guaranteed blue equipment piece
            if (!addItem(3, 3, true))
                addItem(2, 3, true);
        }
        else if (isElite)
        {
            // Elite: 40% chance of green equipment
            if (RandInt<uint32>(1, 100) <= 40)
            {
                if (!addItem(2, 2, true))
                    addItem(2, 2, false);
            }
        }
        else
        {
            // Trash: 15% grey/white junk, 3% green equipment
            if (RandInt<uint32>(1, 100) <= 15)
                addItem(0, 1, false);
            if (RandInt<uint32>(1, 100) <= 3)
                addItem(2, 2, true);
        }
    }

    // Ensure lootable flag is set (critical for boss loot)
    creature->SetDynamicFlag(UNIT_DYNFLAG_LOOTABLE);

    // --- Group Loot Support (Need/Greed) ---
    // If the killing player is in a group with Group Loot or Need Before Greed,
    // trigger the group's loot distribution system for qualifying items.
    loot.loot_type = LOOT_CORPSE;
    Player* looter = nullptr;
    Group*  group  = nullptr;
    for (auto const& pd : session->Players)
    {
        Player* p = ObjectAccessor::FindPlayer(pd.PlayerGuid);
        if (p && p->IsInWorld() && p->GetGroup())
        {
            looter = p;
            group  = p->GetGroup();
            break;
        }
    }

    if (group && looter)
    {
        // Set both individual and group loot recipient so the group's loot
        // distribution system (Need/Greed/Pass) is properly triggered.
        creature->SetLootRecipient(looter, true);

        // Mark items above the group's loot threshold for rolling;
        // items below threshold become free-for-all (direct loot)
        uint8 threshold = group->GetLootThreshold();
        for (auto& item : loot.items)
        {
            ItemTemplate const* proto = sObjectMgr->GetItemTemplate(item.itemid);
            if (!proto || proto->Quality < threshold)
                item.is_underthreshold = true;
        }

        // Trigger group loot distribution — sends Need/Greed/Pass rolls
        // to all eligible group members for qualifying items
        group->GroupLoot(&loot, creature);

        // Force dynamic flag update to all session players so they see the lootable corpse
        for (auto const& pd : session->Players)
        {
            Player* p = ObjectAccessor::FindPlayer(pd.PlayerGuid);
            if (p && p->IsInWorld() && p->GetMapId() == session->MapId)
                creature->SendUpdateToPlayer(p);
        }

        LOG_DEBUG("module", "Atlas: Group loot triggered for {} — {} items, "
            "lootMethod={}, threshold={}, groupSize={}",
            creature->GetName(), loot.items.size(),
            static_cast<int>(group->GetLootMethod()), threshold, group->GetMembersCount());
    }
    else if (looter)
    {
        creature->SetLootRecipient(looter);
    }

    LOG_DEBUG("module", "Atlas: FillCreatureLoot complete for {} (GUID: {}, Boss: {}, Level: {}, Gold: {}, Items: {})",
        creature->GetName(), creature->GetGUID().GetCounter(), isBoss, level, loot.gold, itemsAdded);
}

// Session end / cleanup
void AtlasMgr::EndSession(uint32 sessionId, bool success)
{
    // Check if roguelike session
    uint32 roguelikeRunId = 0;
    {
        std::lock_guard<std::mutex> lock(_sessionMutex);
        auto it = _activeSessions.find(sessionId);
        if (it == _activeSessions.end()) return;

        Session& s = it->second;
        roguelikeRunId = s.RoguelikeRunId;

        if (roguelikeRunId != 0)
        {
            LOG_INFO("module", "Atlas: EndSession {} — roguelike run {}, delegating to RoguelikeMgr.",
                sessionId, roguelikeRunId);

            // Persist stats while session is still alive
            UpdatePlayerStatsFromSession(s, success);
            ReleaseRunInstanceBinds(s);

            // Clean up mappings
            uint32 savedInstanceId = s.InstanceId;
            if (savedInstanceId != 0)
                _instanceToSession.erase(savedInstanceId);
            for (auto const& pd : s.Players)
                _playerToSession.erase(pd.PlayerGuid);

            _activeSessions.erase(it);
            g_dmActiveSessionCount.store(static_cast<uint32>(_activeSessions.size()), std::memory_order_relaxed);
        }
    } // lock released

    if (roguelikeRunId != 0)
    {
        sRoguelikeMgr->EndRun(roguelikeRunId, false);
        return;
    }

    // --- Normal (non-roguelike) session ---
    std::lock_guard<std::mutex> lock(_sessionMutex);
    auto it = _activeSessions.find(sessionId);
    if (it == _activeSessions.end()) return;

    Session& s = it->second;

    LOG_INFO("module", "Atlas: EndSession {} — success={}, state={}, players={}",
        sessionId, success, static_cast<int>(s.State), s.Players.size());

    for (auto const& pd : s.Players)
        if (Player* p = ObjectAccessor::FindPlayer(pd.PlayerGuid))
            if (p->GetSession())
                ChatHandler(p->GetSession()).PSendSysMessage("{} {}", CHAT_PREFIX,
                    success ? "The echo is mended. The Keepers of Time thank you."
                            : "The echo fades unmended. There is no reward this time.");

    if (success && s.State == SessionState::Completed)
        DistributeRewards(&s);

    UpdatePlayerStatsFromSession(s, success);
    if (success && s.State == SessionState::Completed)
        SaveLeaderboardEntry(s);

    TeleportPartyOut(&s);
    ReleaseRunInstanceBinds(s);

    // Save instance ID before cleanup
    uint32 savedInstanceId = s.InstanceId;
    CleanupSession(s);

    for (auto const& pd : s.Players)
        SetCooldown(pd.PlayerGuid);

    if (savedInstanceId != 0)
        _instanceToSession.erase(savedInstanceId);
    for (auto const& pd : s.Players)
        _playerToSession.erase(pd.PlayerGuid);

    _activeSessions.erase(it);
    g_dmActiveSessionCount.store(static_cast<uint32>(_activeSessions.size()), std::memory_order_relaxed);
}

void AtlasMgr::AbandonSession(uint32 id) { EndSession(id, false); }

void AtlasMgr::CleanupRoguelikeSession(uint32 sessionId, bool success)
{
    std::lock_guard<std::mutex> lock(_sessionMutex);
    auto it = _activeSessions.find(sessionId);
    if (it == _activeSessions.end()) return;

    Session& s = it->second;

    UpdatePlayerStatsFromSession(s, success);
    if (success && s.State == SessionState::Completed)
        SaveLeaderboardEntry(s);

    // The floor's instance is never reused (next floor or teleport home).
    ReleaseRunInstanceBinds(s);

    uint32 savedInstanceId = s.InstanceId;

    // Clean up mappings (no teleport/cooldowns for roguelike)
    if (savedInstanceId != 0)
        _instanceToSession.erase(savedInstanceId);
    for (auto const& pd : s.Players)
        _playerToSession.erase(pd.PlayerGuid);

    _activeSessions.erase(it);
    g_dmActiveSessionCount.store(static_cast<uint32>(_activeSessions.size()), std::memory_order_relaxed);

    LOG_DEBUG("module", "Atlas: Roguelike session {} cleaned up (success={}).",
        sessionId, success);
}

void AtlasMgr::CleanupSession(Session& s)
{
    // Release level ownership before the instance id is cleared, otherwise the
    // map grows for the lifetime of the process.
    if (s.InstanceId)
        ForgetOwnedCreaturesForInstance(s.InstanceId);

    s.InstanceId = 0;
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

// Cooldowns
bool AtlasMgr::IsOnCooldown(ObjectGuid g) const
{
    std::lock_guard<std::mutex> lock(_cooldownMutex);
    auto it = _cooldowns.find(g);
    return it != _cooldowns.end()
        && GameTime::GetGameTime().count() < static_cast<time_t>(it->second);
}

void AtlasMgr::SetCooldown(ObjectGuid g)
{
    std::lock_guard<std::mutex> lock(_cooldownMutex);
    _cooldowns[g] = GameTime::GetGameTime().count() + sAtlasConfig->GetCooldownMinutes() * 60;
}

void AtlasMgr::ClearCooldown(ObjectGuid g)
{
    std::lock_guard<std::mutex> lock(_cooldownMutex);
    _cooldowns.erase(g);
}

uint32 AtlasMgr::GetRemainingCooldown(ObjectGuid g) const
{
    std::lock_guard<std::mutex> lock(_cooldownMutex);
    auto it = _cooldowns.find(g);
    if (it == _cooldowns.end()) return 0;
    time_t now = GameTime::GetGameTime().count();
    return (now < static_cast<time_t>(it->second))
        ? static_cast<uint32>(it->second - now) : 0;
}

bool AtlasMgr::CanCreateNewSession() const
{
    return _activeSessions.size() < sAtlasConfig->GetMaxConcurrentRuns();
}

// Entry checks (see AtlasMgr.h)

std::vector<Player*> AtlasMgr::GatherParty(Player* leader)
{
    std::vector<Player*> party;
    if (!leader)
        return party;

    party.push_back(leader);
    if (Group* g = leader->GetGroup())
        for (GroupReference* ref = g->GetFirstMember(); ref; ref = ref->next())
            if (Player* m = ref->GetSource())
                if (m != leader && m->IsInWorld())
                    party.push_back(m);
    return party;
}

uint8 AtlasMgr::GetPartyMinLevel(std::vector<Player*> const& party)
{
    uint8 lowest = 0;
    for (Player* p : party)
        if (!lowest || p->GetLevel() < lowest)
            lowest = p->GetLevel();
    return lowest;
}

bool AtlasMgr::CanPartyEnter(std::vector<Player*> const& party, uint32 mapId)
{
    if (party.empty())
        return false;

    // Atlas bracket: the dungeon may not start above the lowest party member.
    if (DungeonInfo const* dg = sAtlasConfig->GetDungeon(mapId))
        if (dg->MinLevel > GetPartyMinLevel(party))
            return false;

    // Runs are Normal-only (see StartDungeon), so only the Normal requirement applies.
    DungeonProgressionRequirements const* ar = sObjectMgr->GetAccessRequirement(mapId, DUNGEON_DIFFICULTY_NORMAL);
    for (Player* p : party)
    {
        // Asked first because Satisfy() announces a closed map even when told not to report.
        if (sDisableMgr->IsDisabledFor(DISABLE_TYPE_MAP, mapId, p))
            return false;
        if (!p->Satisfy(ar, mapId, false))
            return false;
    }
    return true;
}

std::vector<DungeonInfo const*> AtlasMgr::FilterEnterable(std::vector<DungeonInfo const*> const& dungeons,
                                                          std::vector<Player*> const& party)
{
    std::vector<DungeonInfo const*> out;
    for (DungeonInfo const* dg : dungeons)
        if (CanPartyEnter(party, dg->MapId))
            out.push_back(dg);
    return out;
}

Player* AtlasMgr::FindAtInstanceCap(std::vector<Player*> const& party)
{
    // Every echo is a fresh instance, so it counts against the hourly cap.
    // Game masters bypass the cap in MapMgr::PlayerCannotEnter.
    for (Player* p : party)
        if (!p->IsGameMaster() && !p->CheckInstanceCount(0))
            return p;
    return nullptr;
}

std::string AtlasMgr::InstanceCapNotice(Player const* capped)
{
    return Acore::StringFormat(
        "{} The timeways are strained: |cFFFFFFFF{}|r has entered {} dungeons within the hour, "
        "as many as this realm allows. Rest until an hour has passed since the first of them.",
        CHAT_PREFIX, capped->GetName(), sWorld->getIntConfig(CONFIG_MAX_INSTANCES_PER_HOUR));
}

// Player Statistics & Leaderboard

void AtlasMgr::LoadAllPlayerStats()
{
    std::lock_guard<std::mutex> lock(_statsMutex);
    _playerStats.clear();

    QueryResult result = CharacterDatabase.Query(
        "SELECT guid, total_runs, completed_runs, failed_runs, "
        "total_mobs_killed, total_bosses_killed, total_deaths, fastest_clear "
        "FROM atlas_player_stats");

    if (!result)
    {
        LOG_INFO("module", "Atlas: No player stats found (table may be empty or missing).");
        return;
    }

    uint32 count = 0;
    do
    {
        Field* f = result->Fetch();
        uint32 guidLow = f[0].Get<uint32>();

        PlayerStats ps;
        ps.TotalRuns        = f[1].Get<uint32>();
        ps.CompletedRuns    = f[2].Get<uint32>();
        ps.FailedRuns       = f[3].Get<uint32>();
        ps.TotalMobsKilled  = f[4].Get<uint32>();
        ps.TotalBossesKilled = f[5].Get<uint32>();
        ps.TotalDeaths      = f[6].Get<uint32>();
        ps.FastestClear     = f[7].Get<uint32>();

        _playerStats[guidLow] = ps;
        ++count;
    } while (result->NextRow());

    LOG_INFO("module", "Atlas: Loaded stats for {} players.", count);
}

PlayerStats AtlasMgr::GetPlayerStats(ObjectGuid guid) const
{
    std::lock_guard<std::mutex> lock(_statsMutex);
    uint32 guidLow = guid.GetCounter();
    auto it = _playerStats.find(guidLow);
    if (it != _playerStats.end())
        return it->second;
    return {};
}

void AtlasMgr::SavePlayerStats(uint32 guidLow)
{
    PlayerStats ps;
    {
        std::lock_guard<std::mutex> lock(_statsMutex);
        auto it = _playerStats.find(guidLow);
        if (it == _playerStats.end()) return;
        ps = it->second;
    }

    CharacterDatabase.Execute(
        "REPLACE INTO atlas_player_stats "
        "(guid, total_runs, completed_runs, failed_runs, "
        "total_mobs_killed, total_bosses_killed, total_deaths, fastest_clear) "
        "VALUES ({}, {}, {}, {}, {}, {}, {}, {})",
        guidLow, ps.TotalRuns, ps.CompletedRuns, ps.FailedRuns,
        ps.TotalMobsKilled, ps.TotalBossesKilled, ps.TotalDeaths, ps.FastestClear);
}

void AtlasMgr::UpdatePlayerStatsFromSession(Session const& session, bool success)
{
    uint32 clearTime = 0;
    if (session.EndTime > session.StartTime)
        clearTime = static_cast<uint32>(session.EndTime - session.StartTime);
    else
        clearTime = static_cast<uint32>(GameTime::GetGameTime().count() - session.StartTime);

    for (auto const& pd : session.Players)
    {
        uint32 guidLow = pd.PlayerGuid.GetCounter();

        {
            std::lock_guard<std::mutex> lock(_statsMutex);
            auto& ps = _playerStats[guidLow];
            ps.TotalRuns++;
            if (success)
            {
                ps.CompletedRuns++;
                if (ps.FastestClear == 0 || clearTime < ps.FastestClear)
                    ps.FastestClear = clearTime;
            }
            else
                ps.FailedRuns++;

            ps.TotalMobsKilled   += pd.MobsKilled;
            ps.TotalBossesKilled += pd.BossesKilled;
            ps.TotalDeaths       += pd.Deaths;
        }

        SavePlayerStats(guidLow);
    }
}

void AtlasMgr::SaveLeaderboardEntry(Session const& session)
{
    uint32 clearTime = 0;
    if (session.EndTime > session.StartTime)
        clearTime = static_cast<uint32>(session.EndTime - session.StartTime);
    else
        clearTime = static_cast<uint32>(GameTime::GetGameTime().count() - session.StartTime);

    if (clearTime == 0)
        return;

    LeaderboardEntry e;
    e.Guid           = session.LeaderGuid.GetCounter();
    e.CharName       = "Unknown";
    if (Player* leader = ObjectAccessor::FindPlayer(session.LeaderGuid))
        e.CharName = leader->GetName();
    e.MapId          = session.MapId;
    e.DifficultyId   = session.DifficultyId;
    e.ClearTime      = clearTime;
    e.PartySize      = static_cast<uint8>(session.Players.size());
    e.Scaled         = session.ScaleToParty;
    e.EffectiveLevel = session.EffectiveLevel;

    // Aggregate kills/deaths across all participants
    for (auto const& pd : session.Players)
    {
        e.MobsKilled   += pd.MobsKilled;
        e.BossesKilled += pd.BossesKilled;
        e.Deaths       += pd.Deaths;
    }

    std::string safeName = e.CharName;
    CharacterDatabase.EscapeString(safeName);
    CharacterDatabase.Execute(
        "INSERT INTO atlas_leaderboard "
        "(guid, char_name, map_id, difficulty_id, clear_time, party_size, scaled, "
        "effective_level, mobs_killed, bosses_killed, deaths) "
        "VALUES ({}, '{}', {}, {}, {}, {}, {}, {}, {}, {}, {})",
        e.Guid, safeName, e.MapId, e.DifficultyId, e.ClearTime, e.PartySize,
        e.Scaled ? 1 : 0, e.EffectiveLevel, e.MobsKilled, e.BossesKilled, e.Deaths);

    std::lock_guard<std::mutex> lock(_leaderboardMutex);
    _leaderboard.push_back(std::move(e));
}

// Leaderboards are read from the NPC menu, i.e. on a map thread: serve them
// from memory (loaded once at startup, appended on save) instead of running a
// synchronous characters-DB query per menu open.
void AtlasMgr::LoadLeaderboard()
{
    std::vector<LeaderboardEntry> rows;

    if (QueryResult result = CharacterDatabase.Query(
            "SELECT id, guid, char_name, map_id, difficulty_id, clear_time, party_size, "
            "scaled, effective_level, mobs_killed, bosses_killed, deaths FROM atlas_leaderboard"))
    {
        do
        {
            Field* f = result->Fetch();
            LeaderboardEntry e;
            e.Id             = f[0].Get<uint32>();
            e.Guid           = f[1].Get<uint32>();
            e.CharName       = f[2].Get<std::string>();
            e.MapId          = f[3].Get<uint32>();
            e.DifficultyId   = f[4].Get<uint32>();
            e.ClearTime      = f[5].Get<uint32>();
            e.PartySize      = f[6].Get<uint8>();
            e.Scaled         = f[7].Get<uint8>() != 0;
            e.EffectiveLevel = f[8].Get<uint8>();
            e.MobsKilled     = f[9].Get<uint32>();
            e.BossesKilled   = f[10].Get<uint32>();
            e.Deaths         = f[11].Get<uint32>();
            rows.push_back(std::move(e));
        } while (result->NextRow());
    }

    std::lock_guard<std::mutex> lock(_leaderboardMutex);
    _leaderboard = std::move(rows);
}

std::vector<LeaderboardEntry> AtlasMgr::GetLeaderboard(
    uint32 mapId, uint32 difficultyId, uint32 limit) const
{
    std::vector<LeaderboardEntry> entries;
    {
        std::lock_guard<std::mutex> lock(_leaderboardMutex);
        for (auto const& e : _leaderboard)
            if (e.MapId == mapId && e.DifficultyId == difficultyId)
                entries.push_back(e);
    }

    std::sort(entries.begin(), entries.end(),
        [](LeaderboardEntry const& a, LeaderboardEntry const& b) { return a.ClearTime < b.ClearTime; });
    if (entries.size() > limit)
        entries.resize(limit);
    return entries;
}

std::vector<LeaderboardEntry> AtlasMgr::GetOverallLeaderboard(uint32 limit) const
{
    std::vector<LeaderboardEntry> entries;
    {
        std::lock_guard<std::mutex> lock(_leaderboardMutex);
        entries = _leaderboard;
    }

    std::sort(entries.begin(), entries.end(),
        [](LeaderboardEntry const& a, LeaderboardEntry const& b) { return a.ClearTime < b.ClearTime; });
    if (entries.size() > limit)
        entries.resize(limit);
    return entries;
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

// Main update tick (1s interval)
void AtlasMgr::Update(uint32 diff)
{
    _updateTimer += diff;
    if (_updateTimer < UPDATE_INTERVAL)
        return;
    _updateTimer = 0;

    std::vector<std::pair<uint32, bool>> toEnd;
    std::vector<std::pair<uint32, uint32>> roguelikeCompleted; // {runId, sessionId}

    {
        std::lock_guard<std::mutex> lock(_sessionMutex);

        for (auto& [sid, session] : _activeSessions)
        {
            // ---- Poll creature deaths ----
            if (session.IsActive())
            {
                Player* ref = nullptr;
                for (auto const& pd : session.Players)
                {
                    Player* p = ObjectAccessor::FindPlayer(pd.PlayerGuid);
                    if (p && p->GetMapId() == session.MapId) { ref = p; break; }
                }

                if (ref)
                {
                    // ---- Ensure instance mapping is registered ----
                    if (session.InstanceId == 0)
                    {
                        Map* m2 = ref->GetMap();
                        if (m2 && m2->IsDungeon())
                        {
                            session.InstanceId = m2->ToInstanceMap()->GetInstanceId();
                        }
                    }
                    if (session.InstanceId != 0 &&
                        _instanceToSession.find(session.InstanceId) == _instanceToSession.end())
                    {
                        _instanceToSession[session.InstanceId] = session.SessionId;
                    }

                    // ---- Populate if not yet done ----
                    if (session.TotalMobs == 0 && session.TotalBosses == 0)
                    {
                        Map* m = ref->GetMap();
                        if (m && m->IsDungeon())
                        {
                            InstanceMap* inst = m->ToInstanceMap();
                            if (inst)
                            {
                                session.InstanceId = inst->GetInstanceId();
                                _instanceToSession[session.InstanceId] = session.SessionId;

                                for (auto const& pd2 : session.Players)
                                    if (Player* p2 = ObjectAccessor::FindPlayer(pd2.PlayerGuid))
                                        ChatHandler(p2->GetSession()).PSendSysMessage(
                                            "{} The echo takes shape around you...", CHAT_PREFIX);

                                PopulateDungeon(&session, inst);

                                LOG_INFO("module", "Atlas: Session {} — populated (map {}, mobs={}, bosses={})",
                                    session.SessionId, session.MapId,
                                    session.TotalMobs, session.TotalBosses);

                                std::string const ready = Acore::StringFormat(
                                    "{} |cFFFFFFFF{}|r foes and |cFFFFFFFF{}|r master{} hold this echo "
                                    "(levels |cFFFFFFFF{}-{}|r).",
                                    CHAT_PREFIX, session.TotalMobs, session.TotalBosses,
                                    session.TotalBosses != 1 ? "s" : "",
                                    session.LevelBandMin, session.LevelBandMax);
                                for (auto const& pd2 : session.Players)
                                    if (Player* p2 = ObjectAccessor::FindPlayer(pd2.PlayerGuid))
                                        ChatHandler(p2->GetSession()).SendSysMessage(ready);
                            }
                        }
                    }

                    // Build set of our known GUIDs for stray detection
                    std::set<ObjectGuid> ourGuids;
                    for (auto const& sc : session.SpawnedCreatures)
                        ourGuids.insert(sc.Guid);

                    for (auto& sc : session.SpawnedCreatures)
                    {
                        if (sc.IsDead && sc.LootFilled && sc.KillCredited)
                            continue;   // fully processed
                        Creature* c = ObjectAccessor::GetCreature(*ref, sc.Guid);
                        if (!c || !c->IsAlive())
                        {
                            sc.IsDead = true;

                            if (!sc.LootFilled && c)
                            {
                                sc.LootFilled = true;
                                FillCreatureLoot(c, &session, sc.IsBoss);
                            }

                            if (!sc.KillCredited)
                            {
                                sc.KillCredited = true;
                                GiveKillXP(&session, sc.IsBoss, sc.IsElite);

                                if (sc.IsBoss)
                                {
                                    PendingPhaseCheck ppc;
                                    if (c)
                                        ppc.DeathPos = { c->GetPositionX(), c->GetPositionY(),
                                                         c->GetPositionZ(), c->GetOrientation() };
                                    ppc.DeathTime = GameTime::GetGameTime().count();
                                    ppc.OrigEntry = sc.Entry;
                                    ppc.Resolved  = false;
                                    session.PendingPhaseChecks.push_back(ppc);
                                }
                                else
                                {
                                    ++session.MobsKilled;
                                    for (auto& pd : session.Players)
                                        ++pd.MobsKilled;
                                }
                            }
                        }
                    }

                    // ---- Multi-phase boss resolution ----
                    // After 5 seconds, check if new creatures spawned near the boss death location.
                    // If found, promote them to boss status. If not, confirm the boss kill.
                    uint64 nowTime = GameTime::GetGameTime().count();
                    for (auto& ppc : session.PendingPhaseChecks)
                    {
                        if (ppc.Resolved) continue;
                        if (nowTime - ppc.DeathTime < 5) continue;  // Wait 5 seconds for phase transitions

                        ppc.Resolved = true;

                        // Scan for new non-tracked creatures near the boss death position
                        bool phaseCreatureFound = false;
                        Map* scanMap = ref->GetMap();
                        if (scanMap && scanMap->IsDungeon() && ppc.DeathPos.GetPositionX() != 0.0f)
                        {
                            std::list<Creature*> nearby;
                            ref->GetCreatureListWithEntryInGrid(nearby, 0, 5000.0f);

                            for (Creature* nc : nearby)
                            {
                                if (!nc || !nc->IsAlive() || nc->IsPet() || nc->IsGuardian())
                                    continue;
                                if (nc->GetEntry() == sAtlasConfig->GetNpcEntry())
                                    continue;
                                if (ourGuids.count(nc->GetGUID()) > 0)
                                    continue;  // Already tracked

                                // Check distance from boss death position (within 40 yards)
                                float dx = nc->GetPositionX() - ppc.DeathPos.GetPositionX();
                                float dy = nc->GetPositionY() - ppc.DeathPos.GetPositionY();
                                float dz = nc->GetPositionZ() - ppc.DeathPos.GetPositionZ();
                                float dist = std::sqrt(dx*dx + dy*dy + dz*dz);

                                if (dist > 40.0f) continue;

                                // Check if it's an elite/boss creature (likely phase 2)
                                CreatureTemplate const* tmpl = nc->GetCreatureTemplate();
                                if (!tmpl || (tmpl->rank != 1 && tmpl->rank != 2 && tmpl->rank != 4))
                                    continue;

                                // Promote to boss creature
                                LOG_INFO("module", "Atlas: Phase creature detected! '{}' (entry {}) "
                                    "spawned {:.1f} yds from boss death location — promoting to boss",
                                    nc->GetName(), nc->GetEntry(), dist);

                                nc->SetFaction(14);
                                nc->SetReactState(REACT_AGGRESSIVE);
                                nc->RemoveFlag(UNIT_FIELD_FLAGS, UNIT_FLAG_NON_ATTACKABLE | UNIT_FLAG_IMMUNE_TO_PC
                                                                | UNIT_FLAG_IMMUNE_TO_NPC | UNIT_FLAG_PACIFIED);
                                nc->SetImmuneToPC(false);
                                nc->SetImmuneToNPC(false);

                                SpawnedCreature nsc;
                                nsc.Guid = nc->GetGUID();
                                nsc.Entry = nc->GetEntry();
                                nsc.IsElite = true;
                                nsc.IsBoss = true;
                                session.SpawnedCreatures.push_back(nsc);
                                ourGuids.insert(nc->GetGUID());

                                // Track the GUID for cleanup
                                auto& gl = _instanceCreatureGuids[session.InstanceId];
                                gl.push_back(nc->GetGUID());

                                phaseCreatureFound = true;

                                for (auto const& pd3 : session.Players)
                                    if (Player* p3 = ObjectAccessor::FindPlayer(pd3.PlayerGuid))
                                        if (p3->GetSession())
                                            ChatHandler(p3->GetSession()).PSendSysMessage(
                                                "{} The echo's master is not finished yet!", CHAT_PREFIX);
                                break;  // Only promote one phase creature per check
                            }
                        }

                        if (!phaseCreatureFound)
                        {
                            // No phase creature found — confirm the boss kill
                            ++session.BossesKilled;
                            for (auto& pd : session.Players)
                                ++pd.BossesKilled;

                            LOG_INFO("module", "Atlas: Boss kill confirmed (entry {}) — progress: {}/{}",
                                ppc.OrigEntry, session.BossesKilled, session.TotalBosses);
                            HandleBossDeath(&session);

                            // Check completion
                            if (session.IsActive() && session.TotalBosses > 0
                                && session.BossesKilled >= session.TotalBosses)
                            {
                                session.State   = SessionState::Completed;
                                session.EndTime = GameTime::GetGameTime().count();

                                uint32 delay = (session.RoguelikeRunId != 0)
                                    ? sAtlasConfig->GetRoguelikeTransitionDelay()
                                    : sAtlasConfig->GetCompletionTeleportDelay();

                                for (auto const& pd2 : session.Players)
                                    if (Player* p = ObjectAccessor::FindPlayer(pd2.PlayerGuid))
                                        if (p->GetSession())
                                        {
                                            ChatHandler(p->GetSession()).PSendSysMessage(
                                                "{} {} The timeways return you in |cFFFFFFFF{}|r seconds...",
                                                session.RoguelikeRunId != 0 ? CHAT_PREFIX_ENDLESS : CHAT_PREFIX,
                                                session.RoguelikeRunId != 0
                                                    ? "Echo mended. The next one is already forming."
                                                    : "Echo mended!",
                                                delay);
                                        }
                                break;
                            }
                        }
                    }

                    // Clean up resolved phase checks
                    session.PendingPhaseChecks.erase(
                        std::remove_if(session.PendingPhaseChecks.begin(), session.PendingPhaseChecks.end(),
                            [](PendingPhaseCheck const& p) { return p.Resolved; }),
                        session.PendingPhaseChecks.end());

                    // ---- Sweep for stray creatures (script-spawned, respawned) ----
                    Map* m = ref->GetMap();
                    if (m && m->IsDungeon())
                    {
                        uint32 npcEntry = sAtlasConfig->GetNpcEntry();

                        // Collect matches first: DespawnOrUnsummon() mutates the map's
                        // creature-by-spawnId container, so we must not call it while
                        // still holding a live iterator over that same container
                        // (was causing use-after-free segfaults on the next iteration).
                        std::vector<Creature*> strays;
                        auto const& dbStore = static_cast<InstanceMap*>(m)->GetCreatureBySpawnIdStore();
                        for (auto const& pair : dbStore)
                        {
                            Creature* stray = pair.second;
                            if (stray && stray->IsInWorld() && stray->IsAlive()
                                && stray->GetEntry() != npcEntry
                                && !stray->IsPet() && !stray->IsGuardian() && !stray->IsTotem()
                                && ourGuids.count(stray->GetGUID()) == 0)
                            {
                                strays.push_back(stray);
                            }
                        }

                        for (Creature* stray : strays)
                        {
                            stray->SetRespawnTime(7 * DAY);
                            stray->DespawnOrUnsummon();
                        }
                    }
                }

                // ---- Auto-rez when out of combat ----
                if (session.IsActive() && !session.IsGroupInCombat())
                {
                    for (auto const& pd : session.Players)
                    {
                        Player* p = ObjectAccessor::FindPlayer(pd.PlayerGuid);
                        if (p && !p->IsAlive() && p->GetMapId() == session.MapId)
                        {
                            p->RemoveFlag(PLAYER_FIELD_BYTES, PLAYER_FIELD_BYTE_NO_RELEASE_WINDOW);
                            p->ResurrectPlayer(1.0f);
                            p->SpawnCorpseBones();
                            p->TeleportTo(session.MapId,
                                session.EntrancePos.GetPositionX(),
                                session.EntrancePos.GetPositionY(),
                                session.EntrancePos.GetPositionZ(),
                                session.EntrancePos.GetOrientation());
                            ChatHandler(p->GetSession()).PSendSysMessage(
                                "{} The echo returns you to its entrance. Go on.", CHAT_PREFIX);
                        }
                    }
                }
            }

            // ---- Time limit ----
            if (session.TimeLimit > 0 && session.State == SessionState::InProgress)
            {
                uint64 elapsed = GameTime::GetGameTime().count() - session.StartTime;
                if (elapsed >= session.TimeLimit)
                {
                    session.State = SessionState::Failed;
                    toEnd.emplace_back(sid, false);
                    for (auto const& pd : session.Players)
                        if (Player* p = ObjectAccessor::FindPlayer(pd.PlayerGuid))
                            ChatHandler(p->GetSession()).PSendSysMessage(
                                "{} Time has run out. The echo collapses.", CHAT_PREFIX);
                    continue;
                }
            }

            // ---- Completed → teleport delay ----
            if (session.State == SessionState::Completed)
            {
                uint32 delay = (session.RoguelikeRunId != 0)
                    ? sAtlasConfig->GetRoguelikeTransitionDelay()
                    : sAtlasConfig->GetCompletionTeleportDelay();
                uint64 elapsed = GameTime::GetGameTime().count() - session.EndTime;

                // ---- Roguelike countdown announcements ----
                if (session.RoguelikeRunId != 0 && elapsed < delay)
                {
                    uint32 remaining = static_cast<uint32>(delay - elapsed);
                    static const uint32 announceAt[] = {25, 20, 15, 10, 5, 4, 3, 2, 1};
                    for (uint32 sec : announceAt)
                    {
                        if (remaining == sec)
                        {
                            std::string const countdown = Acore::StringFormat(
                                "{} The next echo opens in |cFFFFFFFF{}|r second{}...",
                                CHAT_PREFIX_ENDLESS, remaining, remaining != 1 ? "s" : "");
                            for (auto const& pd3 : session.Players)
                                if (Player* p3 = ObjectAccessor::FindPlayer(pd3.PlayerGuid))
                                    if (p3->GetSession())
                                        ChatHandler(p3->GetSession()).SendSysMessage(countdown);
                            break;
                        }
                    }
                }

                if (elapsed >= delay)
                {
                    if (session.RoguelikeRunId != 0)
                        roguelikeCompleted.push_back({session.RoguelikeRunId, sid});
                    else
                        toEnd.emplace_back(sid, true);
                    continue;
                }
            }

            // ---- Failed cleanup ----
            if (session.State == SessionState::Failed)
            {
                // Roguelike sessions: wipe is handled by RoguelikeMgr::OnPartyWipe
                if (session.RoguelikeRunId != 0)
                    continue;

                if (session.EndTime == 0)
                    session.EndTime = GameTime::GetGameTime().count();
                else if (GameTime::GetGameTime().count() - session.EndTime >= 2)
                {
                    toEnd.emplace_back(sid, false);
                    continue;
                }
            }

            // ---- Abandoned detection ----
            if (session.IsActive()
                && (GameTime::GetGameTime().count() - session.StartTime) >= 15)
            {
                bool anyone = false;
                for (auto const& pd : session.Players)
                {
                    Player* p = ObjectAccessor::FindPlayer(pd.PlayerGuid);
                    if (p && p->GetMapId() == session.MapId) { anyone = true; break; }
                }
                if (!anyone)
                {
                    LOG_INFO("module", "Atlas: Session {} abandoned — no players on map {} after grace period",
                        sid, session.MapId);
                    session.State = SessionState::Abandoned;
                    toEnd.emplace_back(sid, false);
                }
            }
        }
    } // release lock

    for (auto const& [id, ok] : toEnd)
        EndSession(id, ok);

    // Process roguelike completions outside session lock
    for (auto const& [runId, sessId] : roguelikeCompleted)
        sRoguelikeMgr->OnDungeonCompleted(runId, sessId);

    {
        std::lock_guard<std::mutex> lock(_cooldownMutex);
        time_t now = GameTime::GetGameTime().count();
        for (auto it = _cooldowns.begin(); it != _cooldowns.end(); )
            (now >= static_cast<time_t>(it->second)) ? it = _cooldowns.erase(it) : ++it;
    }
}

std::string AtlasMgr::GetSessionStatusString(Session const* s) const
{
    if (!s) return "No session";
    static char const* names[] = { "None","Preparing","InProgress","BossPhase","Completed","Failed","Abandoned" };
    return Acore::StringFormat("Session {} — {}, Mobs {}/{}, Bosses {}/{}, Band {}-{}",
        s->SessionId, names[static_cast<int>(s->State)],
        s->MobsKilled, s->TotalMobs, s->BossesKilled, s->TotalBosses,
        s->LevelBandMin, s->LevelBandMax);
}

} // namespace Atlas
