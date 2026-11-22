/*
 * mod-dungeon-master — npc_dungeon_master.cpp
 * Gossip NPC (Chronicler Esmadormu, Keepers of Time): menu flow for
 * difficulty / theme / dungeon selection of an "echo" run.
 *
 * Lore frame: the bronze dragonflight opens echoes of known dungeons from
 * fractured timelines, where the halls belong to other masters (the chosen
 * creature theme). The endless mode descends through ever less stable
 * timeways. Internal names (classes, config keys) keep "DungeonMaster".
 */

#include "ScriptMgr.h"
#include "ScriptedCreature.h"
#include "ScriptedGossip.h"
#include "GossipDef.h"
#include "Player.h"
#include "Creature.h"
#include "Group.h"
#include "Log.h"
#include "Chat.h"
#include "ObjectAccessor.h"
#include "Random.h"
#include "StringFormat.h"
#include "DungeonMasterMgr.h"
#include "RoguelikeMgr.h"
#include "RoguelikeTypes.h"
#include "DMConfig.h"
#include <algorithm>
#include <mutex>

using namespace DungeonMaster;

// Gossip action IDs (encoded so ranges never overlap)
enum DMGossipActions
{
    GOSSIP_ACTION_MAIN_START    = 1,
    GOSSIP_ACTION_MAIN_INFO     = 2,
    GOSSIP_ACTION_MAIN_STATS    = 3,   // unused legacy, kept for safety

    GOSSIP_ACTION_DIFF_BASE     = 100,   // +diffId
    GOSSIP_ACTION_THEME_BASE    = 200,   // +themeId
    GOSSIP_ACTION_DUNGEON_BASE  = 300,   // +mapId (maps go up to ~700)
    GOSSIP_ACTION_DUNGEON_RANDOM = 10000,

    GOSSIP_ACTION_CONFIRM       = 10001,
    GOSSIP_ACTION_CANCEL        = 10002,
    GOSSIP_ACTION_SCALE_PARTY   = 10003,
    GOSSIP_ACTION_SCALE_TIER    = 10004,
    GOSSIP_ACTION_LEADERBOARD   = 10005, // legacy — redirects to board menu

    // Dungeon list pagination (free slots, outside the [300,10001) dungeon dispatch range)
    GOSSIP_ACTION_DUNGEON_PREV_PAGE = 10006, // dungeon list - previous page
    GOSSIP_ACTION_DUNGEON_NEXT_PAGE = 10007, // dungeon list - next page

    // Endless Timeways (roguelike mode)
    GOSSIP_ACTION_ROGUELIKE_START       = 10010,
    GOSSIP_ACTION_ROGUELIKE_SCALE_PARTY = 10011,
    GOSSIP_ACTION_ROGUELIKE_SCALE_TIER  = 10012,
    GOSSIP_ACTION_ROGUELIKE_THEME       = 10100, // +themeId
    GOSSIP_ACTION_ROGUELIKE_QUIT        = 10200,
    GOSSIP_ACTION_ROGUELIKE_BOARD       = 10201,

    // Statistics & Leaderboards sub-menus
    GOSSIP_ACTION_STATS_MENU          = 10300,  // Chronicles hub
    GOSSIP_ACTION_STATS_NORMAL        = 10301,  // My echo record
    GOSSIP_ACTION_STATS_ROGUELIKE     = 10302,  // My Endless Timeways record
    GOSSIP_ACTION_BOARD_MENU          = 10310,  // Leaderboards hub
    GOSSIP_ACTION_BOARD_NORMAL        = 10311,  // Echoes — fastest
    GOSSIP_ACTION_BOARD_RL_TIER       = 10312,  // Timeways — deepest
    GOSSIP_ACTION_BOARD_RL_FLOORS     = 10313,  // Timeways — most echoes
};

namespace
{
    // Greeting text (npc_text row in dm_setup.sql).
    constexpr uint32 NPC_TEXT_GREETING = 500000;

    std::string FormatTime(uint32 seconds)
    {
        if (seconds >= 3600)
            return Acore::StringFormat("{}h {:02}m {:02}s", seconds / 3600, (seconds % 3600) / 60, seconds % 60);

        return Acore::StringFormat("{}m {:02}s", seconds / 60, seconds % 60);
    }

    char const* Plural(uint32 n)
    {
        return n != 1 ? "s" : "";
    }
}

struct PlayerDMSelection
{
    uint32 DifficultyId  = 0;
    uint32 ThemeId       = 0;
    uint32 MapId         = 0;
    bool   ScaleToParty  = true;
    bool   IsRoguelike   = false;
    uint32 DungeonPage   = 0;   // current page in the (paginated) dungeon menu
};

static std::unordered_map<ObjectGuid, PlayerDMSelection> sSelections;
static std::mutex sSelMutex;

class npc_dungeon_master : public CreatureScript
{
public:
    npc_dungeon_master() : CreatureScript("npc_dungeon_master") { }

