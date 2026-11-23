/*
 * mod-atlas — Shared data structures
 * AGPL v3
 */

#ifndef ATLAS_TYPES_H
#define ATLAS_TYPES_H

#include "Define.h"
#include "ObjectGuid.h"
#include "Position.h"
#include "RunRng.h"
#include "AtlasAffix.h"
#include <string>
#include <algorithm>
#include <vector>

class Player;
class Creature;
class Map;
class InstanceMap;

namespace Atlas
{

constexpr uint32 MAX_DIFFICULTIES      = 10;
constexpr uint32 MAX_THEMES            = 20;
constexpr uint32 MAX_PARTY_SIZE        = 5;

// Player-facing chat prefixes (Keepers of Time / bronze dragonflight frame).
constexpr char const* CHAT_PREFIX         = "|cFFCD7F32[Keepers of Time]|r";
constexpr char const* CHAT_PREFIX_ENDLESS = "|cFFCD7F32[Endless Timeways]|r";

enum class SessionState : uint8
{
    None       = 0,
    Preparing,
    InProgress,
    BossPhase,
    Completed,
    Failed,
    Abandoned
};

struct DifficultyTier
{
    uint32      Id               = 0;
    std::string Name;
    uint8       MinLevel         = 1;
    uint8       MaxLevel         = 80;
    float       HealthMultiplier = 1.0f;
    float       DamageMultiplier = 1.0f;
    float       RewardMultiplier = 1.0f;
    float       MobCountMultiplier = 1.0f;

    bool IsValidForLevel(uint8 level) const { return level >= MinLevel; }
    bool IsOnLevelFor(uint8 level) const { return level >= MinLevel && level <= MaxLevel; }
};

// Groups creature types for themed spawns; -1 = any type
// Where a theme's creatures come from: config themes pick by creature type;
// database themes (atlas_theme) are a dungeon's own inhabitants or a pool.
enum class ThemeKind : uint8
{
    Type = 0,
    Native,
    Pool
};

struct Theme
{
    uint32                  Id = 0;
    std::string             Name;
    std::vector<uint32>     CreatureTypes;
    ThemeKind               Kind = ThemeKind::Type;
    std::string             Description;
    int32                   SortOrder = 100;

    bool IsRandom() const
    {
        return CreatureTypes.size() == 1 && CreatureTypes[0] == uint32(-1);
    }
};

struct DungeonInfo
{
    uint32      MapId       = 0;
    std::string Name;
    uint8       MinLevel    = 1;
    uint8       MaxLevel    = 80;
    Position    EntrancePos;
    bool        IsAvailable = true;
};

struct SpawnedCreature
{
    ObjectGuid  Guid;
    uint32      Entry      = 0;
    bool        IsElite    = false;
    bool        IsBoss     = false;
    bool        IsRare     = false;
    bool        IsDead     = false;
    bool        LootFilled = false;   // true once FillCreatureLoot has run post-death
    bool        KillCredited = false; // true once kill XP/count has been awarded
    uint16      PackId     = 0;       // the layout anchor it stood at, 1-based; 0 = boss or add
    uint8       Force      = 0;       // its weight on the master's bar (AtlasForces.h)
};

struct PendingPhaseCheck
{
    Position    DeathPos;
    uint64      DeathTime  = 0;
    uint32      OrigEntry  = 0;
    bool        Resolved   = false;
};

struct PlayerSessionData
{
    ObjectGuid  PlayerGuid;
    Position    ReturnPosition;
    uint32      ReturnMapId  = 0;
    uint32      MobsKilled   = 0;
    uint32      BossesKilled = 0;
    uint32      Deaths       = 0;
};

struct Session
{
    uint32          SessionId    = 0;
    ObjectGuid      LeaderGuid;
    SessionState    State        = SessionState::None;

    uint32  DifficultyId    = 0;
    uint32  ThemeId         = 0;
    uint32  MapId           = 0;
    uint32  InstanceId      = 0;
    bool    ScaleToParty    = true;
    uint32  RoguelikeRunId  = 0;  // 0 = standalone, >0 = roguelike

    // Seed of the run's layout choices (creatures, elites, rare, boss), logged
    // with the run so it can be replayed (DESIGN C8). Rng draws from it.
    uint64  Seed = 0;
    RunRng  Rng;

    // Map tier (DESIGN §11), 1 for echoes; drives tier growth in the scaling.
    // Endless floors use their depth curve instead.
    uint32  Tier = 1;

    // Affixes (PLAN 1.2), set when the run is created and folded once into
    // Effects. GroundTimersMs: per Effects.Ground entry, ms until it fires.
    std::vector<ActiveAffix> Affixes;
    AffixEffects             Effects;
    std::vector<int32>       GroundTimersMs;

    uint8   EffectiveLevel = 1;
    uint8   LevelBandMin   = 1;
    uint8   LevelBandMax   = 80;

    uint64  StartTime = 0;
    uint64  EndTime   = 0;
    uint32  TimeLimit = 0;

