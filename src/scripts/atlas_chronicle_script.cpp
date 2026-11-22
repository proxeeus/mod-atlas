/*
 * mod-atlas — the Chronicle window's server side.
 *
 * Protocol (addon prefix "ATLAS", addon whispers to its own character;
 * every request carries a short request token echoed back):
 *   client  H:<req>             hello  -> H:<req>:<protocol>:<endless 0/1>,
 *                                          DG:<req>:mapId,expansion,name;...
 *                                          DF:<req>:id,name;...   HZ:<req>
 *   client  E:<req>:<mapId>     fastest mendings (0 = every echo)
 *           -> E:<req>:rank,name,seconds,mapId,diffId,party,attuned,fallen,mine;...  EZ:<req>:<rows>
 *   client  D:<req>:<T|F>       Endless descents, deepest (T) or most echoes (F)
 *           -> D:<req>:rank,name,depth,echoes,slain,seconds,party,mine;...  DZ:<req>:<rows>
 *   client  R:<req>             the player's own record
 *           -> RN:<req>:walked,mended,lost,slain,masters,fallen,fastest
 *              RE:<req>:descents,deepest,mostEchoes,echoes,slain,masters,fallen,longest
 *              RZ:<req>:<echo rank>:<descent rank>   (0 = not chronicled)
 *   client  M:<req>:<bag>:<slot> an Echo Sigil in the bags (client bag ids:
 *                               0 backpack, 1-4 bags, -1 bank, 5-11 bank bags)
 *           -> MH:<req>:bag,slot,entry,tier,rarity,dungeon,theme   (only for a sigil)
 *              MA:<req>:affix;affix;...   MZ:<req>
 *   server  O                   open the window (sent by the Chronicler)
 *           OFF                 the module is disabled
 * Text fields escape , ; : | % as %XX.
 */

#include "AtlasChronicle.h"
#include "AtlasAffixes.h"
#include "AtlasConfig.h"
#include "AtlasMgr.h"
#include "Chat.h"
#include "DBCStores.h"
#include "Item.h"
#include "Player.h"
#include "RoguelikeMgr.h"
#include "ScriptMgr.h"
#include "StringFormat.h"
#include "WorldPacket.h"
#include "WorldSession.h"
#include <charconv>
#include <limits>
#include <mutex>
#include <string_view>
#include <unordered_set>
#include <vector>

using namespace Atlas;

namespace
{
    constexpr char const* PREFIX           = "ATLAS";
    constexpr uint32      PROTOCOL_VERSION = 1;
    constexpr uint32      BOARD_LIMIT      = 50;
    // A whisper carries 255 bytes: the prefix, a tab, then the payload.
    constexpr std::size_t MAX_PAYLOAD      = 255 - 5 - 1;

    std::mutex                     sAddonMutex;
    std::unordered_set<ObjectGuid> sAddonReady;

    void Send(Player* player, std::string const& payload)
    {
        if (!player || !player->GetSession())
            return;

        std::string const full = std::string(PREFIX) + "\t" + payload;
        WorldPacket data;
        ChatHandler::BuildChatPacket(data, CHAT_MSG_WHISPER, LANG_ADDON, player, player, full);
        player->GetSession()->SendPacket(&data);
    }

    // Packs rows into as few messages as fit, never splitting a row.
    void SendRows(Player* player, std::string const& header, std::vector<std::string> const& rows)
    {
        std::size_t const budget = MAX_PAYLOAD - header.size() - 1;
        std::string line;
        for (std::string const& row : rows)
        {
            if (row.size() > budget)
                continue;   // a cut row would corrupt the list
            if (!line.empty() && line.size() + 1 + row.size() > budget)
            {
                Send(player, header + ":" + line);
                line.clear();
            }
            if (!line.empty())
                line += ';';
            line += row;
        }
        if (!line.empty())
            Send(player, header + ":" + line);
    }

    std::string Escape(std::string_view text)
    {
        static char const* const HEX = "0123456789ABCDEF";
        std::string out;
        out.reserve(text.size());
        for (char c : text)
        {
            if (c == ',' || c == ';' || c == ':' || c == '|' || c == '%')
            {
                out += '%';
                out += HEX[(uint8(c) >> 4) & 0xF];
                out += HEX[uint8(c) & 0xF];
            }
            else
                out += c;
        }
        return out;
    }

    std::vector<std::string_view> Split(std::string_view text, char sep)
    {
        std::vector<std::string_view> parts;
        std::size_t start = 0;
        while (start <= text.size())
        {
            std::size_t const pos = text.find(sep, start);
            if (pos == std::string_view::npos)
            {
                parts.push_back(text.substr(start));
                break;
            }
            parts.push_back(text.substr(start, pos - start));
            start = pos + 1;
        }
        return parts;
    }

    bool ParseUInt(std::string_view text, uint32& out)
    {
        if (text.empty())
            return false;
        auto const result = std::from_chars(text.data(), text.data() + text.size(), out, 10);
        return result.ec == std::errc{} && result.ptr == text.data() + text.size();
    }