    bool OnGossipHello(Player* player, Creature* creature) override
    {
        ChatHandler chat(player->GetSession());

        if (!sDMConfig->IsEnabled())
        {
            chat.PSendSysMessage("{} The timeways are closed for now.", CHAT_PREFIX);
            player->PlayerTalkClass->SendCloseGossip();
            return true;
        }

        if (sDungeonMasterMgr->GetSessionByPlayer(player->GetGUID()))
        {
            LOG_INFO("module", "DungeonMaster: NPC blocked {} — still in active session", player->GetName());
            chat.PSendSysMessage("{} You are already walking an echo.", CHAT_PREFIX);
            player->PlayerTalkClass->SendCloseGossip();
            return true;
        }

        if (sRoguelikeMgr->IsPlayerInRun(player->GetGUID()))
        {
            player->PlayerTalkClass->ClearMenus();

            // Player is in an active Endless Timeways run (auto-transitions between echoes)
            if (RoguelikeRun* run = sRoguelikeMgr->GetRunByPlayer(player->GetGUID()))
                chat.PSendSysMessage("{} Descent in progress — |cFFFFD700depth {}|r, |cFFFFFFFF{}|r echo{} mended.",
                    CHAT_PREFIX_ENDLESS, run->CurrentTier, run->DungeonsCleared, Plural(run->DungeonsCleared));
            else
                chat.PSendSysMessage("{} You are still bound to the Endless Timeways.", CHAT_PREFIX_ENDLESS);

            AddGossipItemFor(player, GOSSIP_ICON_BATTLE, "|cFFFF0000Abandon the descent|r",
                GOSSIP_SENDER_MAIN, GOSSIP_ACTION_ROGUELIKE_QUIT);
            AddGossipItemFor(player, GOSSIP_ICON_CHAT, "Never mind.",
                GOSSIP_SENDER_MAIN, GOSSIP_ACTION_CANCEL);

            SendGossipMenuFor(player, NPC_TEXT_GREETING, creature->GetGUID());
            return true;
        }

        if (sDungeonMasterMgr->IsOnCooldown(player->GetGUID()))
        {
            uint32 rem = sDungeonMasterMgr->GetRemainingCooldown(player->GetGUID());
            LOG_INFO("module", "DungeonMaster: NPC blocked {} — cooldown {}s remaining", player->GetName(), rem);
            chat.PSendSysMessage(
                "{} The timeways need |cFFFFFFFF{}|r min |cFFFFFFFF{}|r sec to settle before your next echo.",
                CHAT_PREFIX, rem / 60, rem % 60);
            player->PlayerTalkClass->SendCloseGossip();
            return true;
        }

        ShowMainMenu(player, creature);
        return true;
    }

    bool OnGossipSelect(Player* player, Creature* creature, uint32 /*sender*/, uint32 action) override
    {
        player->PlayerTalkClass->ClearMenus();
        ChatHandler chat(player->GetSession());

        if (action == GOSSIP_ACTION_MAIN_START)
        {
            if (!sDungeonMasterMgr->CanCreateNewSession())
            {
                chat.PSendSysMessage("{} Too many echoes are open at once. Return shortly.", CHAT_PREFIX);
                player->PlayerTalkClass->SendCloseGossip();
                return true;
            }
            { std::lock_guard<std::mutex> lk(sSelMutex); sSelections[player->GetGUID()] = {}; }
            ShowDifficultyMenu(player, creature);
        }
        else if (action == GOSSIP_ACTION_MAIN_INFO)
            ShowInfoMenu(player, creature);
        else if (action == GOSSIP_ACTION_MAIN_STATS)
            ShowStatsAndBoardsMenu(player, creature);
        else if (action == GOSSIP_ACTION_LEADERBOARD)
            ShowBoardMenu(player, creature);
        else if (action >= GOSSIP_ACTION_DIFF_BASE && action < GOSSIP_ACTION_THEME_BASE)
        {
            uint32 diffId = action - GOSSIP_ACTION_DIFF_BASE;
            bool isRoguelike = false;
            { std::lock_guard<std::mutex> lk(sSelMutex);
              sSelections[player->GetGUID()].DifficultyId = diffId;
              isRoguelike = sSelections[player->GetGUID()].IsRoguelike; }
            if (isRoguelike)
                ShowRoguelikeScalingMenu(player, creature);
            else
                ShowScalingMenu(player, creature);
        }
        else if (action == GOSSIP_ACTION_SCALE_PARTY)
        {
            { std::lock_guard<std::mutex> lk(sSelMutex); sSelections[player->GetGUID()].ScaleToParty = true; }
            ShowThemeMenu(player, creature);
        }
        else if (action == GOSSIP_ACTION_SCALE_TIER)
        {
            { std::lock_guard<std::mutex> lk(sSelMutex); sSelections[player->GetGUID()].ScaleToParty = false; }
            ShowThemeMenu(player, creature);
        }
        else if (action >= GOSSIP_ACTION_THEME_BASE && action < GOSSIP_ACTION_DUNGEON_BASE)
        {
            { std::lock_guard<std::mutex> lk(sSelMutex);
              sSelections[player->GetGUID()].ThemeId     = action - GOSSIP_ACTION_THEME_BASE;
              sSelections[player->GetGUID()].DungeonPage = 0; }
            ShowDungeonMenu(player, creature);
        }
        else if (action == GOSSIP_ACTION_DUNGEON_PREV_PAGE)
        {
            { std::lock_guard<std::mutex> lk(sSelMutex);
              auto& s = sSelections[player->GetGUID()];
              if (s.DungeonPage > 0) --s.DungeonPage; }
            ShowDungeonMenu(player, creature);
        }
        else if (action == GOSSIP_ACTION_DUNGEON_NEXT_PAGE)
        {
            { std::lock_guard<std::mutex> lk(sSelMutex);
              ++sSelections[player->GetGUID()].DungeonPage; }
            ShowDungeonMenu(player, creature);
        }
        else if (action == GOSSIP_ACTION_DUNGEON_RANDOM)
        {
            { std::lock_guard<std::mutex> lk(sSelMutex); sSelections[player->GetGUID()].MapId = 0; }
            ShowConfirmMenu(player, creature);
        }
        else if (action >= GOSSIP_ACTION_DUNGEON_BASE && action < GOSSIP_ACTION_CONFIRM)
        {
            { std::lock_guard<std::mutex> lk(sSelMutex); sSelections[player->GetGUID()].MapId = action - GOSSIP_ACTION_DUNGEON_BASE; }
            ShowConfirmMenu(player, creature);
        }
        else if (action == GOSSIP_ACTION_CONFIRM)
            StartChallenge(player, creature);
        else if (action == GOSSIP_ACTION_CANCEL)
        {
            { std::lock_guard<std::mutex> lk(sSelMutex); sSelections.erase(player->GetGUID()); }
            ShowMainMenu(player, creature);
        }
        // ---- Endless Timeways ----
        else if (action == GOSSIP_ACTION_ROGUELIKE_START)
        {
            if (sRoguelikeMgr->IsPlayerInRun(player->GetGUID()))
            {
                chat.PSendSysMessage("{} You are already bound to the Endless Timeways.", CHAT_PREFIX_ENDLESS);
                player->PlayerTalkClass->SendCloseGossip();
                return true;
            }
            { std::lock_guard<std::mutex> lk(sSelMutex);
              sSelections[player->GetGUID()] = {};
              sSelections[player->GetGUID()].IsRoguelike = true; }
            ShowRoguelikeDifficultyMenu(player, creature);
        }
        else if (action == GOSSIP_ACTION_ROGUELIKE_SCALE_PARTY)
        {
            { std::lock_guard<std::mutex> lk(sSelMutex); sSelections[player->GetGUID()].ScaleToParty = true; }
            ShowRoguelikeThemeMenu(player, creature);
        }
        else if (action == GOSSIP_ACTION_ROGUELIKE_SCALE_TIER)
        {
            { std::lock_guard<std::mutex> lk(sSelMutex); sSelections[player->GetGUID()].ScaleToParty = false; }
            ShowRoguelikeThemeMenu(player, creature);
        }
        else if (action >= GOSSIP_ACTION_ROGUELIKE_THEME && action < GOSSIP_ACTION_ROGUELIKE_QUIT)
        {
            uint32 themeId = action - GOSSIP_ACTION_ROGUELIKE_THEME;
            { std::lock_guard<std::mutex> lk(sSelMutex); sSelections[player->GetGUID()].ThemeId = themeId; }
            StartRoguelike(player, creature);
        }
        else if (action == GOSSIP_ACTION_ROGUELIKE_QUIT)
        {
            if (sRoguelikeMgr->IsPlayerInRun(player->GetGUID()))
            {
                sRoguelikeMgr->QuitRun(player->GetGUID());
                chat.PSendSysMessage("{} You step out of the timeways. The descent is over.", CHAT_PREFIX_ENDLESS);
            }
            player->PlayerTalkClass->SendCloseGossip();
        }
        else if (action == GOSSIP_ACTION_ROGUELIKE_BOARD)
            ShowRoguelikeLeaderboard(player, creature, false);
        // ---- Chronicles (statistics & leaderboards) ----
        else if (action == GOSSIP_ACTION_STATS_MENU)
            ShowStatsAndBoardsMenu(player, creature);
        else if (action == GOSSIP_ACTION_STATS_NORMAL)
            ShowNormalStats(player, creature);
        else if (action == GOSSIP_ACTION_STATS_ROGUELIKE)
            ShowRoguelikeStats(player, creature);
        else if (action == GOSSIP_ACTION_BOARD_MENU)
            ShowBoardMenu(player, creature);
        else if (action == GOSSIP_ACTION_BOARD_NORMAL)
            ShowNormalLeaderboard(player, creature);
        else if (action == GOSSIP_ACTION_BOARD_RL_TIER)
            ShowRoguelikeLeaderboard(player, creature, false);
        else if (action == GOSSIP_ACTION_BOARD_RL_FLOORS)
            ShowRoguelikeLeaderboard(player, creature, true);

        return true;
    }

private:
    // ---- Menu builders ----

