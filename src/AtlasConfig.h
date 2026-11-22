/*
 * mod-atlas — Config singleton
 * Reads settings from mod_atlas.conf.dist. Thread-safe (loaded on world thread).
 * AGPL v3
 */

#ifndef ATLAS_CONFIG_H
#define ATLAS_CONFIG_H

#include "AtlasTypes.h"
#include "RoguelikeTypes.h"
#include "AtlasScalingMath.h"
#include "AtlasTheme.h"
#include <array>
#include <vector>
#include <unordered_map>
#include <unordered_set>

namespace Atlas
{

class AtlasConfig
{
    AtlasConfig() = default;
    ~AtlasConfig() = default;

public:
    static AtlasConfig* Instance();

    void LoadConfig(bool reload = false);

    // --- Core ---
    bool   IsEnabled()        const { return _enabled; }
    bool   IsDebugEnabled()   const { return _debug; }
    uint32 GetNpcEntry()      const { return _npcEntry; }

    // --- Difficulties ---
    std::vector<DifficultyTier> const&      GetDifficulties() const { return _difficulties; }
    DifficultyTier const*                   GetDifficulty(uint32 id) const;
    std::vector<DifficultyTier const*>      GetDifficultiesForLevel(uint8 level) const;

    // --- Themes ---
    std::vector<Theme> const&   GetThemes() const { return _themes; }
    Theme const*                GetTheme(uint32 id) const;

    // --- Dungeons ---
    std::vector<DungeonInfo> const&     GetDungeons() const { return _dungeons; }
    DungeonInfo const*                  GetDungeon(uint32 mapId) const;
    std::vector<DungeonInfo const*>     GetDungeonsForLevel(uint8 minLevel, uint8 maxLevel) const;
    // Lowest level that opens a tier: its own minimum, raised to the first dungeon
    // it can roll (no dungeon may start above the party's lowest level).
    // 255 when the tier has no dungeon at all.
    uint8                               GetTierEntryLevel(DifficultyTier const& tier) const;
    bool                                IsDungeonAllowed(uint32 mapId) const;

    // --- Scaling ---
    uint8 GetLevelBand()          const { return _levelBand; }
    float GetPerPlayerHealthMult() const { return _perPlayerHealth; }
    float GetPerPlayerDamageMult() const { return _perPlayerDamage; }
    float GetSoloMultiplier()     const { return _soloMultiplier; }
    // Role table (DESIGN §9), tier growth, and the cap on spell/heal rescaling.
    RoleMultipliers const& GetRole(CreatureRole role) const { return _roles[static_cast<uint8>(role)]; }
    float GetTierGrowth()         const { return _tierGrowth; }
    float GetSpellScaleMax()      const { return _spellScaleMax; }

    // --- Rewards ---
    uint32 GetBaseGold()     const { return _baseGold; }
    uint32 GetGoldPerMob()   const { return _goldPerMob; }
    uint32 GetGoldPerBoss()  const { return _goldPerBoss; }
    float  GetXPMultiplier() const { return _xpMultiplier; }
    uint32 GetItemChance()   const { return _itemChance; }
    uint32 GetRareChance()   const { return _rareChance; }
    uint32 GetEpicChance()   const { return _epicChance; }
    uint16 GetMaxItemLevel() const { return _maxItemLevel; }
    // Gold settings are amounts at level 80; lower levels get a share in
    // proportion to their level.
    static uint32 ScaleGoldForLevel(uint32 copperAt80, uint8 level)
    {
        return static_cast<uint32>(uint64(copperAt80) * level / 80);
    }

    // --- Dungeon population ---
    uint32 GetBossCount()       const { return _bossCount; }
    uint32 GetEliteChance()     const { return _eliteChance; }
    float  GetAggroRadius()     const { return _aggroRadius; }
    uint32 GetRareSpawnChance() const { return _rareSpawnChance; }
    uint32 GetRecentCreatureEntries() const { return _recentCreatureEntries; }
    // Live creatures a run may spawn at most (pack-size affixes stop there).
    uint32 GetMaxRunCreatures()       const { return _maxRunCreatures; }
    uint32 GetMaxRares()              const { return _maxRares; }

    // --- Population (PLAN 1.3) ---
    float  GetPackRadius()            const { return _packRadius; }
    uint32 GetMaxPackSize()           const { return _maxPackSize; }
    bool   KeepsSmartAI()             const { return _keepSmartAI; }
    uint32 GetScourgeThemeId()        const { return _scourgeThemeId; }
    uint32 GetTouchedAura()           const { return _touchedAura; }
    std::string const& GetTouchedAffixCode() const { return _touchedAffix; }
    // Native / touched / Scourge shares of a corruption level (0-5).
    CorruptionShares GetCorruptionShares(uint8 level) const { return _corruption[std::min<uint8>(level, 5)]; }
    // Themes from the database (atlas_theme), merged into the list by sort order.
    void   SetDatabaseThemes(std::vector<Theme> themes);

    // --- Affixes ---
    bool   IsAffixBotSafeOnly()       const { return _affixBotSafeOnly; }
    float  GetVisualScaleForCreatureType(uint32 type) const;

    // --- Timers ---
    uint32 GetCooldownMinutes()   const { return _cooldownMinutes; }
    bool   IsTimeLimitEnabled()   const { return _timeLimitEnabled; }
    uint32 GetTimeLimitMinutes()  const { return _timeLimitMinutes; }
    uint32 GetMaxConcurrentRuns() const { return _maxConcurrentRuns; }

    // --- Death ---
    bool   ShouldRespawnAtStart()  const { return _respawnAtStart; }
    uint32 GetMaxWipes()           const { return _maxWipes; }

