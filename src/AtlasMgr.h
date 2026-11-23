/*
 * mod-atlas — AtlasMgr.h
 * Central session manager singleton.
 */

#ifndef ATLAS_MGR_H
#define ATLAS_MGR_H

#include "AtlasTypes.h"
#include "AtlasConfig.h"
#include "StockLayoutProvider.h"
#include "AtlasMapItem.h"
#include "AtlasLoot.h"
#include <atomic>
#include <memory>
#include <mutex>
#include <array>
#include <map>
#include <string_view>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

class Player;
class Group;
class Creature;
class Unit;
struct CreatureTemplate;
class Map;
class InstanceMap;
class Item;
class GameObject;

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
    // seed 0 = the leader's pending GM seed if any, else a fresh one.
    Session*  CreateSession(Player* leader, uint32 difficultyId, uint32 themeId, uint32 mapId, bool scaleToParty = true,
                            uint64 seed = 0);
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

    // GM replay (`.atlas seed`): the next run this character starts uses this seed.
    void   SetPendingSeed(ObjectGuid guid, uint64 seed);
    uint64 TakePendingSeed(ObjectGuid guid);

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
    float GetEnvironmentalDamageScale(ObjectGuid playerGuid);

    Position    GetDungeonEntrance(uint32 mapId);
    std::shared_ptr<Layout const> GetLayout(uint32 mapId) const;
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
    // -----------------------------------------------------------------------
    // Role scaling (AtlasScaling.cpp, DESIGN §9; math in AtlasScalingMath.h).
    // -----------------------------------------------------------------------
    struct RoleStats
    {
        uint8  Level       = 0;
        uint32 MaxHealth   = 1;
        float  WeaponMin   = 1.0f;
        float  WeaponMax   = 1.0f;
        float  AttackPower = 0.0f;
        uint32 Armor       = 0;
        float  SpellScale  = 1.0f;   // the creature's spell damage x this
        float  HealScale   = 1.0f;   // its heals x this
    };

    // What scaling needs from a session, copied so creatures can be scaled
    // without holding the session lock (summoning re-enters it, see
    // OwnSpawnScope).
    struct ScalingContext
    {
        uint8        Level          = 1;
        uint32       Tier           = 1;
        uint32       DifficultyId   = 0;
        uint32       PartySize      = 1;
        uint32       RoguelikeRunId = 0;
        uint32       InstanceId     = 0;
        uint32       ThemeId        = 0;
        std::vector<ActiveAffix> Affixes;
        AffixEffects Effects;
    };
    static ScalingContext ContextOf(Session const& session);
    bool      GetScalingContext(uint32 instanceId, ScalingContext& out);

    // Per creature, the run's affixes as they apply to it (DESIGN §10), baked
    // at spawn so the damage hooks need one lookup.
    struct CreatureAffixMods
    {
        float  MeleeDealt    = 1.0f;
        float  PhysTaken     = 1.0f;
        float  SpellTaken    = 1.0f;
        uint32 ExtraHitSpell = 0;
        float  ExtraHitPct   = 0.0f;
        uint32 ShieldSpell   = 0;
        int32  ShieldAmount  = 0;
        uint32 BurstSpell    = 0;
        int32  BurstAmount   = 0;
        uint32 BurstDelayMs  = 0;
        uint32 PoolSpell     = 0;
        int32  PoolTick      = 0;
        uint32 SplitCount    = 0;
        uint32 AddsPerWave   = 0;
        uint8  WavesDone     = 0;
        std::array<uint32, 3> Markers{};   // visual-only marker auras, re-applied when they fall off
    };

    // Tier growth (or the Endless depth curve) x the difficulty's multiplier x
    // the party factor (1.0 for a party of five).
    float     SessionHealthFactor(ScalingContext const& ctx) const;
    float     SessionDamageFactor(ScalingContext const& ctx) const;
    float     SessionHealthFactor(Session const& session) const { return SessionHealthFactor(ContextOf(session)); }
    float     SessionDamageFactor(Session const& session) const { return SessionDamageFactor(ContextOf(session)); }
    RoleStats ComputeRoleStats(ScalingContext const& ctx, CreatureTemplate const* tmpl, CreatureRole role,
                               float affixHealth = 1.0f, float affixDamage = 1.0f) const;
    static void ApplyRoleStats(Creature* creature, RoleStats const& stats, bool keepHealthPct);
    // Level, role stats, the run's affixes and ownership in one step, for
    // anything a run spawns.
    void      ScaleCreature(Creature* creature, ScalingContext const& ctx, CreatureRole role);
    // The same with affixes of the creature's own on top of the run's (a
    // rare's modifiers, the plague's touch).
    void      ScaleCreature(Creature* creature, ScalingContext const& ctx, CreatureRole role,
                            std::vector<ActiveAffix> const& ownAffixes);
    // One melee hit of a warrior-class trash creature of the run (affix damage
    // that follows the run's strength is a share of it).
    float     ReferenceTrashHit(ScalingContext const& ctx) const;
    // `.atlas scaling`: the role table at a level, tier, party size and difficulty.
    std::vector<std::string> DescribeRoleScaling(uint8 level, uint32 tier, uint32 partySize,
                                                 uint32 difficultyId) const;

    void RegisterOwnedCreature(ObjectGuid guid, RoleStats const& stats, CreatureAffixMods const& mods,
                               uint32 instanceId);
    // Mods and spell scale of an owned creature in one lookup (damage hooks).
    bool GetOwnedCombat(ObjectGuid guid, CreatureAffixMods& mods, float& spellScale) const;
    bool GetOwnedCreature(ObjectGuid guid, uint8& level, uint32& maxHealth,
        std::array<uint32, 3>* markers = nullptr) const;
    // For the damage and heal hooks; negative when the creature isn't owned.
    float GetOwnedSpellScale(ObjectGuid guid) const;
    float GetOwnedHealScale(ObjectGuid guid) const;
    // After something reset an owned creature (SelectLevel, another module's
    // SetLevel): its level and role stats again.
    bool  ReapplyOwnedStats(Creature* creature, bool keepHealthPct);
    void ForgetOwnedCreature(ObjectGuid guid);
    void ForgetOwnedCreaturesForInstance(uint32 instanceId);
    static bool HasOwnedCreatures();

    // Level + stat scaling for a creature summoned by an Atlas mob.
    void ScaleSummonedCreature(Creature* creature, Session* session);

    // -----------------------------------------------------------------------
    // Themes, pools and curation (AtlasPopulation.cpp, DESIGN §8).
    // -----------------------------------------------------------------------
    enum class CurationStatus : uint8
    {
        Untested = 0,
        Allowed,
        Blocked
    };
    static char const* CurationName(CurationStatus status);
    static CurationStatus ParseCuration(std::string_view name, bool& ok);

    void           LoadThemes();   // atlas_theme, atlas_theme_creature, atlas_creature_curation
    CurationStatus GetCuration(uint32 entry, std::string* note = nullptr) const;
    // Memory and the world DB (`.atlas curate`).
    void           SetCuration(uint32 entry, CurationStatus status, std::string const& note);
    // Which themes and pack roles list an entry ("51:melee 51:leader").
    std::string    DescribePoolMembership(uint32 entry) const;
    // SmartAI creatures keep their own AI (Atlas.Population.KeepSmartAI).
    static bool    KeepsStockAI(Creature const* creature);
    // A creature spawned as a run would spawn it, at a GM's side (`.atlas curate spawn`).
    Creature*      SpawnCurationSample(Player* gm, uint32 entry, CreatureRole role);

    // -----------------------------------------------------------------------
    // Maps: Echo Sigils, the Timeways Lens, its rifts and the Keepers' cache
    // (AtlasMaps.cpp, PLAN 1.4, DESIGN §7 and §11).
    // -----------------------------------------------------------------------
    // atlas_map_item (pruned of sigils that no longer exist) and atlas_map_grant.
    void        LoadMapItems();
    // A sigil's data; one without a row (lost to a crash) gets a plain echo
    // from its entry and guid.
    MapItemData GetMapItemData(Item const* item) const;
    static std::vector<Item*> FindMapItems(Player* player);
    MapItemData RollMapItem(uint8 tier, MapRarity rarity, RunRng& rng) const;
    // Queued like every reward (DeliverPendingRewards).
    void        GiveMapItem(Player* player, MapItemData data);
    // "Tier 3 Gundrak, a timeline held by mortal warbands: Brutal, of Grief".
    // plain: no colour codes, for gossip options on the parchment frame.
    std::string DescribeMapItem(MapItemData const& data, bool withAffixText, bool plain = false) const;
    bool        HasStarterSigils(ObjectGuid guid) const;
    void        GrantStarterSigils(Player* player);

    // The Lens: places a sigil, opens the run and its rift. False with a
    // reason for the player when it can't; the sigil is kept then.
    bool        OpenMapRun(Player* player, GameObject* lens, ObjectGuid itemGuid, std::string& error);
    // The open run of this player's, for the Lens ("" when none).
    std::string DescribeMapRun(ObjectGuid playerGuid) const;
    void        CloseMapRun(Player* player);
    // Calls the rift back beside the Lens when it is gone (a temporary
    // gameobject does not survive its grid unloading while everyone is inside).
    void        EnsureRift(Player* player, GameObject* lens);
    void        UseEntryRift(Player* player, GameObject* rift);
    void        UseExitRift(Player* player);
    void        UseCache(Player* player, GameObject* cache);
    // Rifts and caches despawn once their run is gone (their AI asks).
    bool        IsMapObjectLive(GameObject const* go) const;
    // Gameobjects inside a run's instance are summoned from that map's own
    // update (OnMapUpdate), never from the world thread.
    void        RequestMapSpawn(uint32 instanceId, uint32 goEntry, Position const& pos, uint32 lifetimeSec);
    void        ProcessMapSpawns(Map* map);
    // Has a client: bots have no socket ("headless" sessions).
    static bool IsRealPlayer(Player const* player);

    // The run's affix quantity and rarity bonuses (%), capped by config.
    static uint32 RunQuantity(Session const& session);
    static uint32 RunRarity(Session const& session);
    // atlas_loot_tier (also `.atlas reload`), and `.atlas loot`.
    void        LoadLootTiers();
    LootTier    GetLootTier(uint8 tier) const;
    std::vector<std::string> DescribeLootTier(uint8 tier, Player const* player) const;

    // -----------------------------------------------------------------------
    // Affixes in play (AtlasAffixEffects.cpp).
    // -----------------------------------------------------------------------
    void SetSessionAffixes(Session* session, std::vector<ActiveAffix> affixes);
    // From AtlasCreatureAI: shield on engaging, death effects, boss waves.
    void OnRunCreatureEngaged(Creature* creature);
    void OnRunCreatureDied(Creature* creature);
    void OnRunCreatureDamaged(Creature* creature, uint32 damage);
    // From the melee hook: an affix's extra elemental hit, cast just after.
    static void QueueExtraHit(Unit* attacker, Unit* target, uint32 spellId, int32 amount);

    // While set, the module is summoning a creature it scales itself; the
    // add-to-world hook stays out of it. That hook takes the session lock,
    // so this also keeps summons made under that lock from deadlocking.
    class OwnSpawnScope
    {
    public:
        OwnSpawnScope();
        ~OwnSpawnScope();
        OwnSpawnScope(OwnSpawnScope const&) = delete;
        OwnSpawnScope& operator=(OwnSpawnScope const&) = delete;
    };
    static bool IsSpawningOwn();