    void ShowMainMenu(Player* player, Creature* creature)
    {
        player->PlayerTalkClass->ClearMenus();
        AddGossipItemFor(player, GOSSIP_ICON_BATTLE, "Show me an echo to walk.",
            GOSSIP_SENDER_MAIN, GOSSIP_ACTION_MAIN_START);
        if (sDMConfig->IsRoguelikeEnabled())
            AddGossipItemFor(player, GOSSIP_ICON_BATTLE, "|cFFCD7F32I will descend the Endless Timeways.|r",
                GOSSIP_SENDER_MAIN, GOSSIP_ACTION_ROGUELIKE_START);
        AddGossipItemFor(player, GOSSIP_ICON_CHAT, "What are these echoes?",
            GOSSIP_SENDER_MAIN, GOSSIP_ACTION_MAIN_INFO);
        AddGossipItemFor(player, GOSSIP_ICON_TABARD, "|cFFFFD700Show me the chronicles.|r",
            GOSSIP_SENDER_MAIN, GOSSIP_ACTION_STATS_MENU);
        SendGossipMenuFor(player, NPC_TEXT_GREETING, creature->GetGUID());
    }

    static std::string DifficultyLabel(DifficultyTier const& d, uint8 lvl, bool roguelike)
    {
        if (!d.IsValidForLevel(lvl))
            return Acore::StringFormat("|cFF808080{}|r (Lv {}-{}) - |cFFFF0000Requires {}+|r",
                d.Name, d.MinLevel, d.MaxLevel, d.MinLevel);

        if (roguelike)
            return Acore::StringFormat("|cFFCD7F32{}|r (Lv {}-{})", d.Name, d.MinLevel, d.MaxLevel);

        if (!d.IsOnLevelFor(lvl))
            return Acore::StringFormat("{} |cFF808080(Lv {}-{} — Easy)|r", d.Name, d.MinLevel, d.MaxLevel);

        return Acore::StringFormat("|cFF00FF00{}|r (Lv {}-{})", d.Name, d.MinLevel, d.MaxLevel);
    }

    void ShowDifficultyMenu(Player* player, Creature* creature)
    {
        player->PlayerTalkClass->ClearMenus();
        uint8 lvl = player->GetLevel();

        for (auto const& d : sDMConfig->GetDifficulties())
            AddGossipItemFor(player,
                d.IsValidForLevel(lvl) ? GOSSIP_ICON_BATTLE : GOSSIP_ICON_CHAT,
                DifficultyLabel(d, lvl, false), GOSSIP_SENDER_MAIN, GOSSIP_ACTION_DIFF_BASE + d.Id);

        AddGossipItemFor(player, GOSSIP_ICON_CHAT, "|cFFFF0000<< Back|r", GOSSIP_SENDER_MAIN, GOSSIP_ACTION_CANCEL);
        SendGossipMenuFor(player, NPC_TEXT_GREETING, creature->GetGUID());
    }

