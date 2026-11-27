/*
 * mod-atlas — AtlasMgr.cpp
 * Run (session) lifecycle: creation, binds, teleports, deaths, end of run,
 * cooldowns, entry checks and the update loop.
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
    sAtlasAffixes->Load();
    LoadThemes();
    LoadCreaturePools();
    LoadDungeonBossPool();
    LoadRewardItems();
    LoadLootPool();
    LoadTierGear();
    LoadLootTiers();
    LoadAtmosphere();
    LoadAllPlayerStats();
    LoadLeaderboard();
    LoadCloneMaps();
    LoadDungeonGeometry();
    LoadMapItems();
    LoadCloneOptIns();
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
                                          bool scaleToParty, uint64 seed)
{
    DifficultyTier const* diff  = sAtlasConfig->GetDifficulty(difficultyId);
    Theme const*          theme = sAtlasConfig->GetTheme(themeId);
    DungeonInfo const*    dg    = sAtlasConfig->GetDungeon(mapId);
    if (!diff || !theme || !dg)
        return nullptr;

    if (!seed)
        seed = TakePendingSeed(leader->GetGUID());
    if (!seed)
        seed = RunRng::MakeSeed();

    // GM-forced affixes (`.atlas affix next`); echoes roll none of their own.
    // Endless floors get theirs from RoguelikeMgr after this.
    std::vector<ActiveAffix> forced = sAtlasAffixes->TakePending(leader->GetGUID());

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
    s.SourceMapId  = mapId;
    s.ScaleToParty = scaleToParty;
    s.StartTime    = GameTime::GetGameTime().count();
    s.Seed         = seed;
    s.Rng.Seed(seed);
    SetSessionAffixes(&s, sAtlasAffixes->AtTier(forced, s.Tier));

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

    LOG_INFO("module", "Atlas: Session {} — leader {}, party {}, diff {}, level band {}-{}, scale={}, seed {:016X}",
        s.SessionId, leader->GetName(), s.Players.size(),
        diff->Name, s.LevelBandMin, s.LevelBandMax, scaleToParty ? "party" : "tier", s.Seed);

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
    QueryResult result = CharacterDatabase.Query("SELECT instance_id FROM atlas_run_active");
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

    // Log them as interrupted (result 3) before forgetting them, in one
    // transaction so the copy runs before the delete.
    CharacterDatabaseTransaction trans = CharacterDatabase.BeginTransaction();
    trans->Append(
        "INSERT INTO atlas_run_log (kind, endless_run_id, depth, endless_seed, seed, map_id, difficulty_id, "
        "theme_id, leader_guid, party_size, effective_level, scaled, result, started_at) "
        "SELECT kind, endless_run_id, depth, endless_seed, seed, map_id, difficulty_id, theme_id, leader_guid, "
        "party_size, effective_level, scaled, 3, started_at FROM atlas_run_active");
    trans->Append("DELETE FROM atlas_run_active");
    CharacterDatabase.CommitTransaction(trans);

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

    CharacterDatabase.Execute("DELETE FROM atlas_run_active WHERE instance_id = {}", session.InstanceId);

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

            if (!session->Affixes.empty())
                ChatHandler(p->GetSession()).PSendSysMessage("{} This echo is unstable: {}",
                    session->RoguelikeRunId ? CHAT_PREFIX_ENDLESS : CHAT_PREFIX,
                    sAtlasAffixes->Describe(session->Affixes, true));
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
        // A map run's members may already be back at the Lens.
        if (session->IsMapRun && p->GetMapId() != session->MapId) continue;
        p->RemoveFlag(PLAYER_FIELD_BYTES, PLAYER_FIELD_BYTE_NO_RELEASE_WINDOW);
        if (!p->IsAlive()) { p->ResurrectPlayer(1.0f); p->SpawnCorpseBones(); }
        p->TeleportTo(pd.ReturnMapId, pd.ReturnPosition.GetPositionX(),
                      pd.ReturnPosition.GetPositionY(), pd.ReturnPosition.GetPositionZ(),
                      pd.ReturnPosition.GetOrientation());
    }
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

    // A map run counts only those inside: members waiting at the Lens don't
    // keep it alive, and the dead inside can be pulled back while rifts last.
    bool wiped = session->IsPartyWiped();
    if (session->IsMapRun)
    {
        wiped = true;
        for (auto const& psd : session->Players)
            if (Player* p = ObjectAccessor::FindPlayer(psd.PlayerGuid))
                if (p->IsAlive() && p->GetMapId() == session->MapId)
                    wiped = false;
    }

    if (wiped && session->IsMapRun && session->PortalsLeft > 0)
    {
        ++session->Wipes;
        --session->PortalsLeft;
        std::string const text = Acore::StringFormat("{} The echo has claimed you all. The Keepers pull you back "
            "to its entrance at the cost of a rift: |cFFFFFFFF{}|r left.", CHAT_PREFIX, session->PortalsLeft);
        for (auto const& psd : session->Players)
            if (Player* p = ObjectAccessor::FindPlayer(psd.PlayerGuid))
                if (p->GetSession() && p->GetMapId() == session->MapId)
                    ChatHandler(p->GetSession()).SendSysMessage(text);
        return;
    }

    if (wiped)
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
            PushRunEnded(s);
            UpdatePlayerStatsFromSession(s, success);
            LogRun(s, success);
            RemoveSessionAffixAuras(s);
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

    bool const mapRun = s.IsMapRun;
    bool const mended = success && s.State == SessionState::Completed;
    char const* farewell = mended
        ? (mapRun ? "The echo closes behind you, mended." : "The echo is mended. The Keepers of Time thank you.")
        : (!mapRun ? "The echo fades unmended. There is no reward this time."
            : s.Entered.empty() ? "The rift closes before anyone crosses."
            : "The echo is lost, and the sigil with it.");
    for (auto const& pd : s.Players)
        if (Player* p = ObjectAccessor::FindPlayer(pd.PlayerGuid))
            if (p->GetSession())
                ChatHandler(p->GetSession()).PSendSysMessage("{} {}", CHAT_PREFIX, farewell);

    if (mended && mapRun)
    {
        // Shares left in the cache follow their players (queued, by mail at logout).
        for (auto const& pd : s.Players)
            if (Player* p = ObjectAccessor::FindPlayer(pd.PlayerGuid))
                if (IsRealPlayer(p) && !Session::Contains(s.CacheClaimed, pd.PlayerGuid))
                {
                    s.CacheClaimed.push_back(pd.PlayerGuid);
                    GiveMapCompletionReward(s, p);
                }
    }
    else if (mended)
        DistributeRewards(&s);

    UpdatePlayerStatsFromSession(s, success);
    if (mended && !mapRun)
        SaveLeaderboardEntry(s);

    LogRun(s, success);
    PushRunEnded(s);
    RemoveSessionAffixAuras(s);
    TeleportPartyOut(&s);
    ReleaseRunInstanceBinds(s);

    // Save instance ID before cleanup
    uint32 savedInstanceId = s.InstanceId;
    CleanupSession(s);

    // Maps cost a sigil; only echoes have a cooldown.
    if (!mapRun)
        for (auto const& pd : s.Players)
            SetCooldown(pd.PlayerGuid);

    if (savedInstanceId)   // rifts, caches, a master still to come
    {
        std::lock_guard<std::mutex> spawnLock(_mapSpawnMutex);
        auto sit = _mapSpawns.find(savedInstanceId);
        if (sit != _mapSpawns.end())
        {
            _mapSpawnCount.fetch_sub(static_cast<uint32>(sit->second.size()), std::memory_order_relaxed);
            _mapSpawns.erase(sit);
        }
    }

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
    LogRun(s, success);
    PushRunEnded(s);
    RemoveSessionAffixAuras(s);
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

void AtlasMgr::RemoveSessionAffixAuras(Session const& s)
{
    for (auto const& pd : s.Players)
        if (Player* p = ObjectAccessor::FindPlayer(pd.PlayerGuid))
            sAtlasAffixes->RemovePlayerAuras(p);
}

void AtlasMgr::CleanupSession(Session& s)
{
    // Release level ownership before the instance id is cleared, otherwise the
    // map grows for the lifetime of the process.
    if (s.InstanceId)
        ForgetOwnedCreaturesForInstance(s.InstanceId);

    s.InstanceId = 0;
}

// GM replay seeds

void AtlasMgr::SetPendingSeed(ObjectGuid guid, uint64 seed)
{
    std::lock_guard<std::mutex> lock(_seedMutex);
    if (seed)
        _pendingSeeds[guid] = seed;
    else
        _pendingSeeds.erase(guid);
}

uint64 AtlasMgr::TakePendingSeed(ObjectGuid guid)
{
    std::lock_guard<std::mutex> lock(_seedMutex);
    auto it = _pendingSeeds.find(guid);
    if (it == _pendingSeeds.end())
        return 0;
    uint64 const seed = it->second;
    _pendingSeeds.erase(it);
    return seed;
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

// Main update tick (1s interval)
void AtlasMgr::Update(uint32 diff)
{
    _updateTimer += diff;
    if (_updateTimer < UPDATE_INTERVAL)
        return;
    _updateTimer = 0;

    std::vector<std::pair<uint32, bool>> toEnd;
    std::vector<std::pair<uint32, uint32>> roguelikeCompleted; // {runId, sessionId}
    std::vector<GroundRequest> groundRequests;                 // cast after the lock

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

                                std::string const ready = PopulationNotice(session);
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

                    // ---- The master's bar: called once enough forces fell ----
                    UpdateForces(session);

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
                            if (session.IsMapRun && session.IsActive() && session.TotalBosses > 0
                                && session.BossesKilled >= session.TotalBosses)
                            {
                                CompleteMapRun(session, ppc.DeathPos);
                                break;
                            }

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

            // ---- Affixes: player auras, ground timers ----
            if (session.IsActive() && !session.Affixes.empty())
                TickSessionAffixes(session, groundRequests);

            // ---- The run HUD: the header when something changed ----
            PushRunHud(session, false);

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

            // ---- Map run: occupancy, rifts, the end once mended ----
            if (session.IsMapRun)
            {
                uint64 const now = GameTime::GetGameTime().count();
                bool anyone = false;
                bool anyoneReal = false;
                for (auto const& pd : session.Players)
                    if (Player* p = ObjectAccessor::FindPlayer(pd.PlayerGuid))
                        if (p->GetMapId() == session.MapId)
                        {
                            anyone = true;
                            anyoneReal = anyoneReal || IsRealPlayer(p);
                        }
                if (anyone)
                    session.LastOccupied = now;

                // Cleared packs: their sigil chances (PLAN 1.5).
                if (session.IsActive())
                    RollPackSigils(session);

                if (session.State == SessionState::Completed)
                {
                    // Closes once every player has left (bots go with it), or
                    // after Atlas.Map.CompletedTimeout.
                    if (!anyoneReal || now - session.EndTime >= sAtlasConfig->GetMapCompletedTimeoutSec())
                        toEnd.emplace_back(sid, true);
                    continue;
                }

                if (session.IsActive() && !anyone)
                {
                    std::string reason;
                    if (!session.PortalsLeft && !session.Entered.empty())
                        reason = "The last rift has closed behind you.";
                    else if (now - session.LastOccupied >= sAtlasConfig->GetMapEmptyTimeoutSec())
                        reason = "Left unwatched, the echo drifts away.";
                    if (!reason.empty())
                    {
                        LOG_INFO("module", "Atlas: Map run {} lost — {} (rifts left {}).", sid, reason,
                            session.PortalsLeft);
                        for (auto const& pd : session.Players)
                            if (Player* p = ObjectAccessor::FindPlayer(pd.PlayerGuid))
                                if (p->GetSession())
                                    ChatHandler(p->GetSession()).PSendSysMessage("{} {}", CHAT_PREFIX, reason);
                        session.State = SessionState::Abandoned;
                        toEnd.emplace_back(sid, false);
                        continue;
                    }
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

            // ---- Abandoned detection (map runs: above) ----
            if (session.IsActive() && !session.IsMapRun
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

    ExecuteGroundRequests(groundRequests);

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
    return Acore::StringFormat("Session {} — {}, map {}, Mobs {}/{}, Bosses {}/{}, Band {}-{}, seed {:016X}",
        s->SessionId, names[static_cast<int>(s->State)], s->MapId,
        s->MobsKilled, s->TotalMobs, s->BossesKilled, s->TotalBosses,
        s->LevelBandMin, s->LevelBandMax, s->Seed);
}

} // namespace Atlas