private:
    // Affixes in play (AtlasAffixEffects.cpp).
    struct GroundRequest
    {
        ObjectGuid Player;
        uint32     InstanceId = 0;
        uint32     SpellId    = 0;
        int32      Amount     = 0;
        uint8      Level      = 1;
    };
    void      TickSessionAffixes(Session& session, std::vector<GroundRequest>& out);
    void      RemoveSessionAffixAuras(Session const& session);
    void      ExecuteGroundRequests(std::vector<GroundRequest> const& requests);
    Creature* SpawnAffixCaster(Map* map, Position const& pos, uint8 level, uint32 lifetimeMs);
    // A creature a run spawns mid-fight (splits, a boss's servants): hostile,
    // scaled, with the run's AI, despawning once dead (AtlasPopulation.cpp).
    Creature* SpawnRunAdd(Map* map, uint32 entry, Position const& pos, ScalingContext const& ctx,
                          CreatureRole role, float visualScale, Unit* target);

    // A pool theme's creature for a pack role; falls back to other roles when
    // the pool has none for it, skips blocked entries. 0 when empty.
    uint32 PickPoolEntry(uint32 themeId, PackRole role, RunRng& rng) const;

    uint32 SelectCreatureForTheme(Theme const* theme, bool isBoss, RunRng& rng, Session const* session = nullptr);
    uint32 SelectDungeonBoss(Theme const* theme, RunRng& rng);

    void   GiveGoldReward(Player* player, uint32 amount);
    void   GiveItemReward(Player* player, uint8 rewardLevel, uint8 quality);