    void ShowScalingMenu(Player* player, Creature* creature)
    {
        player->PlayerTalkClass->ClearMenus();

        PlayerDMSelection sel;
        { std::lock_guard<std::mutex> lk(sSelMutex);
          auto it = sSelections.find(player->GetGUID());
          if (it == sSelections.end()) { player->PlayerTalkClass->SendCloseGossip(); return; }
          sel = it->second; }

        DifficultyTier const* diff = sDMConfig->GetDifficulty(sel.DifficultyId);
        if (!diff) { player->PlayerTalkClass->SendCloseGossip(); return; }

        uint8 partyLevel = sDungeonMasterMgr->ComputeEffectiveLevel(player);

        AddGossipItemFor(player, GOSSIP_ICON_BATTLE,
            Acore::StringFormat("|cFF00FF00Attune the echo to my party|r (Lv {}) — a full challenge at your level",
                partyLevel),
            GOSSIP_SENDER_MAIN, GOSSIP_ACTION_SCALE_PARTY);
        AddGossipItemFor(player, GOSSIP_ICON_INTERACT_1,
            Acore::StringFormat("|cFFFFD700Leave the echo as it was|r (Lv {}-{}) — the original level range",
                diff->MinLevel, diff->MaxLevel),
            GOSSIP_SENDER_MAIN, GOSSIP_ACTION_SCALE_TIER);
        AddGossipItemFor(player, GOSSIP_ICON_CHAT, "|cFFFF0000<< Back|r",
            GOSSIP_SENDER_MAIN, GOSSIP_ACTION_CANCEL);
        SendGossipMenuFor(player, NPC_TEXT_GREETING, creature->GetGUID());
    }

    void ShowThemeMenu(Player* player, Creature* creature)
    {
        player->PlayerTalkClass->ClearMenus();
        for (auto const& t : sDMConfig->GetThemes())
            AddGossipItemFor(player, GOSSIP_ICON_BATTLE, t.Name, GOSSIP_SENDER_MAIN, GOSSIP_ACTION_THEME_BASE + t.Id);
        AddGossipItemFor(player, GOSSIP_ICON_CHAT, "|cFFFF0000<< Back|r", GOSSIP_SENDER_MAIN, GOSSIP_ACTION_CANCEL);
        SendGossipMenuFor(player, NPC_TEXT_GREETING, creature->GetGUID());
    }

    void ShowDungeonMenu(Player* player, Creature* creature)
    {
        player->PlayerTalkClass->ClearMenus();

        // Dungeons per page. Worst-case items rendered per page:
        //   page 0:   Random(1) + DUNGEONS_PER_PAGE + Next(<=1) + Back(1)
        //   page >0:  Prev(1)   + DUNGEONS_PER_PAGE + Next(<=1) + Back(1)
        // With DUNGEONS_PER_PAGE = 24 the max is 27 <= GOSSIP_MAX_MENU_ITEMS(32).
        static uint32 const DUNGEONS_PER_PAGE = 24;

        uint32 diffId;
        uint32 page;
        { std::lock_guard<std::mutex> lk(sSelMutex);
          auto it = sSelections.find(player->GetGUID());
          if (it == sSelections.end()) { player->PlayerTalkClass->SendCloseGossip(); return; }
          diffId = it->second.DifficultyId;
          page   = it->second.DungeonPage; }

        DifficultyTier const* diff = sDMConfig->GetDifficulty(diffId);
        if (!diff) { player->PlayerTalkClass->SendCloseGossip(); return; }

        auto dungeons = sDMConfig->GetDungeonsForLevel(diff->MinLevel, diff->MaxLevel);

        // Clamp page in case the dungeon list shrank or page ran past the end.
        uint32 total      = static_cast<uint32>(dungeons.size());
        uint32 totalPages = total ? ((total + DUNGEONS_PER_PAGE - 1) / DUNGEONS_PER_PAGE) : 1;
        if (page >= totalPages)
        {
            page = totalPages - 1;
            std::lock_guard<std::mutex> lk(sSelMutex);
            auto it = sSelections.find(player->GetGUID());
            if (it != sSelections.end())
                it->second.DungeonPage = page;
        }

        // "Any echo" stays reachable: shown only on the first page.
        if (page == 0)
            AddGossipItemFor(player, GOSSIP_ICON_BATTLE, "|cFFFFD700Let the timeways choose.|r",
                GOSSIP_SENDER_MAIN, GOSSIP_ACTION_DUNGEON_RANDOM);

        // Render this page's slice of dungeons.
        uint32 start = page * DUNGEONS_PER_PAGE;
        uint32 end   = std::min(start + DUNGEONS_PER_PAGE, total);
        for (uint32 i = start; i < end; ++i)
        {
            DungeonInfo const* dg = dungeons[i];
            AddGossipItemFor(player, GOSSIP_ICON_INTERACT_1,
                Acore::StringFormat("{} (Lv {}-{})", dg->Name, dg->MinLevel, dg->MaxLevel),
                GOSSIP_SENDER_MAIN, GOSSIP_ACTION_DUNGEON_BASE + dg->MapId);
        }

        if (dungeons.empty())
            AddGossipItemFor(player, GOSSIP_ICON_CHAT,
                "|cFF808080No echo can be reached from here.|r", GOSSIP_SENDER_MAIN, GOSSIP_ACTION_CANCEL);

        // Page navigation, rendered conditionally.
        if (page > 0)
            AddGossipItemFor(player, GOSSIP_ICON_CHAT, "|cFF00FF00<< Previous Page|r",
                GOSSIP_SENDER_MAIN, GOSSIP_ACTION_DUNGEON_PREV_PAGE);
        if (end < total)
            AddGossipItemFor(player, GOSSIP_ICON_CHAT, "|cFF00FF00Next Page >>|r",
                GOSSIP_SENDER_MAIN, GOSSIP_ACTION_DUNGEON_NEXT_PAGE);

        AddGossipItemFor(player, GOSSIP_ICON_CHAT, "|cFFFF0000<< Back|r", GOSSIP_SENDER_MAIN, GOSSIP_ACTION_CANCEL);
        SendGossipMenuFor(player, NPC_TEXT_GREETING, creature->GetGUID());
    }