    std::vector<PlayerSessionData>  Players;
    std::vector<SpawnedCreature>    SpawnedCreatures;
    std::vector<PendingPhaseCheck>  PendingPhaseChecks;
    std::vector<uint32>             RecentCreatureEntries;

    uint32  TotalMobs   = 0;
    uint32  MobsKilled  = 0;
    uint32  TotalBosses = 0;
    uint32  BossesKilled = 0;
    uint32  Wipes       = 0;

    Position EntrancePos;

    // The master's arrival (AtlasForces.h): slain forces fill a bar; at the
    // threshold the master steps out of the timeways beside the party.
    struct PendingBoss
    {
        uint32 Entry   = 0;
        bool   Touched = false;
        bool   Scourge = false;
    };
    std::vector<PendingBoss> PendingBosses;   // chosen at populate, summoned at the threshold
    uint32  ForcesTotal     = 0;
    uint32  ForcesThreshold = 0;              // 0: masters wait at their own spots
    uint32  ForcesKilled    = 0;
    uint8   ForcesStep      = 0;              // quarter of the bar last announced
    bool    BossCalled      = false;
    uint64  HudStamp        = 0;              // the run HUD last pushed (changes trigger a push)

    // Map runs (PLAN 1.4): opened by placing an Echo Sigil in the Timeways
    // Lens. Entering through its rift is free the first time for each member;
    // every later entry (and every wipe) spends one of PortalsLeft.
    bool        IsMapRun     = false;
    uint8       MapRarity    = 0;
    uint32      PortalsLeft  = 0;
    ObjectGuid  RiftGuid;                   // the entry rift beside the Lens
    uint64      LastOccupied = 0;           // last time someone was inside
    std::vector<ObjectGuid> Entered;        // went in at least once
    std::vector<ObjectGuid> CacheClaimed;   // rewarded: opened the cache, or a bot paid at the kill
    std::vector<ObjectGuid> FirstClears;    // players whose account first mended this dungeon at this tier
    std::vector<bool>       PacksDone;      // per layout anchor: cleared and its sigil chance rolled

    static bool Contains(std::vector<ObjectGuid> const& list, ObjectGuid guid)
    {
        return std::find(list.begin(), list.end(), guid) != list.end();
    }

    bool IsSessionCreature(ObjectGuid guid) const
    {
        for (auto const& sc : SpawnedCreatures)
            if (sc.Guid == guid) return true;
        return false;
    }

    bool IsActive() const
    {
        return State == SessionState::InProgress
            || State == SessionState::BossPhase
            || State == SessionState::Preparing;
    }

    bool IsComplete() const { return State == SessionState::Completed; }

    bool HasPlayer(ObjectGuid guid) const
    {
        for (auto const& p : Players)
            if (p.PlayerGuid == guid) return true;
        return false;
    }

    PlayerSessionData* GetPlayerData(ObjectGuid guid)
    {
        for (auto& p : Players)
            if (p.PlayerGuid == guid) return &p;
        return nullptr;
    }

    uint32 GetAlivePlayerCount() const;
    bool   IsPartyWiped() const;
    bool   IsGroupInCombat() const;
};

struct CreaturePoolEntry
{
    uint32 Entry    = 0;
    uint32 Type     = 0;
    uint8  MinLevel = 1;
    uint8  MaxLevel = 80;
};

struct RewardItem
{
    uint32 Entry         = 0;
    uint32 MinLevel      = 1;
    uint32 MaxLevel      = 80;
    uint16 ItemLevel     = 0;
    uint8  Quality       = 0;       // 0=Poor .. 4=Epic
    uint32 InventoryType = 0;
    uint32 Class         = 0;       // 2=Weapon, 4=Armor
    uint32 SubClass      = 0;
    int32  AllowableClass = -1;
    int32  AllowableRace  = -1;
};

struct LootPoolItem
{
    uint32 Entry          = 0;
    uint8  MinLevel       = 0;
    uint16 ItemLevel      = 0;
    uint8  Quality        = 0;
    uint8  ItemClass      = 0;
    uint8  SubClass       = 0;
    int32  AllowableClass = -1;
};

struct PlayerStats
{
    ObjectGuid PlayerGuid;
    uint32 TotalRuns       = 0;
    uint32 CompletedRuns   = 0;
    uint32 FailedRuns      = 0;
    uint32 TotalMobsKilled = 0;
    uint32 TotalBossesKilled = 0;
    uint32 TotalDeaths     = 0;
    uint32 FastestClear    = 0;
};

struct LeaderboardEntry
{
    uint32      Id            = 0;
    uint32      Guid          = 0;
    std::string CharName;
    uint32      MapId         = 0;
    uint32      DifficultyId  = 0;
    uint32      ClearTime     = 0;
    uint8       PartySize     = 1;
    bool        Scaled        = false;
    uint8       EffectiveLevel = 0;
    uint32      MobsKilled    = 0;
    uint32      BossesKilled  = 0;
    uint32      Deaths        = 0;
};

} // namespace Atlas

#endif // ATLAS_TYPES_H
