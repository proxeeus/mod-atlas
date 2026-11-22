/*
 * mod-dungeon-master — dm_command_script.cpp
 * GM commands: .dm reload, .dm status, .dm list, .dm end, .dm clearcooldown
 */

#include "ScriptMgr.h"
#include "Chat.h"
#include "ChatCommand.h"
#include "Player.h"
#include "Group.h"
#include "DungeonMasterMgr.h"
#include "DMConfig.h"

using namespace Acore::ChatCommands;
using namespace DungeonMaster;

class dm_command_script : public CommandScript
{
public:
    dm_command_script() : CommandScript("dm_command_script") {}

    ChatCommandTable GetCommands() const override
    {
        static ChatCommandTable dmTable =
        {
            { "reload",        HandleReload,        SEC_ADMINISTRATOR,  Console::Yes },
            { "status",        HandleStatus,        SEC_GAMEMASTER,     Console::Yes },
            { "list",          HandleList,           SEC_GAMEMASTER,     Console::Yes },
            { "end",           HandleEnd,            SEC_ADMINISTRATOR,  Console::No  },
            { "clearcooldown", HandleClearCD,        SEC_GAMEMASTER,     Console::No  },
        };
        static ChatCommandTable root = { { "dm", dmTable } };
        return root;
    }

    static bool HandleReload(ChatHandler* h)
    {
        sDMConfig->LoadConfig(true);
        // Dungeon list or boss count may have changed: rebuild the cached
        // entrances / spawn points too (guarded by their own mutex).
        sDungeonMasterMgr->LoadDungeonGeometry();
        h->SendSysMessage("DungeonMaster: Configuration and dungeon geometry reloaded.");
        return true;
    }

    static bool HandleStatus(ChatHandler* h)
    {
        h->SendSysMessage("=== Dungeon Master (Keepers of Time) Status ===");
        h->PSendSysMessage("Enabled: {}", sDMConfig->IsEnabled() ? "Yes" : "No");
        h->PSendSysMessage("Active: {} / {}",
            sDungeonMasterMgr->GetActiveSessionCount(), sDMConfig->GetMaxConcurrentRuns());
        h->PSendSysMessage("Level Band: +/-{}", sDMConfig->GetLevelBand());
        h->PSendSysMessage("Difficulties: {}  Themes: {}  Dungeons: {}",
            sDMConfig->GetDifficulties().size(), sDMConfig->GetThemes().size(), sDMConfig->GetDungeons().size());
        return true;
    }

    static bool HandleList(ChatHandler* h)
    {
        h->PSendSysMessage("Active DM sessions: {}", sDungeonMasterMgr->GetActiveSessionCount());
        return true;
    }

    static bool HandleEnd(ChatHandler* h, Optional<uint32> sessionId)
    {
        if (sessionId)
        {
            if (!sDungeonMasterMgr->GetSession(*sessionId))
            {
                h->PSendSysMessage("Session {} not found.", *sessionId);
                return false;
            }

            sDungeonMasterMgr->EndSession(*sessionId, false);
            h->PSendSysMessage("Session {} ended.", *sessionId);
            return true;
        }

        // Try the invoker's own session first
        Player* invoker = h->GetSession() ? h->GetSession()->GetPlayer() : nullptr;
        Session* s = invoker ? sDungeonMasterMgr->GetSessionByPlayer(invoker->GetGUID()) : nullptr;

        // Fall back to selected player's session
        if (!s)
        {
            Player* t = h->getSelectedPlayer();
            s = t ? sDungeonMasterMgr->GetSessionByPlayer(t->GetGUID()) : nullptr;
        }

        if (!s)
        {
            h->SendSysMessage("Not in a DM session. Select a player or provide session ID.");
            return false;
        }

        uint32 id = s->SessionId;
        sDungeonMasterMgr->EndSession(id, false);
        h->PSendSysMessage("Session {} ended (all players teleported out).", id);
        return true;
    }

    static bool HandleClearCD(ChatHandler* h)
    {
        Player* invoker = h->GetSession() ? h->GetSession()->GetPlayer() : nullptr;
        if (!invoker)
        {
            h->SendSysMessage("In-game only.");
            return false;
        }

        // If invoker is in a group, clear cooldown for ALL group members
        if (Group* g = invoker->GetGroup())
        {
            uint32 cleared = 0;
            for (GroupReference* ref = g->GetFirstMember(); ref; ref = ref->next())
            {
                if (Player* member = ref->GetSource())
                {
                    sDungeonMasterMgr->ClearCooldown(member->GetGUID());
                    ++cleared;
                }
            }
            h->PSendSysMessage("Cooldown cleared for {} group member(s).", cleared);
            return true;
        }

        // Solo — clear for self or selected player
        Player* t = h->getSelectedPlayer();
        if (!t)
            t = invoker;
        sDungeonMasterMgr->ClearCooldown(t->GetGUID());
        h->PSendSysMessage("Cooldown cleared for {}.", t->GetName());
        return true;
    }
};

void AddSC_dm_command_script()
{
    new dm_command_script();
}
