/*
 * mod-atlas — Roguelike run manager
 * Copyright (C) 2025 — AGPL v3
 */

#ifndef ROGUELIKE_MGR_H
#define ROGUELIKE_MGR_H

#include "RoguelikeTypes.h"
#include <mutex>
#include <set>
#include <string>
#include <unordered_map>
#include <vector>

class Player;
class Group;

namespace Atlas
{

class RoguelikeMgr
{
    RoguelikeMgr();
    ~RoguelikeMgr();

public:
    static RoguelikeMgr* Instance();

    void Initialize();

    // Run lifecycle
    bool StartRun(Player* leader, uint32 difficultyId, uint32 themeId, bool scaleToParty = true);
    void OnDungeonCompleted(uint32 runId, uint32 sessionId);
    void OnPartyWipe(uint32 runId);
    void EndRun(uint32 runId, bool announceResults);
    void AbandonRun(uint32 runId);
    void QuitRun(ObjectGuid playerGuid);

    // Queries
    RoguelikeRun* GetRun(uint32 runId);
    RoguelikeRun* GetRunBySession(uint32 sessionId);
    RoguelikeRun* GetRunByPlayer(ObjectGuid playerGuid);
    uint32        GetRunIdBySession(uint32 sessionId) const;

    // Tier scaling
    float GetTierHealthMultiplier(uint32 runId) const;
    float GetTierDamageMultiplier(uint32 runId) const;
    float GetTierArmorMultiplier(uint32 runId) const;
    // For the run log: current depth, the descent's seed, active affix codes ("M01,P02").
    bool        GetRunLogInfo(uint32 runId, uint32& depth, uint64& seed, std::string& affixIds) const;

    // Buff system
    void IncrementBuffStacks(uint32 runId);
    void ApplyBuffStacks(Player* player, uint32 runId);
    void RemoveBuffStacks(Player* player, uint32 runId);
    void ApplyBuffAura(Player* player, uint32 stacks);

    // Removes the run buff from a player. Safe for players outside any run.
    static void StripRunBuff(Player* player);

    // Module-owned passive aura defined in atlas_world_setup.sql (spell_dbc).
    // IDs 91000-91099 are reserved for this module.
    static constexpr uint32 BUFF_SPELL_ID = 91001;
    static constexpr float BUFF_PCT_PER_STACK = 10.0f;

    void Update(uint32 diff);

    uint32 GetActiveRunCount() const;
    bool   IsPlayerInRun(ObjectGuid guid) const;

    // Leaderboard
    void SaveRoguelikeLeaderboard(RoguelikeRun const& run);
    void LoadRoguelikeLeaderboard();
    std::vector<RoguelikeLeaderboardEntry> GetRoguelikeLeaderboard(uint32 limit = 10, bool sortByFloors = false) const;

    // Player stats (separate from normal run stats)
    void LoadAllRoguelikePlayerStats();
    RoguelikePlayerStats GetRoguelikePlayerStats(ObjectGuid guid) const;
    void UpdateRoguelikePlayerStats(RoguelikeRun const& run);

private:
    void SelectAffixesForTier(RoguelikeRun& run);
    uint32 PickFloorTheme(RoguelikeRun& run) const;
    // A dungeon in the run's level range that the whole party may enter, skipping `tried`.
    uint32 SelectRandomDungeon(RoguelikeRun& run, std::vector<Player*> const& party,
                               std::set<uint32> const& tried) const;
    // Opens one floor on mapId; on failure no session is left behind.
    bool OpenFloor(RoguelikeRun& run, Player* leader, uint32 mapId);
    // Picks and opens a floor, rerolling past dungeons that still refuse the party.
    // Returns the opened map, or 0.
    uint32 OpenRandomFloor(RoguelikeRun& run, Player* leader, std::vector<Player*> const& party);
    bool TransitionToNextDungeon(RoguelikeRun& run);
    void TeleportRunPlayersOut(RoguelikeRun& run);
    void AnnounceCountdown(RoguelikeRun const& run, uint32 remainingSec);
    void AnnounceToRun(RoguelikeRun const& run, std::string const& msg);

    std::unordered_map<uint32, RoguelikeRun>   _activeRuns;
    std::unordered_map<uint32, uint32>          _sessionToRun;  // sessionId -> runId
    std::unordered_map<ObjectGuid, uint32>      _playerToRun;   // guid -> runId
    uint32 _nextRunId = 1;
    mutable std::mutex _runMutex;

    std::vector<RoguelikeLeaderboardEntry> _leaderboard;
    mutable std::mutex _leaderboardMutex;

    std::unordered_map<uint32, RoguelikePlayerStats> _roguelikeStats;  // guidLow -> stats
    mutable std::mutex _rlStatsMutex;

    uint32 _updateTimer = 0;
    static constexpr uint32 UPDATE_INTERVAL = 1000;
};

} // namespace Atlas

#define sRoguelikeMgr Atlas::RoguelikeMgr::Instance()

#endif
