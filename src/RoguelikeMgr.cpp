/*
 * mod-dungeon-master — RoguelikeMgr.cpp
 * Roguelike run manager: multi-dungeon progression, buffs, affixes, transitions.
 */

#include "RoguelikeMgr.h"
#include "DungeonMasterMgr.h"
#include "DMConfig.h"
#include "Player.h"
#include "Group.h"
#include "Creature.h"
#include "ObjectAccessor.h"
#include "Chat.h"
#include "GameTime.h"
#include "DatabaseEnv.h"
#include "Log.h"
#include "SpellAuras.h"
#include "SpellAuraEffects.h"
#include "Random.h"
#include "StringFormat.h"
#include <algorithm>

namespace DungeonMaster
{

// RNG: AC's helpers (Random.h), thread-safe. RandInt keeps the call sites'
// integer types; every range used here fits in uint32.
template<typename T>
static T RandInt(T lo, T hi)
{
    return static_cast<T>(urand(static_cast<uint32>(lo), static_cast<uint32>(hi)));
}

// Singleton
RoguelikeMgr::RoguelikeMgr()  = default;
RoguelikeMgr::~RoguelikeMgr() = default;

RoguelikeMgr* RoguelikeMgr::Instance()
{
    static RoguelikeMgr inst;
    return &inst;
}

// Initialization
void RoguelikeMgr::Initialize()
{
    BuildAffixPool();
    LoadAllRoguelikePlayerStats();
    LoadRoguelikeLeaderboard();
    LOG_INFO("module", "RoguelikeMgr: Initialized — {} affix definitions, {} buff pool entries.",
        _affixDefs.size(), sDMConfig->GetRoguelikeBuffPool().size());
}

void RoguelikeMgr::BuildAffixPool()
{
    _affixDefs.clear();

    // Fortified — trash mobs are significantly harder
    {
        AffixDef a;
        a.Id           = AFFIX_FORTIFIED;
        a.Name         = "Fortified";
        a.TrashHpMult  = 1.30f;
        a.TrashDmgMult = 1.15f;
        a.BossHpMult   = 1.0f;
        a.BossDmgMult  = 1.0f;
        _affixDefs.push_back(a);
    }
    // Tyrannical — bosses are significantly harder
    {
        AffixDef a;
        a.Id           = AFFIX_TYRANNICAL;
        a.Name         = "Tyrannical";
        a.TrashHpMult  = 1.0f;
        a.TrashDmgMult = 1.0f;
        a.BossHpMult   = 1.40f;
        a.BossDmgMult  = 1.20f;
        _affixDefs.push_back(a);
    }
    // Raging — everything hits harder
    {
        AffixDef a;
        a.Id           = AFFIX_RAGING;
        a.Name         = "Raging";
        a.TrashHpMult  = 1.0f;
        a.TrashDmgMult = 1.25f;
        a.BossHpMult   = 1.0f;
        a.BossDmgMult  = 1.25f;
        _affixDefs.push_back(a);
    }
    // Bolstering — everything has more health
    {
        AffixDef a;
        a.Id           = AFFIX_BOLSTERING;
        a.Name         = "Bolstering";
        a.TrashHpMult  = 1.20f;
        a.TrashDmgMult = 1.0f;
        a.BossHpMult   = 1.20f;
        a.BossDmgMult  = 1.0f;
        _affixDefs.push_back(a);
    }
    // Savage — more elites, elites are nastier
    {
        AffixDef a;
        a.Id              = AFFIX_SAVAGE;
        a.Name            = "Savage";
        a.TrashHpMult     = 1.0f;
        a.TrashDmgMult    = 1.10f;
        a.BossHpMult      = 1.0f;
        a.BossDmgMult     = 1.0f;
        a.EliteChanceMult = 2.0f;
        _affixDefs.push_back(a);
    }
}

// RUN LIFECYCLE

bool RoguelikeMgr::StartRun(Player* leader, uint32 difficultyId, uint32 themeId, bool scaleToParty)
{
    if (!leader)
        return false;

    if (!sDMConfig->IsRoguelikeEnabled())
    {
        ChatHandler(leader->GetSession()).PSendSysMessage(
            "{} The Endless Timeways are sealed for now.", CHAT_PREFIX_ENDLESS);
        return false;
    }

    // Check: player not already in a run
    if (IsPlayerInRun(leader->GetGUID()))
    {
        ChatHandler(leader->GetSession()).PSendSysMessage(
            "{} You are already bound to the Endless Timeways.", CHAT_PREFIX_ENDLESS);
        return false;
    }

    // Check: player not in a DM session
    if (sDungeonMasterMgr->GetSessionByPlayer(leader->GetGUID()))
    {
        ChatHandler(leader->GetSession()).PSendSysMessage(
            "{} You are already walking an echo.", CHAT_PREFIX_ENDLESS);
        return false;
    }

    // Check: cooldown
    if (sDungeonMasterMgr->IsOnCooldown(leader->GetGUID()))
    {
        uint32 rem = sDungeonMasterMgr->GetRemainingCooldown(leader->GetGUID());
        ChatHandler(leader->GetSession()).PSendSysMessage(
            "{} The timeways need |cFFFFFFFF{}|r min |cFFFFFFFF{}|r sec to settle first.",
            CHAT_PREFIX_ENDLESS, rem / 60, rem % 60);
        return false;
    }

    // Check: can create a session
    if (!sDungeonMasterMgr->CanCreateNewSession())
    {
        ChatHandler(leader->GetSession()).PSendSysMessage(
            "{} Too many echoes are open at once. Return shortly.", CHAT_PREFIX_ENDLESS);
        return false;
    }

    // Build the run
    RoguelikeRun run;
    {
        std::lock_guard<std::mutex> lock(_runMutex);
        run.RunId          = _nextRunId++;
    }
    run.LeaderGuid     = leader->GetGUID();
    run.State          = RoguelikeRunState::Active;
    run.ThemeId        = themeId;
    run.ScaleToParty   = scaleToParty;
    run.CurrentTier    = 1;
    run.RunStartTime   = GameTime::GetGameTime().count();

    // Use the difficulty selected by the player (or fall back to first available)
    run.BaseDifficultyId = difficultyId;
    DifficultyTier const* diff = sDMConfig->GetDifficulty(difficultyId);
    if (!diff)
    {
        auto const& diffs = sDMConfig->GetDifficulties();
        run.BaseDifficultyId = diffs.empty() ? 1 : diffs[0].Id;
    }

    // Store original positions for ALL party members
    RoguelikePlayerData ld;
    ld.PlayerGuid       = leader->GetGUID();
    ld.OriginalMapId    = leader->GetMapId();
    ld.OriginalPosition = { leader->GetPositionX(), leader->GetPositionY(),
                            leader->GetPositionZ(), leader->GetOrientation() };
    run.Players.push_back(ld);

    if (Group* g = leader->GetGroup())
    {
        for (GroupReference* ref = g->GetFirstMember(); ref; ref = ref->next())
        {
            Player* m = ref->GetSource();
            if (m && m != leader && m->IsInWorld())
            {
                // Check: group member not in another run
                if (IsPlayerInRun(m->GetGUID()))
                    continue;
                if (sDungeonMasterMgr->GetSessionByPlayer(m->GetGUID()))
                    continue;

                RoguelikePlayerData md;
                md.PlayerGuid       = m->GetGUID();
                md.OriginalMapId    = m->GetMapId();
                md.OriginalPosition = { m->GetPositionX(), m->GetPositionY(),
                                        m->GetPositionZ(), m->GetOrientation() };
                run.Players.push_back(md);
            }
        }
    }

    // Select the first dungeon
    uint32 mapId = SelectRandomDungeon(run);
    if (!mapId)
    {
        ChatHandler(leader->GetSession()).PSendSysMessage(
            "{} No echo can be reached at your level.", CHAT_PREFIX_ENDLESS);
        return false;
    }

    // Clear cooldowns for all party members so they can enter
    for (auto const& pd : run.Players)
        sDungeonMasterMgr->ClearCooldown(pd.PlayerGuid);

    // Create the DM session with the player's scaling choice
    Session* session = sDungeonMasterMgr->CreateSession(
        leader, run.BaseDifficultyId, themeId, mapId, run.ScaleToParty);
    if (!session)
    {
        ChatHandler(leader->GetSession()).PSendSysMessage(
            "{} The first echo would not hold.", CHAT_PREFIX_ENDLESS);
        return false;
    }

    // Tag the session as roguelike
    session->RoguelikeRunId = run.RunId;
    run.CurrentSessionId    = session->SessionId;

    // Start the dungeon
    if (!sDungeonMasterMgr->StartDungeon(session))
    {
        ChatHandler(leader->GetSession()).PSendSysMessage(
            "{} The first echo could not be opened.", CHAT_PREFIX_ENDLESS);
        sDungeonMasterMgr->CleanupRoguelikeSession(session->SessionId, false);
        return false;
    }

    if (!sDungeonMasterMgr->TeleportPartyIn(session))
    {
        ChatHandler(leader->GetSession()).PSendSysMessage(
            "{} The timeways would not carry you.", CHAT_PREFIX_ENDLESS);
        sDungeonMasterMgr->CleanupRoguelikeSession(session->SessionId, false);
        return false;
    }

    // No buff on tier 1 — first +10% earned after clearing floor 1
    run.BuffStacks = 0;

    // Grace period for async teleport
    run.TransitionStartTime = GameTime::GetGameTime().count();

    // Select affixes for tier 1 (may be none if affix start tier > 1)
    SelectAffixesForTier(run);

    // Register the run
    {
        std::lock_guard<std::mutex> lock(_runMutex);
        _activeRuns[run.RunId] = run;
        _sessionToRun[run.CurrentSessionId] = run.RunId;
        for (auto const& pd : run.Players)
            _playerToRun[pd.PlayerGuid] = run.RunId;
    }

    // Announce
    Theme const* theme = sDMConfig->GetTheme(themeId);
    AnnounceToRun(run, Acore::StringFormat(
        "{} |cFFFFD700{}|r leads the descent into the Endless Timeways. Timeline: |cFF00FF00{}|r. "
        "How deep can you go?",
        CHAT_PREFIX_ENDLESS, leader->GetName(), theme ? theme->Name : "ever-shifting"));

    // Announce active affixes if any are present at tier 1
    if (!run.ActiveAffixes.empty())
    {
        std::string affixNames = GetActiveAffixNames(run.RunId);
        if (!affixNames.empty())
            AnnounceToRun(run, Acore::StringFormat("{} This echo is unstable: {}", CHAT_PREFIX_ENDLESS, affixNames));
    }

    LOG_INFO("module", "RoguelikeMgr: Run {} started — leader {}, party {}, theme {}, map {}",
        run.RunId, leader->GetName(), run.Players.size(),
        theme ? theme->Name.c_str() : "Random", mapId);

    return true;
}

void RoguelikeMgr::OnDungeonCompleted(uint32 runId, uint32 sessionId)
{
    RoguelikeRun* run = nullptr;
    {
        std::lock_guard<std::mutex> lock(_runMutex);
        auto it = _activeRuns.find(runId);
        if (it == _activeRuns.end()) return;
        run = &it->second;
    }

    if (run->CurrentSessionId != sessionId)
    {
        LOG_WARN("module", "RoguelikeMgr: OnDungeonCompleted — session {} != current {}",
            sessionId, run->CurrentSessionId);
        return;
    }

    // Copy data before cleanup invalidates the session
    uint32 sessionMobsKilled   = 0;
    uint32 sessionBossesKilled = 0;
    uint32 sessionDeaths       = 0;
    uint32 sessionMapId        = 0;
    {
        Session* session = sDungeonMasterMgr->GetSession(sessionId);
        if (session)
        {
            sessionMobsKilled   = session->MobsKilled;
            sessionBossesKilled = session->BossesKilled;
            sessionMapId        = session->MapId;
            for (auto const& pd : session->Players)
                sessionDeaths += pd.Deaths;

            // Distribute per-floor rewards while session pointer is still valid
            sDungeonMasterMgr->DistributeRewards(session);
        }
    }

    // Accumulate stats
    run->TotalMobsKilled   += sessionMobsKilled;
    run->TotalBossesKilled += sessionBossesKilled;
    run->TotalDeaths       += sessionDeaths;

    ++run->DungeonsCleared;
    run->PreviousMapId = sessionMapId;

    // Clean up the DM session (no teleport, no cooldown)
    sDungeonMasterMgr->CleanupRoguelikeSession(sessionId, true);

    // Remove old session mapping
    {
        std::lock_guard<std::mutex> lock(_runMutex);
        _sessionToRun.erase(sessionId);
    }

    // Increment tier
    ++run->CurrentTier;

    // Select new affixes
    SelectAffixesForTier(*run);

    // Apply a new buff stack (+10% all stats)
    IncrementBuffStacks(run->RunId);

    // Announce progress
    std::string msg = Acore::StringFormat(
        "{} |cFFFFD700Echo {} mended!|r The timeways pull you deeper, to |cFFFF0000depth {}|r...",
        CHAT_PREFIX_ENDLESS, run->DungeonsCleared, run->CurrentTier);

    // Append active affixes
    std::string affixStr;
    for (RoguelikeAffix afxId : run->ActiveAffixes)
    {
        for (auto const& def : _affixDefs)
        {
            if (def.Id == afxId)
            {
                if (!affixStr.empty()) affixStr += ", ";
                affixStr += "|cFFFF8800" + def.Name + "|r";
                break;
            }
        }
    }
    if (!affixStr.empty())
        msg += " Instabilities: " + affixStr;

    AnnounceToRun(*run, msg);

    // Grace period for abandoned detection
    run->TransitionStartTime = GameTime::GetGameTime().count();

    // Transition to the next dungeon
    if (!TransitionToNextDungeon(*run))
    {
        // Failed to create next dungeon — end the run gracefully
        AnnounceToRun(*run, Acore::StringFormat(
            "{} No further echo can be reached. The descent ends at |cFFFFD700depth {}|r "
            "after |cFFFFFFFF{}|r echo{}.",
            CHAT_PREFIX_ENDLESS, run->CurrentTier, run->DungeonsCleared, run->DungeonsCleared != 1 ? "es" : ""));
        EndRun(run->RunId, true);
    }
}

// Handle party wipe
void RoguelikeMgr::OnPartyWipe(uint32 runId)
{
    RoguelikeRun* run = nullptr;
    {
        std::lock_guard<std::mutex> lock(_runMutex);
        auto it = _activeRuns.find(runId);
        if (it == _activeRuns.end()) return;
        run = &it->second;
    }

    // Accumulate stats from the final session
    Session* session = sDungeonMasterMgr->GetSession(run->CurrentSessionId);
    if (session)
    {
        run->TotalMobsKilled   += session->MobsKilled;
        run->TotalBossesKilled += session->BossesKilled;
        for (auto const& pd : session->Players)
            run->TotalDeaths += pd.Deaths;
    }

    // Announce the wipe
    uint32 duration = static_cast<uint32>(
        GameTime::GetGameTime().count() - run->RunStartTime);
    uint32 dm = duration / 60, ds = duration % 60;

    std::string const results = Acore::StringFormat(
        "{} |cFFFF4444Your whole party has fallen.|r The timeways cast you out.\n"
        "{} The descent, as the Keepers will chronicle it:\n"
        "  Depth Reached: |cFFFFD700{}|r\n"
        "  Echoes Mended: |cFFFFFFFF{}|r\n"
        "  Foes Slain: |cFFFFFFFF{}|r\n"
        "  Masters Slain: |cFFFFFFFF{}|r\n"
        "  Total Deaths: |cFFFF0000{}|r\n"
        "  Duration: |cFF00FFFF{}m {:02}s|r",
        CHAT_PREFIX_ENDLESS, CHAT_PREFIX_ENDLESS, run->CurrentTier, run->DungeonsCleared,
        run->TotalMobsKilled, run->TotalBossesKilled, run->TotalDeaths, dm, ds);

    // Resurrect dead players
    for (auto const& pd : run->Players)
    {
        Player* p = ObjectAccessor::FindPlayer(pd.PlayerGuid);
        if (!p || !p->IsInWorld()) continue;
        p->RemoveFlag(PLAYER_FIELD_BYTES, PLAYER_FIELD_BYTE_NO_RELEASE_WINDOW);
        if (!p->IsAlive())
        {
            p->ResurrectPlayer(1.0f);
            p->SpawnCorpseBones();
        }
    }

    AnnounceToRun(*run, results);

    // Save leaderboard
    SaveRoguelikeLeaderboard(*run);

    // Remove all buff stacks (skip mid-teleport players)
    for (auto const& pd : run->Players)
    {
        Player* p = ObjectAccessor::FindPlayer(pd.PlayerGuid);
        if (p && p->IsInWorld())
            RemoveBuffStacks(p, run->RunId);
    }

    // Clean up the DM session
    if (run->CurrentSessionId != 0)
        sDungeonMasterMgr->CleanupRoguelikeSession(run->CurrentSessionId, false);

    // Teleport everyone back to their original positions
    TeleportRunPlayersOut(*run);

    // Set cooldowns
    for (auto const& pd : run->Players)
        sDungeonMasterMgr->SetCooldown(pd.PlayerGuid);

    // Save before erase invalidates the pointer
    uint32 savedTier    = run->CurrentTier;
    uint32 savedCleared = run->DungeonsCleared;
    uint32 savedSessId  = run->CurrentSessionId;

    // Clean up run
    {
        std::lock_guard<std::mutex> lock(_runMutex);
        _sessionToRun.erase(savedSessId);
        for (auto const& pd : run->Players)
            _playerToRun.erase(pd.PlayerGuid);
        _activeRuns.erase(runId);
    }

    LOG_INFO("module", "RoguelikeMgr: Run {} ended (wipe) — tier {}, {} floors cleared.",
        runId, savedTier, savedCleared);
}

// End run gracefully (voluntary exit or no dungeons left)
void RoguelikeMgr::EndRun(uint32 runId, bool announceResults)
{
    RoguelikeRun* run = nullptr;
    {
        std::lock_guard<std::mutex> lock(_runMutex);
        auto it = _activeRuns.find(runId);
        if (it == _activeRuns.end()) return;
        run = &it->second;
    }

    if (announceResults)
    {
        uint32 duration = static_cast<uint32>(
            GameTime::GetGameTime().count() - run->RunStartTime);
        uint32 dm = duration / 60, ds = duration % 60;

        AnnounceToRun(*run, Acore::StringFormat(
            "{} The descent is over.\n"
            "  Depth Reached: |cFFFFD700{}|r\n"
            "  Echoes Mended: |cFFFFFFFF{}|r\n"
            "  Foes Slain: |cFFFFFFFF{}|r\n"
            "  Masters Slain: |cFFFFFFFF{}|r\n"
            "  Duration: |cFF00FFFF{}m {:02}s|r",
            CHAT_PREFIX_ENDLESS, run->CurrentTier, run->DungeonsCleared,
            run->TotalMobsKilled, run->TotalBossesKilled, dm, ds));

        SaveRoguelikeLeaderboard(*run);
    }

    // Resurrect all dead players (skip mid-teleport players)
    for (auto const& pd : run->Players)
    {
        Player* p = ObjectAccessor::FindPlayer(pd.PlayerGuid);
        if (!p || !p->IsInWorld()) continue;
        p->RemoveFlag(PLAYER_FIELD_BYTES, PLAYER_FIELD_BYTE_NO_RELEASE_WINDOW);
        if (!p->IsAlive())
        {
            p->ResurrectPlayer(1.0f);
            p->SpawnCorpseBones();
        }
    }

    // Remove buff stacks (skip mid-teleport players)
    for (auto const& pd : run->Players)
        if (Player* p = ObjectAccessor::FindPlayer(pd.PlayerGuid))
            if (p->IsInWorld())
                RemoveBuffStacks(p, run->RunId);

    // Distribute roguelike rewards (scaled by tier)
    if (run->DungeonsCleared > 0)
    {
        // Compute effective level from the leader (or first available player)
        uint8 effectiveLevel = 1;
        for (auto const& pd : run->Players)
        {
            Player* p = ObjectAccessor::FindPlayer(pd.PlayerGuid);
            if (p) { effectiveLevel = p->GetLevel(); break; }
        }

        std::vector<ObjectGuid> guids;
        for (auto const& pd : run->Players)
            guids.push_back(pd.PlayerGuid);

        sDungeonMasterMgr->DistributeRoguelikeRewards(
            run->CurrentTier, effectiveLevel, guids);
    }

    // Clean up DM session if one is active
    if (run->CurrentSessionId != 0)
        sDungeonMasterMgr->CleanupRoguelikeSession(run->CurrentSessionId, false);

    // Teleport out
    TeleportRunPlayersOut(*run);

    // Set cooldowns
    for (auto const& pd : run->Players)
        sDungeonMasterMgr->SetCooldown(pd.PlayerGuid);

    // Save before erase invalidates the pointer
    uint32 savedTier    = run->CurrentTier;
    uint32 savedCleared = run->DungeonsCleared;
    uint32 savedSessId  = run->CurrentSessionId;

    // Erase run
    {
        std::lock_guard<std::mutex> lock(_runMutex);
        _sessionToRun.erase(savedSessId);
        for (auto const& pd : run->Players)
            _playerToRun.erase(pd.PlayerGuid);
        _activeRuns.erase(runId);
    }

    LOG_INFO("module", "RoguelikeMgr: Run {} ended (graceful) — tier {}, {} floors.",
        runId, savedTier, savedCleared);
}

void RoguelikeMgr::AbandonRun(uint32 runId)
{
    EndRun(runId, true);
}

void RoguelikeMgr::QuitRun(ObjectGuid playerGuid)
{
    uint32 runId = 0;
    {
        std::lock_guard<std::mutex> lock(_runMutex);
        auto it = _playerToRun.find(playerGuid);
        if (it != _playerToRun.end())
            runId = it->second;
    }
    if (runId != 0)
        AbandonRun(runId);
}

// QUERIES

RoguelikeRun* RoguelikeMgr::GetRun(uint32 runId)
{
    std::lock_guard<std::mutex> lock(_runMutex);
    auto it = _activeRuns.find(runId);
    return it != _activeRuns.end() ? &it->second : nullptr;
}

RoguelikeRun* RoguelikeMgr::GetRunBySession(uint32 sessionId)
{
    std::lock_guard<std::mutex> lock(_runMutex);
    auto it = _sessionToRun.find(sessionId);
    if (it != _sessionToRun.end())
    {
        auto rit = _activeRuns.find(it->second);
        return rit != _activeRuns.end() ? &rit->second : nullptr;
    }
    return nullptr;
}

RoguelikeRun* RoguelikeMgr::GetRunByPlayer(ObjectGuid guid)
{
    std::lock_guard<std::mutex> lock(_runMutex);
    auto it = _playerToRun.find(guid);
    if (it != _playerToRun.end())
    {
        auto rit = _activeRuns.find(it->second);
        return rit != _activeRuns.end() ? &rit->second : nullptr;
    }
    return nullptr;
}

uint32 RoguelikeMgr::GetRunIdBySession(uint32 sessionId) const
{
    std::lock_guard<std::mutex> lock(_runMutex);
    auto it = _sessionToRun.find(sessionId);
    return it != _sessionToRun.end() ? it->second : 0;
}

bool RoguelikeMgr::IsPlayerInRun(ObjectGuid guid) const
{
    std::lock_guard<std::mutex> lock(_runMutex);
    return _playerToRun.count(guid) > 0;
}

uint32 RoguelikeMgr::GetActiveRunCount() const
{
    std::lock_guard<std::mutex> lock(_runMutex);
    return static_cast<uint32>(_activeRuns.size());
}

// Scaling (called from PopulateDungeon)

float RoguelikeMgr::GetTierHealthMultiplier(uint32 runId) const
{
    std::lock_guard<std::mutex> lock(_runMutex);
    auto it = _activeRuns.find(runId);
    if (it == _activeRuns.end()) return 1.0f;

    uint32 tier = it->second.CurrentTier;
    if (tier <= 1) return 1.0f;

    float baseScale = sDMConfig->GetRoguelikeHpScaling();
    uint32 expThresh = sDMConfig->GetRoguelikeExpThreshold();
    float  expFactor = sDMConfig->GetRoguelikeExpFactor();

    if (tier <= expThresh)
        return 1.0f + (tier - 1) * baseScale;

    // Exponential scaling past threshold
    float linearPart = (expThresh - 1) * baseScale;
    float expPart    = 0.0f;
    for (uint32 t = expThresh; t < tier; ++t)
        expPart += baseScale * std::pow(expFactor, static_cast<float>(t - expThresh + 1));

    return 1.0f + linearPart + expPart;
}

float RoguelikeMgr::GetTierDamageMultiplier(uint32 runId) const
{
    std::lock_guard<std::mutex> lock(_runMutex);
    auto it = _activeRuns.find(runId);
    if (it == _activeRuns.end()) return 1.0f;

    uint32 tier = it->second.CurrentTier;
    if (tier <= 1) return 1.0f;

    float baseScale = sDMConfig->GetRoguelikeDmgScaling();
    uint32 expThresh = sDMConfig->GetRoguelikeExpThreshold();
    float  expFactor = sDMConfig->GetRoguelikeExpFactor();

    if (tier <= expThresh)
        return 1.0f + (tier - 1) * baseScale;

    float linearPart = (expThresh - 1) * baseScale;
    float expPart    = 0.0f;
    for (uint32 t = expThresh; t < tier; ++t)
        expPart += baseScale * std::pow(expFactor, static_cast<float>(t - expThresh + 1));

    return 1.0f + linearPart + expPart;
}

float RoguelikeMgr::GetTierArmorMultiplier(uint32 runId) const
{
    std::lock_guard<std::mutex> lock(_runMutex);
    auto it = _activeRuns.find(runId);
    if (it == _activeRuns.end()) return 1.0f;

    uint32 tier = it->second.CurrentTier;
    if (tier <= 1) return 1.0f;

    float baseScale = sDMConfig->GetRoguelikeArmorScaling();
    return 1.0f + (tier - 1) * baseScale;   // Armor scales linearly only
}

void RoguelikeMgr::GetAffixMultipliers(
    uint32 runId, bool isBoss, bool /*isElite*/,
    float& outHpMult, float& outDmgMult, float& outEliteChanceMult) const
{
    outHpMult   = 1.0f;
    outDmgMult  = 1.0f;
    outEliteChanceMult = 1.0f;

    std::lock_guard<std::mutex> lock(_runMutex);
    auto it = _activeRuns.find(runId);
    if (it == _activeRuns.end()) return;

    for (RoguelikeAffix afxId : it->second.ActiveAffixes)
    {
        for (auto const& def : _affixDefs)
        {
            if (def.Id == afxId)
            {
                if (isBoss)
                {
                    outHpMult  *= def.BossHpMult;
                    outDmgMult *= def.BossDmgMult;
                }
                else
                {
                    outHpMult  *= def.TrashHpMult;
                    outDmgMult *= def.TrashDmgMult;
                }
                outEliteChanceMult *= def.EliteChanceMult;
                break;
            }
        }
    }
}

bool RoguelikeMgr::HasActiveAffixes(uint32 runId) const
{
    std::lock_guard<std::mutex> lock(_runMutex);
    auto it = _activeRuns.find(runId);
    return it != _activeRuns.end() && !it->second.ActiveAffixes.empty();
}

std::string RoguelikeMgr::GetActiveAffixNames(uint32 runId) const
{
    std::lock_guard<std::mutex> lock(_runMutex);
    auto it = _activeRuns.find(runId);
    if (it == _activeRuns.end()) return "";

    std::string result;
    for (RoguelikeAffix afxId : it->second.ActiveAffixes)
    {
        for (auto const& def : _affixDefs)
        {
            if (def.Id == afxId)
            {
                if (!result.empty()) result += ", ";
                result += "|cFFFF8800" + def.Name + "|r";
                break;
            }
        }
    }
    return result;
}

// Buff system: +10% all stats per cleared floor, carried by the module's own
// stacking aura (see ApplyBuffAura).

// The module's own passive aura (spell_dbc row in dm_setup.sql): +10% all
// stats per stack, up to 255 stacks. It replaces Greater Blessing of Kings,
// which had to be patched server-wide to stack and so let every paladin stack
// it too (upstream #23). Being passive, it is never sent to clients and never
// saved to character_aura.

void RoguelikeMgr::StripRunBuff(Player* player)
{
    if (player)
        player->RemoveAura(BUFF_SPELL_ID);
}

void RoguelikeMgr::ApplyBuffAura(Player* player, uint32 stacks)
{
    if (!player || !player->IsInWorld() || stacks == 0) return;

    StripRunBuff(player);

    // nullptr if the spell_dbc row is missing (logged once at startup).
    Aura* aura = player->AddAura(BUFF_SPELL_ID, player);
    if (!aura)
        return;

    aura->SetStackAmount(static_cast<uint8>(std::min<uint32>(stacks, 255)));
}

void RoguelikeMgr::IncrementBuffStacks(uint32 runId)
{
    RoguelikeRun* run = nullptr;
    {
        std::lock_guard<std::mutex> lock(_runMutex);
        auto it = _activeRuns.find(runId);
        if (it == _activeRuns.end()) return;
        run = &it->second;
    }

    ++run->BuffStacks;

    for (auto const& pd : run->Players)
    {
        Player* p = ObjectAccessor::FindPlayer(pd.PlayerGuid);
        if (p && p->IsInWorld())
            ApplyBuffAura(p, run->BuffStacks);
    }

    // Announce
    AnnounceToRun(*run, Acore::StringFormat(
        "{} Timeways Resonance grows: |cFF00FF00+{:.0f}% all stats|r ({} echo{} deep).",
        CHAT_PREFIX_ENDLESS, BUFF_PCT_PER_STACK * run->BuffStacks, run->BuffStacks,
        run->BuffStacks != 1 ? "es" : ""));
}

void RoguelikeMgr::ApplyBuffStacks(Player* player, uint32 runId)
{
    if (!player || !player->IsInWorld()) return;

    std::lock_guard<std::mutex> lock(_runMutex);
    auto it = _activeRuns.find(runId);
    if (it == _activeRuns.end()) return;

    if (it->second.BuffStacks == 0) return;
    ApplyBuffAura(player, it->second.BuffStacks);
}

void RoguelikeMgr::RemoveBuffStacks(Player* player, uint32 runId)
{
    if (!player || !player->IsInWorld()) return;
    StripRunBuff(player);
}

// AFFIX SELECTION

void RoguelikeMgr::SelectAffixesForTier(RoguelikeRun& run)
{
    run.ActiveAffixes.clear();

    uint32 affixStart  = sDMConfig->GetRoguelikeAffixStartTier();
    uint32 secondAffix = sDMConfig->GetRoguelikeSecondAffixTier();
    uint32 thirdAffix  = sDMConfig->GetRoguelikeThirdAffixTier();

    if (run.CurrentTier < affixStart || _affixDefs.empty())
        return;

    uint32 numAffixes = 1;
    if (run.CurrentTier >= thirdAffix)
        numAffixes = 3;
    else if (run.CurrentTier >= secondAffix)
        numAffixes = 2;

    std::vector<RoguelikeAffix> pool;
    for (auto const& def : _affixDefs)
        if (def.Id != AFFIX_NONE)
            pool.push_back(def.Id);

    std::shuffle(pool.begin(), pool.end(), tRng);

    for (uint32 i = 0; i < numAffixes && i < pool.size(); ++i)
        run.ActiveAffixes.push_back(pool[i]);
}

// DUNGEON SELECTION

uint32 RoguelikeMgr::SelectRandomDungeon(RoguelikeRun const& run) const
{
    DifficultyTier const* diff = sDMConfig->GetDifficulty(run.BaseDifficultyId);
    if (!diff)
    {
        // Fallback: use broadest range
        auto dgs = sDMConfig->GetDungeonsForLevel(1, 80);
        if (dgs.empty()) return 0;
        return dgs[RandInt<size_t>(0, dgs.size() - 1)]->MapId;
    }

    auto dgs = sDMConfig->GetDungeonsForLevel(diff->MinLevel, diff->MaxLevel);
    if (dgs.empty()) return 0;

    // Try to avoid repeating the same dungeon
    if (dgs.size() > 1 && run.PreviousMapId != 0)
    {
        std::vector<DungeonInfo const*> filtered;
        for (auto const* d : dgs)
            if (d->MapId != run.PreviousMapId)
                filtered.push_back(d);

        if (!filtered.empty())
            return filtered[RandInt<size_t>(0, filtered.size() - 1)]->MapId;
    }

    return dgs[RandInt<size_t>(0, dgs.size() - 1)]->MapId;
}

// Transition between dungeons

bool RoguelikeMgr::TransitionToNextDungeon(RoguelikeRun& run)
{
    uint32 mapId = SelectRandomDungeon(run);
    if (!mapId)
    {
        LOG_WARN("module", "RoguelikeMgr: No dungeon available for run {} tier {}",
            run.RunId, run.CurrentTier);
        return false;
    }

    // Find leader or first online player
    Player* leader = ObjectAccessor::FindPlayer(run.LeaderGuid);
    if (!leader)
    {
        for (auto const& pd : run.Players)
        {
            leader = ObjectAccessor::FindPlayer(pd.PlayerGuid);
            if (leader) { run.LeaderGuid = leader->GetGUID(); break; }
        }
    }

    if (!leader)
    {
        LOG_WARN("module", "RoguelikeMgr: No online leader for run {}", run.RunId);
        return false;
    }

    // Clear cooldowns (EndSession might have set them)
    for (auto const& pd : run.Players)
        sDungeonMasterMgr->ClearCooldown(pd.PlayerGuid);

    // Select theme: run-locked theme or random
    uint32 themeId = run.ThemeId;
    if (themeId == 0)
    {
        auto const& themes = sDMConfig->GetThemes();
        if (!themes.empty())
            themeId = themes[RandInt<size_t>(0, themes.size() - 1)].Id;
    }

    // Create the new DM session
    Session* session = sDungeonMasterMgr->CreateSession(
        leader, run.BaseDifficultyId, themeId, mapId, run.ScaleToParty);
    if (!session)
    {
        LOG_ERROR("module", "RoguelikeMgr: Failed to create session for run {} tier {}",
            run.RunId, run.CurrentTier);
        return false;
    }

    // Tag as roguelike
    session->RoguelikeRunId = run.RunId;
    run.CurrentSessionId    = session->SessionId;

    // Register session mapping
    {
        std::lock_guard<std::mutex> lock(_runMutex);
        _sessionToRun[session->SessionId] = run.RunId;
    }

    // Start and teleport
    if (!sDungeonMasterMgr->StartDungeon(session))
    {
        LOG_ERROR("module", "RoguelikeMgr: StartDungeon failed for run {}", run.RunId);
        sDungeonMasterMgr->CleanupRoguelikeSession(session->SessionId, false);
        {
            std::lock_guard<std::mutex> lock2(_runMutex);
            _sessionToRun.erase(session->SessionId);
        }
        return false;
    }

    if (!sDungeonMasterMgr->TeleportPartyIn(session))
    {
        LOG_ERROR("module", "RoguelikeMgr: Teleport failed for run {}", run.RunId);
        sDungeonMasterMgr->CleanupRoguelikeSession(session->SessionId, false);
        {
            std::lock_guard<std::mutex> lock2(_runMutex);
            _sessionToRun.erase(session->SessionId);
        }
        return false;
    }

    run.State = RoguelikeRunState::Active;

    DungeonInfo const* dg = sDMConfig->GetDungeon(mapId);
    AnnounceToRun(run, Acore::StringFormat("{} An echo of |cFFFFFFFF{}|r forms — |cFFFF0000depth {}|r.",
        CHAT_PREFIX_ENDLESS, dg ? dg->Name : "an unknown place", run.CurrentTier));

    LOG_INFO("module", "RoguelikeMgr: Run {} transitioned to tier {} — map {} ({})",
        run.RunId, run.CurrentTier, mapId,
        dg ? dg->Name.c_str() : "?");

    return true;
}

void RoguelikeMgr::TeleportRunPlayersOut(RoguelikeRun& run)
{
    for (auto const& pd : run.Players)
    {
        Player* p = ObjectAccessor::FindPlayer(pd.PlayerGuid);
        if (!p) continue;

        // Skip players that are mid-teleport or not fully in the world.
        if (!p->IsInWorld())
            continue;

        p->RemoveFlag(PLAYER_FIELD_BYTES, PLAYER_FIELD_BYTE_NO_RELEASE_WINDOW);
        if (!p->IsAlive())
        {
            p->ResurrectPlayer(1.0f);
            p->SpawnCorpseBones();
        }

        p->TeleportTo(pd.OriginalMapId,
            pd.OriginalPosition.GetPositionX(),
            pd.OriginalPosition.GetPositionY(),
            pd.OriginalPosition.GetPositionZ(),
            pd.OriginalPosition.GetOrientation());
    }
}

// ANNOUNCEMENTS

void RoguelikeMgr::AnnounceToRun(RoguelikeRun const& run, std::string const& msg)
{
    for (auto const& pd : run.Players)
        if (Player* p = ObjectAccessor::FindPlayer(pd.PlayerGuid))
            if (p->GetSession())
                ChatHandler(p->GetSession()).SendSysMessage(msg);
}

void RoguelikeMgr::AnnounceCountdown(RoguelikeRun const& run, uint32 remainingSec)
{
    AnnounceToRun(run, Acore::StringFormat("{} The next echo opens in |cFFFFFFFF{}|r second{}...",
        CHAT_PREFIX_ENDLESS, remainingSec, remainingSec != 1 ? "s" : ""));
}

void RoguelikeMgr::Update(uint32 diff)
{
    _updateTimer += diff;
    if (_updateTimer < UPDATE_INTERVAL)
        return;
    _updateTimer = 0;

    std::vector<uint32> toAbandon;

    {
        std::lock_guard<std::mutex> lock(_runMutex);

        for (auto& [rid, run] : _activeRuns)
        {
            // ---- Transition grace period ----
            if (run.TransitionStartTime > 0)
            {
                uint64 elapsed = GameTime::GetGameTime().count() - run.TransitionStartTime;
                if (elapsed < 30)
                    continue;   // still in grace window
                // Grace expired — clear flag so normal detection resumes
                run.TransitionStartTime = 0;
            }

            // ---- Abandoned detection: all players offline ----
            bool anyOnline = false;
            for (auto const& pd : run.Players)
            {
                Player* p = ObjectAccessor::FindPlayer(pd.PlayerGuid);
                if (p && p->IsInWorld()) { anyOnline = true; break; }
            }

            if (!anyOnline)
            {
                toAbandon.push_back(rid);
                continue;
            }

            // ---- Re-apply buff aura after death ----
            if (run.State == RoguelikeRunState::Active && run.BuffStacks > 0)
            {
                for (auto const& pd : run.Players)
                {
                    Player* p = ObjectAccessor::FindPlayer(pd.PlayerGuid);
                    if (!p || !p->IsInWorld() || !p->IsAlive()) continue;

                    if (!p->HasAura(BUFF_SPELL_ID))
                        ApplyBuffAura(p, run.BuffStacks);
                }
            }
        }
    }

    for (uint32 rid : toAbandon)
    {
        LOG_INFO("module", "RoguelikeMgr: Run {} — all players offline, abandoning.", rid);
        EndRun(rid, false);
    }
}

// LEADERBOARD

void RoguelikeMgr::SaveRoguelikeLeaderboard(RoguelikeRun const& run)
{
    RoguelikeLeaderboardEntry e;
    if (GameTime::GetGameTime().count() > static_cast<time_t>(run.RunStartTime))
        e.RunDuration = static_cast<uint32>(GameTime::GetGameTime().count() - run.RunStartTime);

    e.Guid            = run.LeaderGuid.GetCounter();
    e.CharName        = "Unknown";
    if (Player* leader = ObjectAccessor::FindPlayer(run.LeaderGuid))
        e.CharName = leader->GetName();
    e.TierReached     = run.CurrentTier;
    e.DungeonsCleared = run.DungeonsCleared;
    e.TotalKills      = run.TotalMobsKilled + run.TotalBossesKilled;
    e.TotalBosses     = run.TotalBossesKilled;
    e.TotalDeaths     = run.TotalDeaths;
    e.PartySize       = static_cast<uint8>(run.Players.size());

    std::string safeName = e.CharName;
    CharacterDatabase.EscapeString(safeName);
    CharacterDatabase.Execute(
        "INSERT INTO dm_roguelike_leaderboard "
        "(guid, char_name, tier_reached, dungeons_cleared, total_kills, "
        "total_bosses, total_deaths, run_duration, party_size) "
        "VALUES ({}, '{}', {}, {}, {}, {}, {}, {}, {})",
        e.Guid, safeName, e.TierReached, e.DungeonsCleared, e.TotalKills,
        e.TotalBosses, e.TotalDeaths, e.RunDuration, e.PartySize);

    {
        std::lock_guard<std::mutex> lock(_leaderboardMutex);
        _leaderboard.push_back(std::move(e));
    }

    // Also update per-player roguelike stats
    UpdateRoguelikePlayerStats(run);
}

// Read from the NPC menu (a map thread): served from memory, loaded once at
// startup and appended on save, instead of a synchronous query per menu open.
void RoguelikeMgr::LoadRoguelikeLeaderboard()
{
    std::vector<RoguelikeLeaderboardEntry> rows;

    if (QueryResult result = CharacterDatabase.Query(
            "SELECT id, guid, char_name, tier_reached, dungeons_cleared, "
            "total_kills, total_bosses, total_deaths, run_duration, party_size "
            "FROM dm_roguelike_leaderboard"))
    {
        do
        {
            Field* f = result->Fetch();
            RoguelikeLeaderboardEntry e;
            e.Id              = f[0].Get<uint32>();
            e.Guid            = f[1].Get<uint32>();
            e.CharName        = f[2].Get<std::string>();
            e.TierReached     = f[3].Get<uint32>();
            e.DungeonsCleared = f[4].Get<uint32>();
            e.TotalKills      = f[5].Get<uint32>();
            e.TotalBosses     = f[6].Get<uint32>();
            e.TotalDeaths     = f[7].Get<uint32>();
            e.RunDuration     = f[8].Get<uint32>();
            e.PartySize       = f[9].Get<uint8>();
            rows.push_back(std::move(e));
        } while (result->NextRow());
    }

    std::lock_guard<std::mutex> lock(_leaderboardMutex);
    _leaderboard = std::move(rows);
}

std::vector<RoguelikeLeaderboardEntry> RoguelikeMgr::GetRoguelikeLeaderboard(
    uint32 limit, bool sortByFloors) const
{
    std::vector<RoguelikeLeaderboardEntry> entries;
    {
        std::lock_guard<std::mutex> lock(_leaderboardMutex);
        entries = _leaderboard;
    }

    // Same orderings as the former SQL ORDER BY clauses.
    std::sort(entries.begin(), entries.end(),
        [sortByFloors](RoguelikeLeaderboardEntry const& a, RoguelikeLeaderboardEntry const& b)
        {
            uint32 const aFirst  = sortByFloors ? a.DungeonsCleared : a.TierReached;
            uint32 const bFirst  = sortByFloors ? b.DungeonsCleared : b.TierReached;
            uint32 const aSecond = sortByFloors ? a.TierReached : a.DungeonsCleared;
            uint32 const bSecond = sortByFloors ? b.TierReached : b.DungeonsCleared;

            if (aFirst != bFirst)
                return aFirst > bFirst;
            if (aSecond != bSecond)
                return aSecond > bSecond;
            return a.RunDuration < b.RunDuration;
        });

    if (entries.size() > limit)
        entries.resize(limit);
    return entries;
}

// ---------------------------------------------------------------------------
// Roguelike Player Stats
// ---------------------------------------------------------------------------

void RoguelikeMgr::LoadAllRoguelikePlayerStats()
{
    std::lock_guard<std::mutex> lock(_rlStatsMutex);
    _roguelikeStats.clear();

    QueryResult result = CharacterDatabase.Query(
        "SELECT guid, total_runs, highest_tier, most_floors_cleared, "
        "total_floors_cleared, total_mobs_killed, total_bosses_killed, "
        "total_deaths, longest_run_time "
        "FROM dm_roguelike_player_stats");

    if (!result)
    {
        LOG_INFO("module", "RoguelikeMgr: No roguelike player stats found.");
        return;
    }

    uint32 count = 0;
    do
    {
        Field* f = result->Fetch();
        uint32 guidLow = f[0].Get<uint32>();

        RoguelikePlayerStats ps;
        ps.TotalRuns          = f[1].Get<uint32>();
        ps.HighestTier        = f[2].Get<uint32>();
        ps.MostFloorsCleared  = f[3].Get<uint32>();
        ps.TotalFloorsCleared = f[4].Get<uint32>();
        ps.TotalMobsKilled    = f[5].Get<uint32>();
        ps.TotalBossesKilled  = f[6].Get<uint32>();
        ps.TotalDeaths        = f[7].Get<uint32>();
        ps.LongestRunTime     = f[8].Get<uint32>();

        _roguelikeStats[guidLow] = ps;
        ++count;
    } while (result->NextRow());

    LOG_INFO("module", "RoguelikeMgr: Loaded roguelike stats for {} players.", count);
}

RoguelikePlayerStats RoguelikeMgr::GetRoguelikePlayerStats(ObjectGuid guid) const
{
    std::lock_guard<std::mutex> lock(_rlStatsMutex);
    uint32 guidLow = guid.GetCounter();
    auto it = _roguelikeStats.find(guidLow);
    if (it != _roguelikeStats.end())
        return it->second;
    return {};
}

void RoguelikeMgr::UpdateRoguelikePlayerStats(RoguelikeRun const& run)
{
    uint32 duration = 0;
    if (GameTime::GetGameTime().count() > static_cast<time_t>(run.RunStartTime))
        duration = static_cast<uint32>(GameTime::GetGameTime().count() - run.RunStartTime);

    for (auto const& pd : run.Players)
    {
        uint32 guidLow = pd.PlayerGuid.GetCounter();

        {
            std::lock_guard<std::mutex> lock(_rlStatsMutex);
            auto& ps = _roguelikeStats[guidLow];
            ps.TotalRuns++;
            if (run.CurrentTier > ps.HighestTier)
                ps.HighestTier = run.CurrentTier;
            if (run.DungeonsCleared > ps.MostFloorsCleared)
                ps.MostFloorsCleared = run.DungeonsCleared;
            ps.TotalFloorsCleared += run.DungeonsCleared;
            ps.TotalMobsKilled    += run.TotalMobsKilled;
            ps.TotalBossesKilled  += run.TotalBossesKilled;
            ps.TotalDeaths        += run.TotalDeaths;
            if (duration > ps.LongestRunTime)
                ps.LongestRunTime = duration;
        }

        // Persist
        RoguelikePlayerStats ps;
        {
            std::lock_guard<std::mutex> lock(_rlStatsMutex);
            ps = _roguelikeStats[guidLow];
        }

        CharacterDatabase.Execute(
            "REPLACE INTO dm_roguelike_player_stats "
            "(guid, total_runs, highest_tier, most_floors_cleared, "
            "total_floors_cleared, total_mobs_killed, total_bosses_killed, "
            "total_deaths, longest_run_time) "
            "VALUES ({}, {}, {}, {}, {}, {}, {}, {}, {})",
            guidLow, ps.TotalRuns, ps.HighestTier, ps.MostFloorsCleared,
            ps.TotalFloorsCleared, ps.TotalMobsKilled, ps.TotalBossesKilled,
            ps.TotalDeaths, ps.LongestRunTime);
    }
}

} // namespace DungeonMaster