    void ShowConfirmMenu(Player* player, Creature* creature)
    {
        player->PlayerTalkClass->ClearMenus();

        PlayerDMSelection sel;
        { std::lock_guard<std::mutex> lk(sSelMutex);
          auto it = sSelections.find(player->GetGUID());
          if (it == sSelections.end()) { player->PlayerTalkClass->SendCloseGossip(); return; }
          sel = it->second; }

        DifficultyTier const* diff = sDMConfig->GetDifficulty(sel.DifficultyId);
        Theme const*          theme = sDMConfig->GetTheme(sel.ThemeId);

        std::string dgName = "Chosen by the timeways";
        if (sel.MapId > 0)
            if (DungeonInfo const* dg = sDMConfig->GetDungeon(sel.MapId))
                dgName = dg->Name;

        Group* g = player->GetGroup();
        uint32 ps = g ? g->GetMembersCount() : 1;

        ChatHandler chat(player->GetSession());
        chat.SendSysMessage("|cFFCD7F32========== The Echo ==========|r");
        chat.PSendSysMessage("  Difficulty: |cFF00FF00{}|r", diff ? diff->Name : "?");
        chat.PSendSysMessage("  Attunement: |cFF00FF00{}|r", sel.ScaleToParty ? "Your party's level" : "As it was");
        chat.PSendSysMessage("  Timeline:   |cFF00FF00{}|r", theme ? theme->Name : "?");
        chat.PSendSysMessage("  Dungeon:    |cFF00FF00{}|r", dgName);
        chat.PSendSysMessage("  Party Size: |cFFFFFFFF{}|r", ps);
        if (ps > 1)
            chat.SendSysMessage("|cFFFFFF00  Your whole party will be drawn into the echo.|r");
        chat.SendSysMessage("|cFFCD7F32==============================|r");

        AddGossipItemFor(player, GOSSIP_ICON_BATTLE, "|cFF00FF00Open the echo.|r",
            GOSSIP_SENDER_MAIN, GOSSIP_ACTION_CONFIRM);
        AddGossipItemFor(player, GOSSIP_ICON_CHAT, "|cFFFF0000<< Not yet|r",
            GOSSIP_SENDER_MAIN, GOSSIP_ACTION_CANCEL);
        SendGossipMenuFor(player, NPC_TEXT_GREETING, creature->GetGUID());
    }

    void ShowInfoMenu(Player* player, Creature* creature)
    {
        player->PlayerTalkClass->ClearMenus();
        ChatHandler chat(player->GetSession());
        chat.SendSysMessage("|cFFCD7F32========= Echoes of the Timeways =========|r");
        chat.SendSysMessage("|cFFFFFFFF1.|r Choose how strong the echo should be (difficulty).");
        chat.SendSysMessage("|cFFFFFFFF2.|r Attune it to your party, or leave it as it was.");
        chat.SendSysMessage("|cFFFFFFFF3.|r Choose a fractured timeline: who rules the halls there.");
        chat.SendSysMessage("|cFFFFFFFF4.|r Choose a dungeon, or let the timeways choose.");
        chat.SendSysMessage("|cFFFFFFFF5.|r Normal difficulty only: echoes cannot hold a heroic timeline.");
        chat.SendSysMessage("|cFFFFFFFF6.|r Defeat the echo's master to mend it.");
        chat.SendSysMessage("|cFFFFFFFF7.|r The Keepers of Time reward you in gold and gear.");
        chat.SendSysMessage("|cFFCD7F32==========================================|r");
        AddGossipItemFor(player, GOSSIP_ICON_CHAT, "<< Back", GOSSIP_SENDER_MAIN, GOSSIP_ACTION_CANCEL);
        SendGossipMenuFor(player, NPC_TEXT_GREETING, creature->GetGUID());
    }

    // ---- Chronicles hub (statistics & leaderboards) ----

    void ShowStatsAndBoardsMenu(Player* player, Creature* creature)
    {
        player->PlayerTalkClass->ClearMenus();
        AddGossipItemFor(player, GOSSIP_ICON_TABARD, "My echoes",
            GOSSIP_SENDER_MAIN, GOSSIP_ACTION_STATS_NORMAL);
        if (sDMConfig->IsRoguelikeEnabled())
            AddGossipItemFor(player, GOSSIP_ICON_TABARD, "|cFFCD7F32My descents into the Endless Timeways|r",
                GOSSIP_SENDER_MAIN, GOSSIP_ACTION_STATS_ROGUELIKE);
        AddGossipItemFor(player, GOSSIP_ICON_TABARD, "|cFFFFD700Chronicles of the fastest and deepest|r",
            GOSSIP_SENDER_MAIN, GOSSIP_ACTION_BOARD_MENU);
        AddGossipItemFor(player, GOSSIP_ICON_CHAT, "|cFFFF0000<< Back|r",
            GOSSIP_SENDER_MAIN, GOSSIP_ACTION_CANCEL);
        SendGossipMenuFor(player, NPC_TEXT_GREETING, creature->GetGUID());
    }