public:
    // Reward items wait until their player stands in the world, not in a
    // teleport: rewards come just before the party is sent out (or deeper),
    // and an item stored in that tick only showed in the bags after a relog.
    // Whatever is still waiting at logout goes by mail.
    void   DeliverPendingRewards(ObjectGuid guid);
    void   MailPendingRewards(Player* player);
private:
    struct PendingReward
    {
        uint32 Entry = 0;
        std::shared_ptr<MapItemData> Map;   // a sigil's data, stored with the item
    };
    void   QueueReward(Player* player, PendingReward reward);
    void   StoreOrMailRewardItem(Player* player, PendingReward const& reward);
    void   PersistMapItem(MapItemData const& data);
    void   ForgetMapItem(uint32 itemGuid);

    // Map runs: a player's share of the run's gold and gear, and its sigils.
    void   GiveRunReward(Session const& session, Player* player);
    void   GiveMapCompletionReward(Session const& session, Player* player);
    // A map run's master fell: the cache and a rift home where it fell, bots
    // paid at once.
    void   CompleteMapRun(Session& session, Position const& bossPos);
    uint32 PickMapSource(uint8 tier, RunRng& rng) const;
    void   MailItemReward(Player* player, uint8 level, uint8 quality,
                          std::string const& subject, std::string const& body);
    void   GiveKillXP(Session* session, bool isBoss, bool isElite);
    uint32 SelectRewardItem(uint8 level, uint8 quality, uint32 playerClass);
    uint32 SelectLootItem(uint8 level, uint8 minQuality, uint8 maxQuality, bool equipmentOnly = false, uint32 playerClass = 0);

    void LoadCreaturePools();
    void LoadDungeonBossPool();
    void LoadRewardItems();
    void LoadLootPool();

    // The rewards ladder of map runs (AtlasRewards.cpp, PLAN 1.5).
    void   LoadTierGear();        // item_template: tier gear, PvP excluded
    uint32 SelectTierItem(uint8 tier, uint32 epicShare, Player const* player) const;
    void   GiveTierGear(Player* player, uint8 tier, uint32 count, uint32 epicShare);
    // Map runs (AtlasMaps.cpp): a cleared pack's sigil chance, first clears.
    void   RollPackSigils(Session& session);
    void   MarkFirstClears(Session& session);
    void LoadLeaderboard();
    void CleanupSession(Session& session);

    // Run records (AtlasRecords.cpp): atlas_run_active while a run uses an
    // instance, atlas_run_log once it ends (BACKLOG R4).
    void RecordRunActive(Session const& session, uint32 instanceId, uint32 mapId);
    void LogRun(Session const& session, bool success);

    std::unordered_map<uint32, Session>      _activeSessions;
    std::unordered_map<uint32, uint32>       _instanceToSession;
    std::unordered_map<ObjectGuid, uint32>   _playerToSession;
    uint32 _nextSessionId = 1;
    mutable std::mutex _sessionMutex;

    std::unordered_map<ObjectGuid, uint64>   _cooldowns;
    mutable std::mutex _cooldownMutex;

    std::unordered_map<ObjectGuid, uint64>   _pendingSeeds;
    std::mutex _seedMutex;

    std::unordered_map<uint32, PlayerStats>  _playerStats;
    mutable std::mutex _statsMutex;

    // Ordered: the order candidates are listed in feeds the run RNG (C8).
    std::map<uint32, std::vector<CreaturePoolEntry>> _creaturesByType;
    std::map<uint32, std::vector<CreaturePoolEntry>> _bossCreatures;
    std::map<uint32, std::vector<CreaturePoolEntry>> _dungeonBossPool;

    std::unordered_map<uint32, std::vector<ObjectGuid>> _instanceCreatureGuids;

    struct OwnedCreature
    {
        RoleStats         Stats;
        CreatureAffixMods Mods;
        uint32            InstanceId = 0;
    };
    std::unordered_map<ObjectGuid, OwnedCreature> _ownedCreatures;
    mutable std::mutex _ownedMutex;

    std::unordered_map<ObjectGuid, std::vector<PendingReward>> _pendingRewards;
    std::mutex _pendingRewardsMutex;

    std::unordered_map<uint32, MapItemData> _mapItems;   // by item guid
    std::unordered_set<uint32>              _mapGrants;  // character guids
    mutable std::mutex                      _mapItemsMutex;

    struct MapSpawnRequest
    {
        uint32   Entry       = 0;
        Position Pos;
        uint32   LifetimeSec = 0;
    };
    std::unordered_map<uint32, std::vector<MapSpawnRequest>> _mapSpawns;   // by instance id
    std::mutex            _mapSpawnMutex;
    std::atomic<uint32>   _mapSpawnCount{ 0 };

    std::vector<RewardItem> _rewardItems;
    std::vector<RewardItem> _tierGear;                       // map-run gear, item level 187+
    std::array<LootTier, MAP_MAX_TIER> _lootTiers{};         // atlas_loot_tier
    std::unordered_map<uint64, uint32> _accountProgress;     // account << 32 | map -> tiers mask (_mapItemsMutex)
    std::vector<LootPoolItem> _lootPool;

    std::vector<LeaderboardEntry> _leaderboard;
    mutable std::mutex _leaderboardMutex;

    // Pool themes by pack role, rebuilt by LoadThemes (startup, `.atlas reload`).
    struct ThemePool
    {
        std::array<std::vector<uint32>, static_cast<std::size_t>(PackRole::Count)> ByRole;
    };
    std::map<uint32, ThemePool> _themePools;
    std::unordered_map<uint32, std::pair<CurationStatus, std::string>> _curation;
    mutable std::mutex _themeMutex;

    // Where runs happen (DESIGN §5). V0 runs in stock dungeons; clone layouts
    // come with PLAN stage 2 behind the same LayoutProvider interface.
    StockLayoutProvider _stockLayouts;

    uint32 _updateTimer = 0;
    static constexpr uint32 UPDATE_INTERVAL = 1000;
};

} // namespace Atlas

#define sAtlasMgr Atlas::AtlasMgr::Instance()

#endif