    void HandleHello(Player* player, std::string const& req)
    {
        {
            std::lock_guard<std::mutex> lock(sAddonMutex);
            sAddonReady.insert(player->GetGUID());
        }

        Send(player, Acore::StringFormat("H:{}:{}:{}", req, PROTOCOL_VERSION,
            sAtlasConfig->IsRoguelikeEnabled() ? 1 : 0));

        std::vector<std::string> dungeons;
        for (DungeonInfo const* dg : sAtlasConfig->GetDungeonsForLevel(1, 83))
        {
            MapEntry const* map = sMapStore.LookupEntry(dg->MapId);
            dungeons.push_back(Acore::StringFormat("{},{},{}", dg->MapId, map ? map->Expansion() : 0,
                Escape(dg->Name)));
        }
        SendRows(player, "DG:" + req, dungeons);

        std::vector<std::string> tiers;
        for (DifficultyTier const& d : sAtlasConfig->GetDifficulties())
            tiers.push_back(Acore::StringFormat("{},{}", d.Id, Escape(d.Name)));
        SendRows(player, "DF:" + req, tiers);

        Send(player, "HZ:" + req);
    }

    void HandleEchoes(Player* player, std::string const& req, uint32 mapId)
    {
        uint32 const me = player->GetGUID().GetCounter();
        std::vector<std::string> rows;
        uint32 rank = 0;
        for (LeaderboardEntry const& e : sAtlasMgr->GetOverallLeaderboard(std::numeric_limits<uint32>::max()))
        {
            if (mapId && e.MapId != mapId)
                continue;
            if (++rank > BOARD_LIMIT)
                break;
            rows.push_back(Acore::StringFormat("{},{},{},{},{},{},{},{},{}", rank, Escape(e.CharName),
                e.ClearTime, e.MapId, e.DifficultyId, e.PartySize, e.Scaled ? 1 : 0, e.Deaths,
                e.Guid == me ? 1 : 0));
        }
        SendRows(player, "E:" + req, rows);
        Send(player, Acore::StringFormat("EZ:{}:{}", req, rows.size()));
    }

    void HandleDescents(Player* player, std::string const& req, bool byEchoes)
    {
        uint32 const me = player->GetGUID().GetCounter();
        std::vector<std::string> rows;
        if (sAtlasConfig->IsRoguelikeEnabled())
        {
            uint32 rank = 0;
            for (RoguelikeLeaderboardEntry const& e : sRoguelikeMgr->GetRoguelikeLeaderboard(BOARD_LIMIT, byEchoes))
                rows.push_back(Acore::StringFormat("{},{},{},{},{},{},{},{}", ++rank, Escape(e.CharName),
                    e.TierReached, e.DungeonsCleared, e.TotalKills, e.RunDuration, e.PartySize,
                    e.Guid == me ? 1 : 0));
        }
        SendRows(player, "D:" + req, rows);
        Send(player, Acore::StringFormat("DZ:{}:{}", req, rows.size()));
    }

    void HandleRecord(Player* player, std::string const& req)
    {
        PlayerStats const n = sAtlasMgr->GetPlayerStats(player->GetGUID());
        Send(player, Acore::StringFormat("RN:{}:{},{},{},{},{},{},{}", req, n.TotalRuns, n.CompletedRuns,
            n.FailedRuns, n.TotalMobsKilled, n.TotalBossesKilled, n.TotalDeaths, n.FastestClear));

        RoguelikePlayerStats const r = sRoguelikeMgr->GetRoguelikePlayerStats(player->GetGUID());
        Send(player, Acore::StringFormat("RE:{}:{},{},{},{},{},{},{},{}", req, r.TotalRuns, r.HighestTier,
            r.MostFloorsCleared, r.TotalFloorsCleared, r.TotalMobsKilled, r.TotalBossesKilled, r.TotalDeaths,
            r.LongestRunTime));

        // Where the player's best run stands among all chronicled ones.
        uint32 const me = player->GetGUID().GetCounter();
        uint32 echoRank = 0;
        uint32 rank = 0;
        for (LeaderboardEntry const& e : sAtlasMgr->GetOverallLeaderboard(std::numeric_limits<uint32>::max()))
        {
            ++rank;
            if (e.Guid == me)
            {
                echoRank = rank;
                break;
            }
        }

        uint32 descentRank = 0;
        rank = 0;
        for (RoguelikeLeaderboardEntry const& e :
            sRoguelikeMgr->GetRoguelikeLeaderboard(std::numeric_limits<uint32>::max(), false))
        {
            ++rank;
            if (e.Guid == me)
            {
                descentRank = rank;
                break;
            }
        }
        Send(player, Acore::StringFormat("RZ:{}:{}:{}", req, echoRank, descentRank));
    }

    bool ParseInt(std::string_view text, int32& out)
    {
        if (text.empty())
            return false;
        auto const result = std::from_chars(text.data(), text.data() + text.size(), out, 10);
        return result.ec == std::errc{} && result.ptr == text.data() + text.size();
    }