    void ShowNormalStats(Player* player, Creature* creature)
    {
        player->PlayerTalkClass->ClearMenus();
        PlayerStats st = sDungeonMasterMgr->GetPlayerStats(player->GetGUID());
        ChatHandler chat(player->GetSession());

        chat.SendSysMessage("|cFFFFD700═══════════ My Echoes ═══════════|r");
        chat.PSendSysMessage("  Echoes walked: |cFFFFFFFF{}|r  —  Mended: |cFF00FF00{}|r  —  Lost: |cFFFF0000{}|r",
            st.TotalRuns, st.CompletedRuns, st.FailedRuns);

        if (st.TotalRuns > 0)
            chat.PSendSysMessage("  Success Rate: |cFFFFD700{:.1f}%|r", st.CompletedRuns * 100.0f / st.TotalRuns);

        chat.SendSysMessage(" ");
        chat.PSendSysMessage("  Foes Slain:  |cFFFFFFFF{}|r  —  Masters Slain: |cFFFFFFFF{}|r",
            st.TotalMobsKilled, st.TotalBossesKilled);
        chat.PSendSysMessage("  Deaths: |cFFFF0000{}|r", st.TotalDeaths);

        if (st.TotalDeaths > 0 && st.TotalMobsKilled > 0)
            chat.PSendSysMessage("  Kill/Death Ratio: |cFFFFD700{:.1f}|r",
                static_cast<float>(st.TotalMobsKilled + st.TotalBossesKilled) / st.TotalDeaths);

        if (st.FastestClear > 0)
            chat.PSendSysMessage("  Fastest Echo: |cFF00FFFF{}|r", FormatTime(st.FastestClear));

        chat.SendSysMessage("|cFFFFD700══════════════════════════════════════════|r");

        AddGossipItemFor(player, GOSSIP_ICON_TABARD, "|cFFFFD700View the chronicles|r",
            GOSSIP_SENDER_MAIN, GOSSIP_ACTION_BOARD_MENU);
        AddGossipItemFor(player, GOSSIP_ICON_CHAT, "|cFFFF0000<< Back|r",
            GOSSIP_SENDER_MAIN, GOSSIP_ACTION_STATS_MENU);
        SendGossipMenuFor(player, NPC_TEXT_GREETING, creature->GetGUID());
    }

    void ShowRoguelikeStats(Player* player, Creature* creature)
    {
        player->PlayerTalkClass->ClearMenus();
        RoguelikePlayerStats st = sRoguelikeMgr->GetRoguelikePlayerStats(player->GetGUID());
        ChatHandler chat(player->GetSession());

        chat.SendSysMessage("|cFFCD7F32═══════════ My Descents ═══════════|r");
        chat.PSendSysMessage("  Descents: |cFFFFFFFF{}|r", st.TotalRuns);
        chat.PSendSysMessage("  Deepest: |cFFFFD700depth {}|r  —  Most Echoes in one descent: |cFFFFD700{}|r",
            st.HighestTier, st.MostFloorsCleared);
        chat.PSendSysMessage("  Echoes Mended: |cFFFFFFFF{}|r", st.TotalFloorsCleared);
        chat.SendSysMessage(" ");
        chat.PSendSysMessage("  Foes Slain: |cFFFFFFFF{}|r  —  Masters Slain: |cFFFFFFFF{}|r",
            st.TotalMobsKilled, st.TotalBossesKilled);
        chat.PSendSysMessage("  Deaths: |cFFFF0000{}|r", st.TotalDeaths);

        if (st.TotalDeaths > 0 && (st.TotalMobsKilled + st.TotalBossesKilled) > 0)
            chat.PSendSysMessage("  Kill/Death Ratio: |cFFFFD700{:.1f}|r",
                static_cast<float>(st.TotalMobsKilled + st.TotalBossesKilled) / st.TotalDeaths);

        if (st.TotalRuns > 0)
            chat.PSendSysMessage("  Echoes per Descent: |cFF00FFFF{:.1f}|r",
                static_cast<float>(st.TotalFloorsCleared) / st.TotalRuns);

        if (st.LongestRunTime > 0)
            chat.PSendSysMessage("  Longest Descent: |cFF00FFFF{}|r", FormatTime(st.LongestRunTime));

        chat.SendSysMessage("|cFFCD7F32══════════════════════════════════════════|r");

        AddGossipItemFor(player, GOSSIP_ICON_TABARD, "|cFFFFD700View the chronicles|r",
            GOSSIP_SENDER_MAIN, GOSSIP_ACTION_BOARD_MENU);
        AddGossipItemFor(player, GOSSIP_ICON_CHAT, "|cFFFF0000<< Back|r",
            GOSSIP_SENDER_MAIN, GOSSIP_ACTION_STATS_MENU);
        SendGossipMenuFor(player, NPC_TEXT_GREETING, creature->GetGUID());
    }

    // ---- Leaderboard hub ----

    void ShowBoardMenu(Player* player, Creature* creature)
    {
        player->PlayerTalkClass->ClearMenus();
        AddGossipItemFor(player, GOSSIP_ICON_TABARD,
            "|cFFFFD700Echoes — fastest mended|r",
            GOSSIP_SENDER_MAIN, GOSSIP_ACTION_BOARD_NORMAL);
        if (sDMConfig->IsRoguelikeEnabled())
        {
            AddGossipItemFor(player, GOSSIP_ICON_TABARD,
                "|cFFCD7F32Endless Timeways — deepest descents|r",
                GOSSIP_SENDER_MAIN, GOSSIP_ACTION_BOARD_RL_TIER);
            AddGossipItemFor(player, GOSSIP_ICON_TABARD,
                "|cFFCD7F32Endless Timeways — most echoes mended|r",
                GOSSIP_SENDER_MAIN, GOSSIP_ACTION_BOARD_RL_FLOORS);
        }
        AddGossipItemFor(player, GOSSIP_ICON_CHAT, "|cFFFF0000<< Back|r",
            GOSSIP_SENDER_MAIN, GOSSIP_ACTION_STATS_MENU);
        SendGossipMenuFor(player, NPC_TEXT_GREETING, creature->GetGUID());
    }

