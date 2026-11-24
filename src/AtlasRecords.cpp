/*
 * mod-atlas — AtlasRecords.cpp
 * Records: per-character statistics and the echo leaderboard.
 *
 * AtlasMgr is split across AtlasMgr.cpp, AtlasPopulation.cpp, AtlasScaling.cpp,
 * AtlasRewards.cpp and AtlasRecords.cpp; the class itself is in AtlasMgr.h.
 */

#include "AtlasMgr.h"
#include "AtlasAffixes.h"
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
    e.MapId          = session.SourceMapId;
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

// Run records

namespace
{
    enum RunKind : uint8
    {
        RUN_KIND_ECHO    = 0,
        RUN_KIND_ENDLESS = 1,
    };

    enum RunResult : uint8
    {
        RUN_RESULT_COMPLETED   = 0,
        RUN_RESULT_FAILED      = 1,   // the whole party fell
        RUN_RESULT_ABANDONED   = 2,   // left, ended by a GM, or the descent stopped
        RUN_RESULT_INTERRUPTED = 3,   // the server stopped mid-run (logged at startup)
    };
}

void AtlasMgr::RecordRunActive(Session const& session, uint32 instanceId, uint32 mapId)
{
    uint32 depth = 0;
    uint64 endlessSeed = 0;
    std::string affixIds;
    if (session.RoguelikeRunId)
        sRoguelikeMgr->GetRunLogInfo(session.RoguelikeRunId, depth, endlessSeed, affixIds);

    CharacterDatabase.Execute(
        "REPLACE INTO atlas_run_active (instance_id, map_id, session_id, kind, endless_run_id, depth, "
        "endless_seed, seed, difficulty_id, theme_id, leader_guid, party_size, effective_level, scaled, "
        "started_at) VALUES ({}, {}, {}, {}, {}, {}, {}, {}, {}, {}, {}, {}, {}, {}, {})",
        instanceId, mapId, session.SessionId,
        uint32(session.RoguelikeRunId ? RUN_KIND_ENDLESS : RUN_KIND_ECHO), session.RoguelikeRunId, depth,
        endlessSeed, session.Seed, session.DifficultyId, session.ThemeId, session.LeaderGuid.GetCounter(),
        session.Players.size(), uint32(session.EffectiveLevel), session.ScaleToParty ? 1 : 0, session.StartTime);
}

void AtlasMgr::LogRun(Session const& session, bool success)
{
    // A floor that never opened (no instance) left nothing to log.
    if (!session.InstanceId)
        return;

    RunResult result = RUN_RESULT_ABANDONED;
    if (success && session.State == SessionState::Completed)
        result = RUN_RESULT_COMPLETED;
    else if (session.State == SessionState::Failed)
        result = RUN_RESULT_FAILED;

    uint32 deaths = 0;
    for (auto const& pd : session.Players)
        deaths += pd.Deaths;

    uint64 const now = GameTime::GetGameTime().count();
    uint64 const end = session.EndTime > session.StartTime ? session.EndTime : now;
    uint32 const duration = end > session.StartTime ? uint32(end - session.StartTime) : 0;

    uint32 depth = 0;
    uint64 endlessSeed = 0;
    std::string affixIds;
    if (session.RoguelikeRunId)
        sRoguelikeMgr->GetRunLogInfo(session.RoguelikeRunId, depth, endlessSeed, affixIds);
    affixIds = sAtlasAffixes->Codes(session.Affixes);

    CharacterDatabase.Execute(
        "INSERT INTO atlas_run_log (kind, endless_run_id, depth, endless_seed, seed, map_id, difficulty_id, "
        "theme_id, leader_guid, party_size, effective_level, scaled, result, started_at, duration, total_mobs, "
        "mobs_killed, bosses_killed, deaths, affixes) VALUES "
        "({}, {}, {}, {}, {}, {}, {}, {}, {}, {}, {}, {}, {}, {}, {}, {}, {}, {}, {}, '{}')",
        uint32(session.RoguelikeRunId ? RUN_KIND_ENDLESS : RUN_KIND_ECHO), session.RoguelikeRunId, depth,
        endlessSeed, session.Seed, session.MapId, session.DifficultyId, session.ThemeId,
        session.LeaderGuid.GetCounter(), session.Players.size(), uint32(session.EffectiveLevel),
        session.ScaleToParty ? 1 : 0, uint32(result), session.StartTime, duration, session.TotalMobs,
        session.MobsKilled, session.BossesKilled, deaths, affixIds);
}

} // namespace Atlas