    // Client bag and slot (1-based) to the server's position, false if out of range.
    bool ToServerPosition(int32 bag, int32 slot, uint8& outBag, uint8& outSlot)
    {
        if (slot < 1 || slot > 36)
            return false;
        if (bag == 0 && slot <= INVENTORY_SLOT_ITEM_END - INVENTORY_SLOT_ITEM_START)
        {
            outBag = INVENTORY_SLOT_BAG_0;
            outSlot = static_cast<uint8>(INVENTORY_SLOT_ITEM_START + slot - 1);
            return true;
        }
        if (bag == -1 && slot <= BANK_SLOT_ITEM_END - BANK_SLOT_ITEM_START)
        {
            outBag = INVENTORY_SLOT_BAG_0;
            outSlot = static_cast<uint8>(BANK_SLOT_ITEM_START + slot - 1);
            return true;
        }
        if (bag >= 1 && bag <= 4)
        {
            outBag = static_cast<uint8>(INVENTORY_SLOT_BAG_START + bag - 1);
            outSlot = static_cast<uint8>(slot - 1);
            return true;
        }
        if (bag >= 5 && bag <= 11)
        {
            outBag = static_cast<uint8>(BANK_SLOT_BAG_START + bag - 5);
            outSlot = static_cast<uint8>(slot - 1);
            return true;
        }
        return false;
    }

    void HandleSigil(Player* player, std::string const& req, int32 bag, int32 slot)
    {
        uint8 serverBag = 0;
        uint8 serverSlot = 0;
        Item* item = ToServerPosition(bag, slot, serverBag, serverSlot)
            ? player->GetItemByPos(serverBag, serverSlot) : nullptr;
        if (item && IsMapItemEntry(item->GetEntry()))
        {
            MapItemData const data = sAtlasMgr->GetMapItemData(item);
            DungeonInfo const* dg = sAtlasConfig->GetDungeon(data.SourceMap);
            Theme const* theme = sAtlasConfig->GetTheme(data.ThemeId);
            Send(player, Acore::StringFormat("MH:{}:{},{},{},{},{},{},{}", req, bag, slot, item->GetEntry(),
                data.Tier, static_cast<uint32>(data.Rarity), Escape(dg ? dg->Name : "an unknown place"),
                Escape(theme ? theme->Name : "")));

            std::vector<std::string> affixes;
            for (ActiveAffix const& a : data.Affixes)
                affixes.push_back(Escape(sAtlasAffixes->Describe({ a }, true)));
            SendRows(player, "MA:" + req, affixes);
        }
        Send(player, "MZ:" + req);
    }

    void Dispatch(Player* player, std::string_view message)
    {
        std::vector<std::string_view> const args = Split(message, ':');
        if (args.size() < 2 || args[1].empty())
            return;

        std::string const req(args[1].substr(0, 8));
        std::string_view const command = args[0];

        if (command == "H")
            HandleHello(player, req);
        else if (command == "E")
        {
            uint32 mapId = 0;
            if (args.size() > 2 && !ParseUInt(args[2], mapId))
                return;
            HandleEchoes(player, req, mapId);
        }
        else if (command == "D")
            HandleDescents(player, req, args.size() > 2 && args[2] == "F");
        else if (command == "R")
            HandleRecord(player, req);
        else if (command == "M" && args.size() > 3)
        {
            int32 bag = 0;
            int32 slot = 0;
            if (ParseInt(args[2], bag) && ParseInt(args[3], slot))
                HandleSigil(player, req, bag, slot);
        }
    }
}

namespace Atlas::Chronicle
{
    bool HasAddon(ObjectGuid guid)
    {
        std::lock_guard<std::mutex> lock(sAddonMutex);
        return sAddonReady.count(guid) != 0;
    }

    void Open(Player* player)
    {
        Send(player, "O");
    }
}

class atlas_chronicle_script : public PlayerScript
{
public:
    atlas_chronicle_script() : PlayerScript("atlas_chronicle_script",
        {
            PLAYERHOOK_CAN_PLAYER_USE_PRIVATE_CHAT,
            PLAYERHOOK_ON_LOGOUT,
        }) { }

    // Only the private-chat overload is overridden; keep the others visible.
    using PlayerScript::OnPlayerCanUseChat;

    // The addon whispers its own character; swallow those so they never show as chat.
    bool OnPlayerCanUseChat(Player* player, uint32 /*type*/, uint32 lang, std::string& msg, Player* receiver) override
    {
        if (lang != LANG_ADDON || !receiver || receiver != player)
            return true;

        std::string const prefixTab = std::string(PREFIX) + "\t";
        if (msg.compare(0, prefixTab.size(), prefixTab) != 0)
            return true;

        if (!sAtlasConfig->IsEnabled())
        {
            Send(player, "OFF");
            return false;
        }

        Dispatch(player, std::string_view(msg).substr(prefixTab.size()));
        return false;
    }

    void OnPlayerLogout(Player* player) override
    {
        std::lock_guard<std::mutex> lock(sAddonMutex);
        sAddonReady.erase(player->GetGUID());
    }
};

void AddSC_atlas_chronicle_script()
{
    new atlas_chronicle_script();
}