    void ShowNormalLeaderboard(Player* player, Creature* creature)
    {
        player->PlayerTalkClass->ClearMenus();

        auto entries = sDungeonMasterMgr->GetOverallLeaderboard(10);
        ChatHandler chat(player->GetSession());

        chat.SendSysMessage("|cFFFFD700═══════ Echoes — Fastest Mended ═══════|r");

        if (entries.empty())
            chat.SendSysMessage("  |cFF808080The chronicles are still empty.|r");
        else
        {
            uint32 rank = 0;
            uint32 myGuid = player->GetGUID().GetCounter();
            for (auto const& e : entries)
            {
                ++rank;
                DifficultyTier const* diff = sDMConfig->GetDifficulty(e.DifficultyId);
                DungeonInfo const* dg = sDMConfig->GetDungeon(e.MapId);
                bool isMe = (e.Guid == myGuid);

                chat.PSendSysMessage("  {}#{}|r |cFFFFFFFF{}|r — |cFF00FFFF{}|r — {} ({}) {}P{}{}",
                    isMe ? "|cFF00FF00" : "|cFFFFD700", rank, e.CharName, FormatTime(e.ClearTime),
                    dg ? dg->Name : "?", diff ? diff->Name : "?", e.PartySize,
                    e.Scaled ? " |cFF00FF00[Attuned]|r" : "",
                    isMe ? " |cFF00FF00<< YOU|r" : "");
            }
        }

        chat.SendSysMessage("|cFFFFD700════════════════════════════════════════════════|r");
        AddGossipItemFor(player, GOSSIP_ICON_CHAT, "|cFFFF0000<< Back|r",
            GOSSIP_SENDER_MAIN, GOSSIP_ACTION_BOARD_MENU);
        SendGossipMenuFor(player, NPC_TEXT_GREETING, creature->GetGUID());
    }

    void ShowRoguelikeLeaderboard(Player* player, Creature* creature, bool sortByFloors)
    {
        player->PlayerTalkClass->ClearMenus();

        auto entries = sRoguelikeMgr->GetRoguelikeLeaderboard(10, sortByFloors);
        ChatHandler chat(player->GetSession());

        chat.SendSysMessage(sortByFloors
            ? "|cFFCD7F32═══════ Endless Timeways — Most Echoes Mended ═══════|r"
            : "|cFFCD7F32═══════ Endless Timeways — Deepest Descents ═══════|r");

        if (entries.empty())
            chat.SendSysMessage("  |cFF808080No descent has been chronicled yet.|r");
        else
        {
            uint32 rank = 0;
            uint32 myGuid = player->GetGUID().GetCounter();
            for (auto const& e : entries)
            {
                ++rank;
                bool isMe = (e.Guid == myGuid);
                chat.PSendSysMessage(
                    "  {}#{}|r |cFFFFFFFF{}|r — depth |cFFFFD700{}|r — |cFF00FF00{}|r echo{} — "
                    "|cFF00FFFF{}|r — {} slain — {}P{}",
                    isMe ? "|cFF00FF00" : "|cFFFFD700", rank, e.CharName, e.TierReached,
                    e.DungeonsCleared, Plural(e.DungeonsCleared), FormatTime(e.RunDuration),
                    e.TotalKills, e.PartySize, isMe ? " |cFF00FF00<< YOU|r" : "");
            }
        }

        chat.SendSysMessage("|cFFCD7F32══════════════════════════════════════════════|r");
        AddGossipItemFor(player, GOSSIP_ICON_CHAT, "|cFFFF0000<< Back|r",
            GOSSIP_SENDER_MAIN, GOSSIP_ACTION_BOARD_MENU);
        SendGossipMenuFor(player, NPC_TEXT_GREETING, creature->GetGUID());
    }

    // ---- Endless Timeways menus ----

    void ShowRoguelikeDifficultyMenu(Player* player, Creature* creature)
    {
        player->PlayerTalkClass->ClearMenus();
        uint8 lvl = player->GetLevel();

        ChatHandler chat(player->GetSession());
        chat.SendSysMessage("|cFFCD7F32========== The Endless Timeways ==========|r");
        chat.SendSysMessage("|cFFFFFFFFOne echo after another, each deeper and less stable than the last.|r");
        chat.SendSysMessage("|cFFFFFFFFThe timeways resonate with you as you go: you grow stronger, and so do they.|r");
        chat.SendSysMessage("|cFFFF0000If your whole party falls, the descent ends.|r");
        chat.SendSysMessage("|cFFCD7F32==========================================|r");

        for (auto const& d : sDMConfig->GetDifficulties())
            AddGossipItemFor(player,
                d.IsValidForLevel(lvl) ? GOSSIP_ICON_BATTLE : GOSSIP_ICON_CHAT,
                DifficultyLabel(d, lvl, true), GOSSIP_SENDER_MAIN, GOSSIP_ACTION_DIFF_BASE + d.Id);

        AddGossipItemFor(player, GOSSIP_ICON_CHAT, "|cFFFF0000<< Back|r",
            GOSSIP_SENDER_MAIN, GOSSIP_ACTION_CANCEL);
        SendGossipMenuFor(player, NPC_TEXT_GREETING, creature->GetGUID());
    }

    void ShowRoguelikeScalingMenu(Player* player, Creature* creature)
    {
        player->PlayerTalkClass->ClearMenus();

        uint8 partyLevel = sDungeonMasterMgr->ComputeEffectiveLevel(player);

        AddGossipItemFor(player, GOSSIP_ICON_BATTLE,
            Acore::StringFormat("|cFF00FF00Attune the echoes to my party|r (Lv {})", partyLevel),
            GOSSIP_SENDER_MAIN, GOSSIP_ACTION_ROGUELIKE_SCALE_PARTY);
        AddGossipItemFor(player, GOSSIP_ICON_INTERACT_1,
            "|cFFFFD700Leave the echoes as they were|r — original level ranges",
            GOSSIP_SENDER_MAIN, GOSSIP_ACTION_ROGUELIKE_SCALE_TIER);
        AddGossipItemFor(player, GOSSIP_ICON_CHAT, "|cFFFF0000<< Back|r",
            GOSSIP_SENDER_MAIN, GOSSIP_ACTION_CANCEL);
        SendGossipMenuFor(player, NPC_TEXT_GREETING, creature->GetGUID());
    }

