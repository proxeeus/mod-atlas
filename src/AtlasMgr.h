/*
 * mod-atlas — AtlasMgr.h
 * Central session manager singleton.
 */

#ifndef ATLAS_MGR_H
#define ATLAS_MGR_H

#include "AtlasTypes.h"
#include "AtlasConfig.h"
#include <mutex>
#include <map>
#include <string>
#include <unordered_map>
#include <vector>

class Player;
class Group;
class Creature;
class Map;
class InstanceMap;

namespace Atlas
{

class AtlasMgr
{
    AtlasMgr();
    ~AtlasMgr();

public:
    static AtlasMgr* Instance();

    void Initialize();
    void LoadFromDB();
    void LoadDungeonGeometry();   // also used by `.atlas reload`

    // Session lifecycle
    Session*  CreateSession(Player* leader, uint32 difficultyId, uint32 themeId, uint32 mapId, bool scaleToParty = true);
    Session*  GetSession(uint32 sessionId);
    Session*  GetSessionByInstance(uint32 instanceId);
    Session*  GetSessionByPlayer(ObjectGuid playerGuid);
    void      EndSession(uint32 sessionId, bool success);
    void      AbandonSession(uint32 sessionId);
    void      CleanupRoguelikeSession(uint32 sessionId, bool success);

    // Session ops
    bool StartDungeon(Session* session);
    bool ReleaseBindsBeforeRun(Session* session);
    void ReleaseInstancesLeftOpen();
    void ReleaseRunInstanceBinds(Session const& session);
    bool TeleportPartyIn(Session* session);
    void TeleportPartyOut(Session* session);
    void HandlePlayerDeath(Player* player, Session* session);
    void HandleCreatureDeath(Creature* creature, Session* session);
    void HandleBossDeath(Session* session);
    void OnCreatureDeathHook(Creature* creature);

    // Dungeon population
    void ClearDungeonCreatures(InstanceMap* map);
    void OpenAllDoors(InstanceMap* map);
    void PopulateDungeon(Session* session, InstanceMap* map);

    // Rewards
    void DistributeRewards(Session* session);
    void FillCreatureLoot(Creature* creature, Session* session, bool isBoss);

    // Cooldowns
    bool   IsOnCooldown(ObjectGuid playerGuid) const;
    void   SetCooldown(ObjectGuid playerGuid);
    void   ClearCooldown(ObjectGuid playerGuid);
    uint32 GetRemainingCooldown(ObjectGuid playerGuid) const;

    // Stats & leaderboard
    PlayerStats GetPlayerStats(ObjectGuid guid) const;
    void        LoadAllPlayerStats();
    void        SavePlayerStats(uint32 guidLow);
    void        UpdatePlayerStatsFromSession(Session const& session, bool success);
    void        SaveLeaderboardEntry(Session const& session);
    std::vector<LeaderboardEntry> GetLeaderboard(uint32 mapId, uint32 difficultyId, uint32 limit = 10) const;
    std::vector<LeaderboardEntry> GetOverallLeaderboard(uint32 limit = 10) const;

    void Update(uint32 diff);

    uint32 GetActiveSessionCount() const { return static_cast<uint32>(_activeSessions.size()); }

    // Lock-free "is any session running?" check for hot paths (every damage
    // event on the server reaches the unit script). Kept in sync under
    // _sessionMutex whenever _activeSessions changes.
    static bool HasActiveSessions();
    bool   CanCreateNewSession()   const;

    // Entry checks run before a dungeon is picked, so a run never rolls one its
    // party is turned away from. They mirror MapMgr::PlayerCannotEnter and say
    // nothing to the players. GatherParty = the leader plus in-world group members.
    static std::vector<Player*> GatherParty(Player* leader);
    // Brackets follow the lowest level in the party, so a levelling character is
    // never pulled into a dungeon above it.
    static uint8                GetPartyMinLevel(std::vector<Player*> const& party);
    static bool                 CanPartyEnter(std::vector<Player*> const& party, uint32 mapId);
    static std::vector<DungeonInfo const*> FilterEnterable(std::vector<DungeonInfo const*> const& dungeons,
                                                           std::vector<Player*> const& party);
    // First party member who may not enter another new dungeon this hour
    // (AccountInstancesPerHour), or nullptr.
    static Player*              FindAtInstanceCap(std::vector<Player*> const& party);
    static std::string          InstanceCapNotice(Player const* capped);

    // Env damage scaling
    bool  IsSessionCreature(ObjectGuid playerGuid, ObjectGuid creatureGuid);
    bool  IsSessionBoss(ObjectGuid playerGuid, ObjectGuid creatureGuid);
    float GetEnvironmentalDamageScale(ObjectGuid playerGuid);
    float GetSessionCreatureDamageScale(ObjectGuid playerGuid, ObjectGuid creatureGuid);
    float GetOwnedCreatureDamageScale(ObjectGuid playerGuid, Creature const* creature);