    // --- Completion ---
    uint32 GetCompletionTeleportDelay() const { return _completionTeleportDelay; }
    bool   ShouldAnnounceCompletion()   const { return _announceCompletion; }

    // --- Roguelike Mode ---
    bool   IsRoguelikeEnabled()             const { return _roguelikeEnabled; }
    uint32 GetRoguelikeTransitionDelay()    const { return _roguelikeTransitionDelay; }
    float  GetRoguelikeHpScaling()          const { return _roguelikeHpScaling; }
    float  GetRoguelikeDmgScaling()         const { return _roguelikeDmgScaling; }
    float  GetRoguelikeArmorScaling()       const { return _roguelikeArmorScaling; }
    uint32 GetRoguelikeExpThreshold()       const { return _roguelikeExpThreshold; }
    float  GetRoguelikeExpFactor()          const { return _roguelikeExpFactor; }
    uint32 GetRoguelikeAffixStartTier()     const { return _roguelikeAffixStartTier; }
    uint32 GetRoguelikeSecondAffixTier()    const { return _roguelikeSecondAffixTier; }
    uint32 GetRoguelikeThirdAffixTier()     const { return _roguelikeThirdAffixTier; }
    uint32 GetRoguelikeMaxBuffs()           const { return _roguelikeMaxBuffs; }
    std::vector<RoguelikeBuff> const& GetRoguelikeBuffPool() const { return _roguelikeBuffPool; }
    bool   IsRoguelikeVendorEnabled()       const { return _roguelikeVendorEnabled; }

private:
    void LoadDifficulties();
    void LoadThemes();
    void LoadDungeons();
    void LoadRoguelikeBuffPool();
    void ParseStringList(std::string const& str, std::unordered_set<uint32>& outSet);

    // Core
    bool   _enabled   = true;
    bool   _debug     = false;
    uint32 _npcEntry  = 500000;

    // Data
    std::vector<DifficultyTier>     _difficulties;
    std::vector<Theme>              _themes;
    std::vector<DungeonInfo>        _dungeons;
    std::unordered_set<uint32>      _dungeonWhitelist;
    std::unordered_set<uint32>      _dungeonBlacklist;

    // Scaling
    uint8 _levelBand       = 3;     // creatures must be within ±N levels
    float _perPlayerHealth = 0.25f;
    float _perPlayerDamage = 0.10f;
    float _soloMultiplier  = 0.50f;
    std::array<RoleMultipliers, static_cast<uint8>(CreatureRole::Count)> _roles{};
    float _tierGrowth      = 1.075f;
    float _spellScaleMax   = 20.0f;

    // Rewards
    uint32 _baseGold    = 40000;
    uint32 _goldPerMob  = 800;
    uint32 _goldPerBoss = 40000;
    float  _xpMultiplier = 1.0f;
    uint32 _itemChance  = 80;
    uint32 _rareChance  = 40;
    uint32 _epicChance  = 15;
    uint16 _maxItemLevel = 219;

    // Dungeon
    uint32 _bossCount       = 1;
    uint32 _maxRunCreatures = 450;
    uint32 _maxRares        = 2;
    float  _packRadius      = 8.0f;
    uint32 _maxPackSize     = 6;
    bool   _keepSmartAI     = true;
    uint32 _scourgeThemeId  = 51;
    uint32 _touchedAura     = 10848;
    std::string _touchedAffix = "M04";
    std::array<CorruptionShares, 6> _corruption{};
    std::vector<Theme> _configThemes;
    std::vector<Theme> _databaseThemes;
    bool   _affixBotSafeOnly = true;
    uint32 _eliteChance     = 20;
    float  _aggroRadius     = 15.0f;
    uint32 _rareSpawnChance = 5;
    uint32 _recentCreatureEntries = 12;
    float  _visualScaleBeast = 1.0f;
    float  _visualScaleDragonkin = 0.85f;
    float  _visualScaleDemon = 0.95f;
    float  _visualScaleElemental = 0.90f;
    float  _visualScaleGiant = 0.70f;
    float  _visualScaleUndead = 1.0f;
    float  _visualScaleHumanoid = 1.0f;
    float  _visualScaleMechanical = 0.90f;

    // Timers
    uint32 _cooldownMinutes   = 5;
    bool   _timeLimitEnabled  = false;
    uint32 _timeLimitMinutes  = 30;
    uint32 _maxConcurrentRuns = 20;

    // Death
    bool   _respawnAtStart = true;
    uint32 _maxWipes       = 0;

    // Completion
    uint32 _completionTeleportDelay = 30;
    bool   _announceCompletion      = true;

    // Roguelike
    bool   _roguelikeVendorEnabled = true;
    bool   _roguelikeEnabled          = true;
    uint32 _roguelikeTransitionDelay  = 30;
    float  _roguelikeHpScaling        = 0.10f;
    float  _roguelikeDmgScaling       = 0.08f;
    float  _roguelikeArmorScaling     = 0.05f;
    uint32 _roguelikeExpThreshold     = 5;
    float  _roguelikeExpFactor        = 1.15f;
    uint32 _roguelikeAffixStartTier   = 3;
    uint32 _roguelikeSecondAffixTier  = 7;
    uint32 _roguelikeThirdAffixTier   = 10;
    uint32 _roguelikeMaxBuffs         = 20;
    std::vector<RoguelikeBuff> _roguelikeBuffPool;
};

} // namespace Atlas

#define sAtlasConfig Atlas::AtlasConfig::Instance()

#endif // ATLAS_CONFIG_H