    void ShowRoguelikeThemeMenu(Player* player, Creature* creature)
    {
        player->PlayerTalkClass->ClearMenus();
        for (auto const& t : sDMConfig->GetThemes())
            AddGossipItemFor(player, GOSSIP_ICON_BATTLE, t.Name,
                GOSSIP_SENDER_MAIN, GOSSIP_ACTION_ROGUELIKE_THEME + t.Id);
        AddGossipItemFor(player, GOSSIP_ICON_CHAT, "|cFFFF0000<< Back|r",
            GOSSIP_SENDER_MAIN, GOSSIP_ACTION_CANCEL);
        SendGossipMenuFor(player, NPC_TEXT_GREETING, creature->GetGUID());
    }

    void StartRoguelike(Player* player, Creature* /*creature*/)
    {
        player->PlayerTalkClass->SendCloseGossip();
        ChatHandler chat(player->GetSession());

        PlayerDMSelection sel;
        { std::lock_guard<std::mutex> lk(sSelMutex);
          auto it = sSelections.find(player->GetGUID());
          if (it == sSelections.end())
          {
              chat.PSendSysMessage("{} The moment has passed. Speak to me again.", CHAT_PREFIX_ENDLESS);
              return;
          }
          sel = it->second;
          sSelections.erase(it); }

        DifficultyTier const* diff = sDMConfig->GetDifficulty(sel.DifficultyId);
        if (!diff || !diff->IsValidForLevel(player->GetLevel()))
        {
            chat.PSendSysMessage("{} You are not yet ready for echoes this strong.", CHAT_PREFIX_ENDLESS);
            return;
        }

        if (!sRoguelikeMgr->StartRun(player, sel.DifficultyId, sel.ThemeId, sel.ScaleToParty))
        {
            chat.PSendSysMessage("{} The timeways would not open.", CHAT_PREFIX_ENDLESS);
            return;
        }

        chat.PSendSysMessage("{} The descent begins. Mend each echo to go deeper.", CHAT_PREFIX_ENDLESS);
    }

    // ---- Launch ----
    void StartChallenge(Player* player, Creature* /*creature*/)
    {
        player->PlayerTalkClass->SendCloseGossip();
        ChatHandler chat(player->GetSession());

        PlayerDMSelection sel;
        { std::lock_guard<std::mutex> lk(sSelMutex);
          auto it = sSelections.find(player->GetGUID());
          if (it == sSelections.end())
          {
              chat.PSendSysMessage("{} The moment has passed. Speak to me again.", CHAT_PREFIX);
              return;
          }
          sel = it->second;
          sSelections.erase(it); }

        DifficultyTier const* diff = sDMConfig->GetDifficulty(sel.DifficultyId);
        if (!diff || !diff->IsValidForLevel(player->GetLevel()))
        {
            chat.PSendSysMessage("{} You are not yet ready for an echo this strong.", CHAT_PREFIX);
            return;
        }

        // Resolve "let the timeways choose"
        uint32 mapId = sel.MapId;
        if (mapId == 0)
        {
            auto dgs = sDMConfig->GetDungeonsForLevel(diff->MinLevel, diff->MaxLevel);
            if (dgs.empty())
            {
                chat.PSendSysMessage("{} No echo can be reached from here.", CHAT_PREFIX);
                return;
            }
            mapId = dgs[urand(0, static_cast<uint32>(dgs.size()) - 1)]->MapId;
        }

        Session* s = sDungeonMasterMgr->CreateSession(player, sel.DifficultyId, sel.ThemeId, mapId, sel.ScaleToParty);
        if (!s)
        {
            chat.PSendSysMessage("{} The echo would not hold. Try again later.", CHAT_PREFIX);
            return;
        }

        if (!sDungeonMasterMgr->StartDungeon(s))
        {
            chat.PSendSysMessage("{} The echo could not be opened.", CHAT_PREFIX);
            sDungeonMasterMgr->AbandonSession(s->SessionId);
            return;
        }

        if (!sDungeonMasterMgr->TeleportPartyIn(s))
        {
            chat.PSendSysMessage("{} The timeways would not carry you.", CHAT_PREFIX);
            sDungeonMasterMgr->AbandonSession(s->SessionId);
            return;
        }

        if (sDMConfig->ShouldAnnounceCompletion())
        {
            Theme const* theme = sDMConfig->GetTheme(sel.ThemeId);
            DungeonInfo const* dg = sDMConfig->GetDungeon(mapId);

            std::string const opened = Acore::StringFormat(
                "{} |cFFFFFFFF{}|r opened a |cFFFFD700{}|r echo: |cFF00FFFF{}|r.",
                CHAT_PREFIX, player->GetName(), diff->Name, theme ? theme->Name : "a shattered timeline");
            std::string const detail = Acore::StringFormat(
                "{} Dungeon: |cFF00FF00{}|r  Attunement: |cFF00FF00{}|r",
                CHAT_PREFIX, dg ? dg->Name : "chosen by the timeways",
                sel.ScaleToParty ? "your party's level" : "as it was");

            // Tell the whole party
            for (auto const& pd : s->Players)
                if (Player* p = ObjectAccessor::FindPlayer(pd.PlayerGuid))
                {
                    ChatHandler(p->GetSession()).SendSysMessage(opened);
                    ChatHandler(p->GetSession()).SendSysMessage(detail);
                }
        }
    }
};

void AddSC_npc_dungeon_master()
{
    new npc_dungeon_master();
}