    Position    GetDungeonEntrance(uint32 mapId);
    std::string GetSessionStatusString(Session const* session) const;
    uint8       ComputeEffectiveLevel(Player* leader) const;

    void   DistributeRoguelikeRewards(uint32 tier, uint8 effectiveLevel,
                                       std::vector<ObjectGuid> const& playerGuids);

    // -----------------------------------------------------------------------
    // Creature level ownership.
    //
    // Anything that re-runs Creature::SelectLevel() (respawn, UpdateEntry) or
    // calls SetLevel() from another module reverts an Atlas creature to
    // its creature_template level, undoing the level band for the run. We record
    // what was assigned so it can be re-asserted. Deliberately independent of
    // which module caused the drift.
    // -----------------------------------------------------------------------
    void RegisterOwnedCreature(ObjectGuid guid, uint8 level, uint32 maxHealth, uint32 instanceId);
    bool GetOwnedCreature(ObjectGuid guid, uint8& level, uint32& maxHealth) const;
    void ForgetOwnedCreature(ObjectGuid guid);
    void ForgetOwnedCreaturesForInstance(uint32 instanceId);
    static bool HasOwnedCreatures();

    // Level + stat scaling for a creature summoned by an Atlas mob.
    void ScaleSummonedCreature(Creature* creature, Session* session);

private:
    // Cached reads (see LoadDungeonGeometry) and the world-DB builders behind them.
    std::vector<SpawnPoint> GetSpawnPointsForMap(uint32 mapId);
    std::vector<SpawnPoint> BuildSpawnPointsForMap(uint32 mapId);
    Position                QueryDungeonEntrance(uint32 mapId);
    uint32 SelectCreatureForTheme(Theme const* theme, bool isBoss, Session const* session = nullptr);
    uint32 SelectDungeonBoss(Theme const* theme);

    void   GiveGoldReward(Player* player, uint32 amount);
    void   GiveItemReward(Player* player, uint8 rewardLevel, uint8 quality);
    void   MailItemReward(Player* player, uint8 level, uint8 quality,
                          std::string const& subject, std::string const& body);
    void   GiveKillXP(Session* session, bool isBoss, bool isElite);
    uint32 SelectRewardItem(uint8 level, uint8 quality, uint32 playerClass);
    uint32 SelectLootItem(uint8 level, uint8 minQuality, uint8 maxQuality, bool equipmentOnly = false, uint32 playerClass = 0);

    float ComputeLevelDamageScale(Session const& session, Creature const* creature) const;
    float CalculateHealthMultiplier(Session const* session) const;
    float CalculateDamageMultiplier(Session const* session) const;
    ClassLevelStatEntry const* GetBaseStatsForLevel(uint8 unitClass, uint8 level) const;

    void LoadCreaturePools();
    void LoadDungeonBossPool();
    void LoadClassLevelStats();
    void LoadRewardItems();
    void LoadLootPool();
    void LoadLeaderboard();
    void CleanupSession(Session& session);

    std::unordered_map<uint32, Session>      _activeSessions;
    std::unordered_map<uint32, uint32>       _instanceToSession;
    std::unordered_map<ObjectGuid, uint32>   _playerToSession;
    uint32 _nextSessionId = 1;
    mutable std::mutex _sessionMutex;

    std::unordered_map<ObjectGuid, uint64>   _cooldowns;
    mutable std::mutex _cooldownMutex;

    std::unordered_map<uint32, PlayerStats>  _playerStats;
    mutable std::mutex _statsMutex;

    std::unordered_map<uint32, std::vector<CreaturePoolEntry>> _creaturesByType;
    std::unordered_map<uint32, std::vector<CreaturePoolEntry>> _bossCreatures;
    std::unordered_map<uint32, std::vector<CreaturePoolEntry>> _dungeonBossPool;

    std::map<std::pair<uint8,uint8>, ClassLevelStatEntry> _classLevelStats;
    std::unordered_map<uint32, std::vector<ObjectGuid>> _instanceCreatureGuids;

    struct OwnedCreature
    {
        uint8  Level      = 0;
        uint32 MaxHealth  = 0;
        uint32 InstanceId = 0;
    };
    std::unordered_map<ObjectGuid, OwnedCreature> _ownedCreatures;
    mutable std::mutex _ownedMutex;

    std::vector<RewardItem> _rewardItems;
    std::vector<LootPoolItem> _lootPool;

    std::vector<LeaderboardEntry> _leaderboard;
    mutable std::mutex _leaderboardMutex;

    std::unordered_map<uint32, Position>                _entrances;
    std::unordered_map<uint32, std::vector<SpawnPoint>> _spawnPoints;
    mutable std::mutex _geometryMutex;

    uint32 _updateTimer = 0;
    static constexpr uint32 UPDATE_INTERVAL = 1000;
};

} // namespace Atlas

#define sAtlasMgr Atlas::AtlasMgr::Instance()

#endif
