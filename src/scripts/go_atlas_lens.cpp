/*
 * mod-atlas — go_atlas_lens.cpp
 * The Timeways Lens in Dalaran and the gameobjects of a map run (PLAN 1.4):
 *   go_atlas_lens       lists the Echo Sigils in your bags; placing one opens
 *                       the run and a Rift in Time beside the Lens
 *   go_atlas_rift       carries you (and your bots) into the run
 *   go_atlas_rift_home  carries you back to the Lens
 *   go_atlas_cache      your share of a mended run, and new sigils
 * The rifts and the cache despawn once their run is gone.
 */

#include "ScriptMgr.h"
#include "ScriptedGossip.h"
#include "GameObject.h"
#include "GameObjectAI.h"
#include "Item.h"
#include "Player.h"
#include "Chat.h"
#include "StringFormat.h"
#include "AtlasMgr.h"
#include "AtlasConfig.h"
#include <algorithm>
#include <mutex>
#include <unordered_map>
#include <vector>

using namespace Atlas;

namespace
{
    constexpr uint32 NPC_TEXT_LENS        = 500001;
    constexpr uint32 NPC_TEXT_LENS_EMPTY  = 500002;
    constexpr uint32 NPC_TEXT_LENS_OPEN   = 500003;

    constexpr uint32 ACTION_CLOSE_RUN     = 1;
    constexpr uint32 ACTION_PLACE_BASE    = 100;   // + index into the sigils shown
    constexpr std::size_t MAX_SIGILS_SHOWN = 20;   // a gossip menu holds 32 options

    // The sigils last shown to each player, by option index.
    std::mutex sLensMutex;
    std::unordered_map<ObjectGuid, std::vector<ObjectGuid>> sLensChoices;

    // Despawns its gameobject once the run it belongs to is gone.
    struct AtlasRunObjectAI : public GameObjectAI
    {
        explicit AtlasRunObjectAI(GameObject* go) : GameObjectAI(go) { }

        void UpdateAI(uint32 diff) override
        {
            if (_checkTimer > diff)
            {
                _checkTimer -= diff;
                return;
            }
            _checkTimer = 2000;
            if (!sAtlasMgr->IsMapObjectLive(me))
                me->DespawnOrUnsummon();
        }

    private:
        uint32 _checkTimer = 2000;
    };
}

class go_atlas_lens : public GameObjectScript
{
public:
    go_atlas_lens() : GameObjectScript("go_atlas_lens") { }

    bool OnGossipHello(Player* player, GameObject* go) override
    {
        ClearGossipMenuFor(player);

        if (!sAtlasConfig->IsEnabled() || !sAtlasConfig->IsMapsEnabled())
        {
            SendGossipMenuFor(player, NPC_TEXT_LENS_EMPTY, go->GetGUID());
            return true;
        }

        std::string const run = sAtlasMgr->DescribeMapRun(player->GetGUID());
        if (!run.empty())
        {
            sAtlasMgr->EnsureRift(player, go);
            AddGossipItemFor(player, GOSSIP_ICON_CHAT, run, GOSSIP_SENDER_MAIN, 0);
            AddGossipItemFor(player, GOSSIP_ICON_BATTLE, "|cFF8B0000Close the echo|r", GOSSIP_SENDER_MAIN,
                ACTION_CLOSE_RUN, "Close this echo? Its sigil will not return.", 0, false);
            SendGossipMenuFor(player, NPC_TEXT_LENS_OPEN, go->GetGUID());
            return true;
        }

        struct Shown
        {
            ObjectGuid  Guid;
            MapItemData Data;
        };
        std::vector<Shown> sigils;
        for (Item* item : AtlasMgr::FindMapItems(player))
            sigils.push_back({ item->GetGUID(), sAtlasMgr->GetMapItemData(item) });

        // Highest tier first, then the rarest.
        std::stable_sort(sigils.begin(), sigils.end(), [](Shown const& a, Shown const& b)
        {
            return a.Data.Tier != b.Data.Tier ? a.Data.Tier > b.Data.Tier : a.Data.Rarity > b.Data.Rarity;
        });
        if (sigils.size() > MAX_SIGILS_SHOWN)
            sigils.resize(MAX_SIGILS_SHOWN);

        if (sigils.empty())
        {
            SendGossipMenuFor(player, NPC_TEXT_LENS_EMPTY, go->GetGUID());
            return true;
        }

        std::vector<ObjectGuid> choices;
        for (std::size_t i = 0; i < sigils.size(); ++i)
        {
            choices.push_back(sigils[i].Guid);
            AddGossipItemFor(player, GOSSIP_ICON_INTERACT_1, sAtlasMgr->DescribeMapItem(sigils[i].Data, false, true),
                GOSSIP_SENDER_MAIN, ACTION_PLACE_BASE + static_cast<uint32>(i),
                "Place this sigil in the Lens? It will be spent.", 0, false);
        }
        {
            std::lock_guard<std::mutex> lock(sLensMutex);
            sLensChoices[player->GetGUID()] = std::move(choices);
        }

        SendGossipMenuFor(player, NPC_TEXT_LENS, go->GetGUID());
        return true;
    }

    bool OnGossipSelect(Player* player, GameObject* go, uint32 /*sender*/, uint32 action) override
    {
        CloseGossipMenuFor(player);

        if (action == ACTION_CLOSE_RUN)
        {
            sAtlasMgr->CloseMapRun(player);
            return true;
        }
        if (action < ACTION_PLACE_BASE)
            return true;

        ObjectGuid itemGuid;
        {
            std::lock_guard<std::mutex> lock(sLensMutex);
            auto it = sLensChoices.find(player->GetGUID());
            std::size_t const index = action - ACTION_PLACE_BASE;
            if (it == sLensChoices.end() || index >= it->second.size())
                return true;
            itemGuid = it->second[index];
            sLensChoices.erase(it);
        }

        std::string error;
        if (!sAtlasMgr->OpenMapRun(player, go, itemGuid, error) && !error.empty())
            ChatHandler(player->GetSession()).PSendSysMessage("{} {}", CHAT_PREFIX, error);
        return true;
    }
};

class go_atlas_rift : public GameObjectScript
{
public:
    go_atlas_rift() : GameObjectScript("go_atlas_rift") { }

    bool OnGossipHello(Player* player, GameObject* go) override
    {
        sAtlasMgr->UseEntryRift(player, go);
        return true;
    }

    GameObjectAI* GetAI(GameObject* go) const override { return new AtlasRunObjectAI(go); }
};

class go_atlas_rift_home : public GameObjectScript
{
public:
    go_atlas_rift_home() : GameObjectScript("go_atlas_rift_home") { }

    bool OnGossipHello(Player* player, GameObject* /*go*/) override
    {
        sAtlasMgr->UseExitRift(player);
        return true;
    }

    GameObjectAI* GetAI(GameObject* go) const override { return new AtlasRunObjectAI(go); }
};

class go_atlas_cache : public GameObjectScript
{
public:
    go_atlas_cache() : GameObjectScript("go_atlas_cache") { }

    bool OnGossipHello(Player* player, GameObject* go) override
    {
        sAtlasMgr->UseCache(player, go);
        return true;
    }

    GameObjectAI* GetAI(GameObject* go) const override { return new AtlasRunObjectAI(go); }
};

void AddSC_go_atlas_lens()
{
    new go_atlas_lens();
    new go_atlas_rift();
    new go_atlas_rift_home();
    new go_atlas_cache();
}
