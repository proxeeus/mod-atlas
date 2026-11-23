/*
 * mod-atlas — AtlasMaps.cpp
 * AtlasMgr: Echo Sigils, the Timeways Lens, its rifts and the Keepers' cache
 * (PLAN 1.4, DESIGN §7 and §11).
 *
 * A sigil opens a map run: the Lens takes it and opens a Rift in Time beside
 * it. Each member's first crossing is free (bots cross with the player who
 * uses the rift); every later crossing, and every wipe, spends one of the
 * run's rifts. With none left and nobody inside, the run is lost. Its master
 * slain, a Cache of the Keepers and a rift home appear where it fell; each
 * player opens the cache for their share and new sigils, bots are paid at once.
 */

#include "AtlasMgr.h"
#include "AtlasAffixes.h"
#include "AtlasConfig.h"
#include "Bag.h"
#include "Chat.h"
#include "DatabaseEnv.h"
#include "Field.h"
#include "GameObject.h"
#include "GameTime.h"
#include "Item.h"
#include "Log.h"
#include "Map.h"
#include "ObjectAccessor.h"
#include "Player.h"
#include "QueryResult.h"
#include "StringFormat.h"
#include "WorldSession.h"
#include <algorithm>
#include <cmath>

namespace Atlas
{

namespace
{
    // Where a rift home leads when a member's return point is unknown: the Lens.
    constexpr uint32 LENS_MAP = 571;
    Position const   LENS_POS = { 5810.2f, 803.6f, 662.2f, 0.35f };

    // Spawns inside a run outlive it at most this long; their AI despawns
    // them as soon as the run is gone.
    constexpr uint32 RUN_OBJECT_LIFETIME = 4 * 3600;

    void Tell(Player* player, std::string const& text)
    {
        if (player && player->GetSession())
            ChatHandler(player->GetSession()).SendSysMessage(text);
    }

    // "|cFFFF8800Brutal|r" -> "Brutal".
    std::string StripColours(std::string const& text)
    {
        std::string out;
        out.reserve(text.size());
        for (std::size_t i = 0; i < text.size(); ++i)
        {
            if (text[i] == '|' && i + 1 < text.size() && (text[i + 1] == 'c' || text[i + 1] == 'r'))
            {
                i += text[i + 1] == 'c' ? 9 : 1;
                continue;
            }
            out += text[i];
        }
        return out;
    }

    Position Offset(Position const& pos, float angle, float distance)
    {
        return { pos.GetPositionX() + std::cos(angle) * distance, pos.GetPositionY() + std::sin(angle) * distance,
            pos.GetPositionZ(), pos.GetOrientation() };
    }

    // The rift opens beside the Lens, away from the Chronicler; runs open at
    // the same time stand a little apart.
    GameObject* SummonRift(GameObject* lens, uint32 sessionId)
    {
        float const angle = lens->GetOrientation() + float(M_PI) / 2.0f;
        Position const pos = Offset(lens->GetPosition(), angle, 3.0f + 2.5f * float(sessionId % 3));
        float const o = lens->GetOrientation();
        return lens->SummonGameObject(GO_ATLAS_RIFT, pos.GetPositionX(), pos.GetPositionY(), pos.GetPositionZ(), o,
            0.0f, 0.0f, std::sin(o / 2.0f), std::cos(o / 2.0f), RUN_OBJECT_LIFETIME);
    }
}

bool AtlasMgr::IsRealPlayer(Player const* player)
{
    WorldSession* session = player ? player->GetSession() : nullptr;
    return session && session->GetRemoteAddress() != "headless";
}

// ---------------------------------------------------------------------------
// Sigil data
// ---------------------------------------------------------------------------

void AtlasMgr::LoadMapItems()
{
    // Sigils sold, destroyed or lost to a crash before their owner was saved.
    CharacterDatabase.DirectExecute("DELETE m FROM atlas_map_item m LEFT JOIN item_instance i "
        "ON i.guid = m.item_guid WHERE i.guid IS NULL");

    std::unordered_map<uint32, MapItemData> items;
    if (QueryResult result = CharacterDatabase.Query("SELECT item_guid, owner_guid, tier, rarity, source_map, "
        "theme_id, seed, affixes FROM atlas_map_item"))
    {
        do
        {
            Field* f = result->Fetch();
            MapItemData d;
            d.ItemGuid  = f[0].Get<uint32>();
            d.OwnerGuid = f[1].Get<uint32>();
            d.Tier      = std::clamp<uint8>(f[2].Get<uint8>(), 1, MAP_MAX_TIER);
            d.Rarity    = static_cast<MapRarity>(std::min<uint8>(f[3].Get<uint8>(), uint8(MapRarity::Rare)));
            d.SourceMap = f[4].Get<uint32>();
            d.ThemeId   = f[5].Get<uint32>();
            d.Seed      = f[6].Get<uint64>();
            d.Affixes   = ParseMapAffixes(f[7].Get<std::string>());
            items[d.ItemGuid] = std::move(d);
        } while (result->NextRow());
    }

    std::unordered_set<uint32> grants;
    if (QueryResult result = CharacterDatabase.Query("SELECT guid FROM atlas_map_grant"))
    {
        do
            grants.insert(result->Fetch()[0].Get<uint32>());
        while (result->NextRow());
    }

    std::unordered_map<uint64, uint32> progress;
    if (QueryResult result = CharacterDatabase.Query("SELECT account, source_map, tiers FROM atlas_account_progress"))
    {
        do
        {
            Field* f = result->Fetch();
            progress[(uint64(f[0].Get<uint32>()) << 32) | f[1].Get<uint32>()] = f[2].Get<uint32>();
        } while (result->NextRow());
    }

    std::lock_guard<std::mutex> lock(_mapItemsMutex);
    _mapItems = std::move(items);
    _mapGrants = std::move(grants);
    _accountProgress = std::move(progress);
    LOG_INFO("module", "Atlas: {} Echo Sigils, {} starter grants, {} dungeon records loaded.", _mapItems.size(),
        _mapGrants.size(), _accountProgress.size());
}

void AtlasMgr::PersistMapItem(MapItemData const& data)
{
    {
        std::lock_guard<std::mutex> lock(_mapItemsMutex);
        _mapItems[data.ItemGuid] = data;
    }
    CharacterDatabase.Execute("REPLACE INTO atlas_map_item (item_guid, owner_guid, tier, rarity, source_map, "
        "theme_id, seed, affixes, created) VALUES ({}, {}, {}, {}, {}, {}, {}, '{}', UNIX_TIMESTAMP())",
        data.ItemGuid, data.OwnerGuid, data.Tier, static_cast<uint32>(data.Rarity), data.SourceMap, data.ThemeId,
        data.Seed, SerializeMapAffixes(data.Affixes));
}

void AtlasMgr::ForgetMapItem(uint32 itemGuid)
{
    {
        std::lock_guard<std::mutex> lock(_mapItemsMutex);
        _mapItems.erase(itemGuid);
    }
    CharacterDatabase.Execute("DELETE FROM atlas_map_item WHERE item_guid = {}", itemGuid);
}

MapItemData AtlasMgr::GetMapItemData(Item const* item) const
{
    MapItemData data;
    if (!item)
        return data;

    uint32 const guid = item->GetGUID().GetCounter();
    {
        std::lock_guard<std::mutex> lock(_mapItemsMutex);
        auto it = _mapItems.find(guid);
        if (it != _mapItems.end())
            return it->second;
    }

    // No row: a plain echo of the sigil's tier, the same every time.
    DecodeMapItemEntry(item->GetEntry(), data.Tier, data.Rarity);
    RunRng rng(uint64(guid) * 0x9E3779B97F4A7C15ULL + 1);
    data.ItemGuid  = guid;
    data.SourceMap = PickMapSource(data.Tier, rng);
    auto const& themes = sAtlasConfig->GetThemes();
    data.ThemeId   = themes.empty() ? 0 : themes.front().Id;
    data.Seed      = rng.Next() | 1;
    return data;
}

std::vector<Item*> AtlasMgr::FindMapItems(Player* player)
{
    std::vector<Item*> items;
    if (!player)
        return items;

    for (uint8 slot = INVENTORY_SLOT_ITEM_START; slot < INVENTORY_SLOT_ITEM_END; ++slot)
        if (Item* item = player->GetItemByPos(INVENTORY_SLOT_BAG_0, slot))
            if (IsMapItemEntry(item->GetEntry()))
                items.push_back(item);

    for (uint8 bagSlot = INVENTORY_SLOT_BAG_START; bagSlot < INVENTORY_SLOT_BAG_END; ++bagSlot)
        if (Bag* bag = player->GetBagByPos(bagSlot))
            for (uint32 slot = 0; slot < bag->GetBagSize(); ++slot)
                if (Item* item = bag->GetItemByPos(static_cast<uint8>(slot)))
                    if (IsMapItemEntry(item->GetEntry()))
                        items.push_back(item);
    return items;
}

uint32 AtlasMgr::PickMapSource(uint8 tier, RunRng& rng) const
{
    // Dungeons by band (AtlasMapItem.h): Classic, The Burning Crusade, Wrath.
    std::array<std::vector<uint32>, 3> bands;
    for (DungeonInfo const* dg : sAtlasConfig->GetDungeonsForLevel(1, 83))
    {
        if (!dg->IsAvailable || sAtlasConfig->IsMapSourceExcluded(dg->MapId))
            continue;
        std::shared_ptr<Layout const> layout = GetLayout(dg->MapId);
        if (!layout || layout->Arenas.empty())
            continue;
        uint8 const band = dg->MaxLevel <= 62 ? 0 : dg->MaxLevel <= 72 ? 1 : 2;
        bands[band].push_back(dg->MapId);
    }

    std::vector<uint32> candidates = bands[MapSourceBand(tier)];
    if (candidates.empty())
        for (auto const& band : bands)
            candidates.insert(candidates.end(), band.begin(), band.end());
    if (candidates.empty())
        return 0;

    std::sort(candidates.begin(), candidates.end());
    return rng.Pick(candidates);
}

MapItemData AtlasMgr::RollMapItem(uint8 tier, MapRarity rarity, RunRng& rng) const
{
    MapItemData data;
    data.Tier      = std::clamp<uint8>(tier, 1, MAP_MAX_TIER);
    data.Rarity    = rarity;
    data.SourceMap = PickMapSource(data.Tier, rng);
    auto const& themes = sAtlasConfig->GetThemes();
    data.ThemeId   = themes.empty() ? 0 : rng.Pick(themes).Id;
    data.Seed      = rng.Next() | 1;

    auto const [lo, hi] = MapAffixCountRange(rarity);
    if (hi)
        data.Affixes = sAtlasAffixes->Roll(data.Tier, rng.Range(lo, hi), rng);
    return data;
}

void AtlasMgr::GiveMapItem(Player* player, MapItemData data)
{
    if (!player)
        return;

    PendingReward reward;
    reward.Entry = MapItemEntry(data.Tier, data.Rarity);
    reward.Map   = std::make_shared<MapItemData>(std::move(data));
    QueueReward(player, std::move(reward));
}

std::string AtlasMgr::DescribeMapItem(MapItemData const& data, bool withAffixText, bool plain) const
{
    static char const* const COLOURS[] = { "|cFFFFFFFF", "|cFF1EFF00", "|cFF0070DD" };
    DungeonInfo const* dg = sAtlasConfig->GetDungeon(data.SourceMap);
    Theme const* theme = sAtlasConfig->GetTheme(data.ThemeId);

    std::string out = plain
        ? Acore::StringFormat("Tier {} {}{}", data.Tier, dg ? dg->Name : "an unknown place",
            data.Rarity == MapRarity::Normal ? "" : Acore::StringFormat(" ({})", MapRarityName(data.Rarity)))
        : Acore::StringFormat("{}Tier {} {}|r", COLOURS[std::min<uint8>(uint8(data.Rarity), 2)], data.Tier,
            dg ? dg->Name : "an unknown place");
    if (theme)
        out += ", " + theme->Name;
    if (!data.Affixes.empty())
        out += ": " + sAtlasAffixes->Describe(data.Affixes, withAffixText);
    return plain ? StripColours(out) : out;
}

bool AtlasMgr::HasStarterSigils(ObjectGuid guid) const
{
    std::lock_guard<std::mutex> lock(_mapItemsMutex);
    return _mapGrants.count(guid.GetCounter()) != 0;
}

void AtlasMgr::GrantStarterSigils(Player* player)
{
    if (!player)
        return;

    uint32 const guid = player->GetGUID().GetCounter();
    {
        std::lock_guard<std::mutex> lock(_mapItemsMutex);
        if (!_mapGrants.insert(guid).second)
            return;
    }
    CharacterDatabase.Execute("INSERT IGNORE INTO atlas_map_grant (guid, granted) VALUES ({}, UNIX_TIMESTAMP())", guid);

    RunRng rng(RunRng::MakeSeed());
    for (uint32 i = 0; i < sAtlasConfig->GetMapStarterCount(); ++i)
        GiveMapItem(player, RollMapItem(1, MapRarity::Normal, rng));
}

// ---------------------------------------------------------------------------
// The Lens
// ---------------------------------------------------------------------------

bool AtlasMgr::OpenMapRun(Player* player, GameObject* lens, ObjectGuid itemGuid, std::string& error)
{
    if (!player || !lens)
        return false;

    if (!sAtlasConfig->IsMapsEnabled())
    {
        error = "The Lens is dark. The Keepers have closed it for now.";
        return false;
    }

    std::vector<Player*> const party = GatherParty(player);
    for (Player* member : party)
    {
        if (member->GetLevel() < sAtlasConfig->GetMapMinLevel())
        {
            error = Acore::StringFormat("{} has not yet come into their strength (level {}).", member->GetName(),
                sAtlasConfig->GetMapMinLevel());
            return false;
        }
        if (GetSessionByPlayer(member->GetGUID()))
        {
            error = Acore::StringFormat("{} already walks an echo; mend it or close it first.", member->GetName());
            return false;
        }
    }
    if (Player* capped = FindAtInstanceCap(party))
    {
        error = InstanceCapNotice(capped);
        return false;
    }

    Item* item = player->GetItemByGuid(itemGuid);
    if (!item || !IsMapItemEntry(item->GetEntry()))
    {
        error = "That sigil is no longer with you.";
        return false;
    }

    MapItemData data = GetMapItemData(item);
    DungeonInfo const* dg = sAtlasConfig->GetDungeon(data.SourceMap);
    if (!dg || !CanPartyEnter(party, data.SourceMap))
    {
        error = "The echo in this sigil is sealed to someone in your party: it asks for an attunement or an "
            "item level they lack.";
        return false;
    }
    if (!sAtlasConfig->GetTheme(data.ThemeId))
    {
        auto const& themes = sAtlasConfig->GetThemes();
        data.ThemeId = themes.empty() ? 0 : themes.front().Id;
    }

    Session* created = CreateSession(player, sAtlasConfig->GetMapDifficultyId(), data.ThemeId, data.SourceMap, true,
        data.Seed);
    if (!created)
    {
        error = "The Lens cannot hold another echo right now. Try again later.";
        return false;
    }
    uint32 const sessionId = created->SessionId;

    {
        std::lock_guard<std::mutex> lock(_sessionMutex);
        auto it = _activeSessions.find(sessionId);
        if (it == _activeSessions.end())
            return false;
        Session& s = it->second;
        s.IsMapRun     = true;
        s.Tier         = data.Tier;
        s.MapRarity    = static_cast<uint8>(data.Rarity);
        s.PortalsLeft  = sAtlasConfig->GetMapPortals();
        s.LastOccupied = GameTime::GetGameTime().count();

        // The sigil's affixes, plus any a GM forced (`.atlas affix next`).
        std::vector<ActiveAffix> affixes = data.Affixes;
        affixes.insert(affixes.end(), s.Affixes.begin(), s.Affixes.end());
        SetSessionAffixes(&s, sAtlasAffixes->AtTier(affixes, data.Tier));
    }

    if (!StartDungeon(GetSession(sessionId)))
    {
        AbandonSession(sessionId);
        error = "The echo would not open. Your sigil is unharmed.";
        return false;
    }

    // The sigil is spent.
    uint32 const itemLow = item->GetGUID().GetCounter();
    player->DestroyItem(item->GetBagSlot(), item->GetSlot(), true);
    ForgetMapItem(itemLow);

    GameObject* rift = SummonRift(lens, sessionId);

    uint32 portals = 0;
    std::string affixText;
    {
        std::lock_guard<std::mutex> lock(_sessionMutex);
        auto it = _activeSessions.find(sessionId);
        if (it != _activeSessions.end())
        {
            if (rift)
                it->second.RiftGuid = rift->GetGUID();
            portals = it->second.PortalsLeft;
            if (!it->second.Affixes.empty())
                affixText = sAtlasAffixes->Describe(it->second.Affixes, true);
        }
    }
    if (!rift)
        LOG_ERROR("module", "Atlas: map run {} — the rift beside the Lens could not be summoned.", sessionId);

    LOG_INFO("module", "Atlas: Map run {} opened by {} — tier {}, {}, map {}, theme {}, seed {:016X}, affixes {}",
        sessionId, player->GetName(), data.Tier, MapRarityName(data.Rarity), data.SourceMap, data.ThemeId, data.Seed,
        sAtlasAffixes->Codes(data.Affixes));

    std::string const opened = Acore::StringFormat("{} The Lens takes the sigil. A rift into {} opens beside it.",
        CHAT_PREFIX, DescribeMapItem(data, false));
    std::string const rifts = Acore::StringFormat("{} Crossing is free the first time; after that the rift can carry "
        "you |cFFFFFFFF{}|r more times. Spend them carefully.", CHAT_PREFIX, portals);
    for (Player* member : party)
    {
        Tell(member, opened);
        Tell(member, rifts);
        if (!affixText.empty())
            Tell(member, Acore::StringFormat("{} This echo is unstable: {}", CHAT_PREFIX, affixText));
    }
    return true;
}

std::string AtlasMgr::DescribeMapRun(ObjectGuid playerGuid) const
{
    std::lock_guard<std::mutex> lock(_sessionMutex);
    auto pit = _playerToSession.find(playerGuid);
    if (pit == _playerToSession.end())
        return {};
    auto it = _activeSessions.find(pit->second);
    if (it == _activeSessions.end() || !it->second.IsMapRun)
        return {};

    Session const& s = it->second;
    DungeonInfo const* dg = sAtlasConfig->GetDungeon(s.MapId);
    return Acore::StringFormat("Tier {} {}: {} rift{} left, {}/{} foes, {}/{} masters", s.Tier,
        dg ? dg->Name : "?", s.PortalsLeft, s.PortalsLeft != 1 ? "s" : "", s.MobsKilled, s.TotalMobs,
        s.BossesKilled, s.TotalBosses);
}

void AtlasMgr::CloseMapRun(Player* player)
{
    Session* s = player ? GetSessionByPlayer(player->GetGUID()) : nullptr;
    if (!s || !s->IsMapRun)
        return;
    LOG_INFO("module", "Atlas: Map run {} closed at the Lens by {}.", s->SessionId, player->GetName());
    AbandonSession(s->SessionId);
}

void AtlasMgr::EnsureRift(Player* player, GameObject* lens)
{
    if (!player || !lens)
        return;

    uint32 sessionId = 0;
    ObjectGuid riftGuid;
    {
        std::lock_guard<std::mutex> lock(_sessionMutex);
        auto pit = _playerToSession.find(player->GetGUID());
        auto it = pit != _playerToSession.end() ? _activeSessions.find(pit->second) : _activeSessions.end();
        if (it == _activeSessions.end() || !it->second.IsMapRun || !it->second.IsActive())
            return;
        sessionId = it->second.SessionId;
        riftGuid = it->second.RiftGuid;
    }

    if (riftGuid && lens->GetMap()->GetGameObject(riftGuid))
        return;

    GameObject* rift = SummonRift(lens, sessionId);
    if (!rift)
        return;

    std::lock_guard<std::mutex> lock(_sessionMutex);
    auto it = _activeSessions.find(sessionId);
    if (it != _activeSessions.end())
        it->second.RiftGuid = rift->GetGUID();
    LOG_INFO("module", "Atlas: Map run {} — its rift was gone; the Lens called it back.", sessionId);
}

// ---------------------------------------------------------------------------
// Rifts and the cache
// ---------------------------------------------------------------------------

void AtlasMgr::UseEntryRift(Player* player, GameObject* rift)
{
    if (!player || !rift)
        return;

    std::vector<Player*> travelers;
    Position entrance;
    uint32 mapId = 0;
    uint32 left = 0;
    bool spent = false;
    std::string affixText;
    std::string where;
    {
        std::lock_guard<std::mutex> lock(_sessionMutex);
        Session* s = nullptr;
        for (auto& [id, session] : _activeSessions)
            if (session.IsMapRun && session.RiftGuid == rift->GetGUID())
                s = &session;

        if (!s)
        {
            Tell(player, Acore::StringFormat("{} The rift is fading; it leads nowhere now.", CHAT_PREFIX));
            return;
        }
        if (!s->HasPlayer(player->GetGUID()))
        {
            Tell(player, Acore::StringFormat("{} This rift answers another's sigil.", CHAT_PREFIX));
            return;
        }
        if (!s->IsActive())
        {
            Tell(player, Acore::StringFormat("{} The echo beyond this rift has closed.", CHAT_PREFIX));
            return;
        }

        // The player, and the bots of the run standing in the same place.
        travelers.push_back(player);
        for (PlayerSessionData const& pd : s->Players)
        {
            if (pd.PlayerGuid == player->GetGUID())
                continue;
            Player* member = ObjectAccessor::FindPlayer(pd.PlayerGuid);
            if (member && member->IsInWorld() && !IsRealPlayer(member) && member->GetMapId() == player->GetMapId())
                travelers.push_back(member);
        }

        bool const reentry = std::any_of(travelers.begin(), travelers.end(),
            [s](Player* t) { return Session::Contains(s->Entered, t->GetGUID()); });
        if (reentry)
        {
            if (!s->PortalsLeft)
            {
                Tell(player, Acore::StringFormat("{} The rift has no strength left to carry you.", CHAT_PREFIX));
                return;
            }
            --s->PortalsLeft;
            spent = true;
        }

        if (s->State == SessionState::Preparing)
            s->State = SessionState::InProgress;   // the allmap script populates on the first arrival

        for (Player* t : travelers)
        {
            if (!Session::Contains(s->Entered, t->GetGUID()))
                s->Entered.push_back(t->GetGUID());
            if (PlayerSessionData* pd = s->GetPlayerData(t->GetGUID()))
            {
                pd->ReturnMapId = t->GetMapId();
                pd->ReturnPosition = t->GetPosition();
            }
        }

        entrance = s->EntrancePos;
        mapId = s->MapId;
        left = s->PortalsLeft;
        s->LastOccupied = GameTime::GetGameTime().count();
        if (!spent && !s->Affixes.empty())
            affixText = sAtlasAffixes->Describe(s->Affixes, true);
        DungeonInfo const* dg = sAtlasConfig->GetDungeon(mapId);
        where = dg ? dg->Name : "the echo";
    }

    for (Player* t : travelers)
    {
        if (!t->IsAlive())
        {
            t->RemoveFlag(PLAYER_FIELD_BYTES, PLAYER_FIELD_BYTE_NO_RELEASE_WINDOW);
            t->ResurrectPlayer(1.0f);
            t->SpawnCorpseBones();
        }
        t->TeleportTo(mapId, entrance.GetPositionX(), entrance.GetPositionY(), entrance.GetPositionZ(),
            entrance.GetOrientation());
    }

    Tell(player, spent
        ? Acore::StringFormat("{} The rift carries you back into {}. |cFFFFFFFF{}|r crossing{} left.", CHAT_PREFIX,
            where, left, left != 1 ? "s" : "")
        : Acore::StringFormat("{} You step through the rift into an echo of |cFFFFFFFF{}|r. Defeat its master to "
            "mend it.", CHAT_PREFIX, where));
    if (!affixText.empty())
        Tell(player, Acore::StringFormat("{} This echo is unstable: {}", CHAT_PREFIX, affixText));
}

void AtlasMgr::UseExitRift(Player* player)
{
    if (!player)
        return;

    struct Trip
    {
        Player*  Who = nullptr;
        uint32   MapId = LENS_MAP;
        Position Pos = LENS_POS;
    };
    std::vector<Trip> trips;
    uint32 left = 0;
    bool completed = false;
    bool unclaimed = false;
    {
        std::lock_guard<std::mutex> lock(_sessionMutex);
        auto pit = _playerToSession.find(player->GetGUID());
        auto it = pit != _playerToSession.end() ? _activeSessions.find(pit->second) : _activeSessions.end();
        Session* s = it != _activeSessions.end() && it->second.IsMapRun ? &it->second : nullptr;

        auto tripFor = [&](Player* who)
        {
            Trip trip;
            trip.Who = who;
            PlayerSessionData const* pd = s ? s->GetPlayerData(who->GetGUID()) : nullptr;
            if (pd && pd->ReturnMapId && (!s || pd->ReturnMapId != s->MapId))
            {
                trip.MapId = pd->ReturnMapId;
                trip.Pos = pd->ReturnPosition;
            }
            return trip;
        };

        trips.push_back(tripFor(player));
        if (s)
        {
            for (PlayerSessionData const& pd : s->Players)
            {
                if (pd.PlayerGuid == player->GetGUID())
                    continue;
                Player* member = ObjectAccessor::FindPlayer(pd.PlayerGuid);
                if (member && member->IsInWorld() && !IsRealPlayer(member) && member->GetMap() == player->GetMap())
                    trips.push_back(tripFor(member));
            }
            left = s->PortalsLeft;
            completed = s->State == SessionState::Completed;
            unclaimed = completed && IsRealPlayer(player) && !Session::Contains(s->CacheClaimed, player->GetGUID());
        }
    }

    for (Trip const& trip : trips)
    {
        if (!trip.Who->IsAlive())
        {
            trip.Who->RemoveFlag(PLAYER_FIELD_BYTES, PLAYER_FIELD_BYTE_NO_RELEASE_WINDOW);
            trip.Who->ResurrectPlayer(1.0f);
            trip.Who->SpawnCorpseBones();
        }
        trip.Who->TeleportTo(trip.MapId, trip.Pos.GetPositionX(), trip.Pos.GetPositionY(), trip.Pos.GetPositionZ(),
            trip.Pos.GetOrientation());
    }

    if (unclaimed)
        Tell(player, Acore::StringFormat("{} You leave your share in the cache. The Keepers will send it after you "
            "when the echo closes.", CHAT_PREFIX));
    else if (!completed)
        Tell(player, Acore::StringFormat("{} The rift returns you to the Lens. It can carry you back |cFFFFFFFF{}|r "
            "more time{}.", CHAT_PREFIX, left, left != 1 ? "s" : ""));
}

void AtlasMgr::UseCache(Player* player, GameObject* cache)
{
    if (!player || !cache)
        return;

    std::lock_guard<std::mutex> lock(_sessionMutex);
    auto iit = _instanceToSession.find(cache->GetInstanceId());
    auto it = iit != _instanceToSession.end() ? _activeSessions.find(iit->second) : _activeSessions.end();
    if (it == _activeSessions.end() || !it->second.IsMapRun || it->second.State != SessionState::Completed)
    {
        Tell(player, Acore::StringFormat("{} The cache is sealed.", CHAT_PREFIX));
        return;
    }

    Session& s = it->second;
    if (!s.HasPlayer(player->GetGUID()) || !IsRealPlayer(player))
    {
        Tell(player, Acore::StringFormat("{} This cache was left for others.", CHAT_PREFIX));
        return;
    }
    if (Session::Contains(s.CacheClaimed, player->GetGUID()))
    {
        Tell(player, Acore::StringFormat("{} You have taken your share.", CHAT_PREFIX));
        return;
    }

    s.CacheClaimed.push_back(player->GetGUID());
    GiveMapCompletionReward(s, player);
}

void AtlasMgr::GiveMapCompletionReward(Session const& session, Player* player)
{
    GiveRunReward(session, player);

    // Sustain (DESIGN §14, PLAN 1.5): the master's sigil, mostly the run's tier
    // or one lower; a second one sometimes; one of the next tier for a first
    // clear. Packs dropped theirs as they fell (RollPackSigils). Real players only.
    SigilDropRules const& rules = sAtlasConfig->GetSigilDropRules();
    uint32 const quantity = RunQuantity(session);
    std::array<uint32, 3> const weights = ScaleRarityWeights(sAtlasConfig->GetMapRarityWeights(), RunRarity(session));
    uint8 const tier = static_cast<uint8>(session.Tier);
    RunRng rng(RunRng::MakeSeed());

    std::vector<uint8> tiers{ RollSigilTier(tier, rules.TierShift, rng) };
    if (RollPermille(ScaleChance(rules.ExtraPermille, quantity), rng))
        tiers.push_back(RollSigilTier(tier, rules.TierShift, rng));
    bool const firstClear = Session::Contains(session.FirstClears, player->GetGUID());
    if (firstClear)
        tiers.push_back(std::min<uint8>(MAP_MAX_TIER, tier + 1));

    for (uint8 t : tiers)
        GiveMapItem(player, RollMapItem(t, RollMapRarity(weights, rng), rng));

    Tell(player, Acore::StringFormat("{} The cache opens. The Keepers press {} sigil{} into your hands.",
        CHAT_PREFIX, tiers.size(), tiers.size() != 1 ? "s" : ""));
    if (firstClear)
    {
        DungeonInfo const* dg = sAtlasConfig->GetDungeon(session.MapId);
        Tell(player, Acore::StringFormat("{} The Keepers mark your first mending of {} at tier {}: one sigil "
            "reaches further.", CHAT_PREFIX, dg ? dg->Name : "this echo", tier));
    }
}

void AtlasMgr::MarkFirstClears(Session& session)
{
    if (!sAtlasConfig->IsMapFirstClearBonus())
        return;

    uint32 const bit = 1u << (std::clamp<uint32>(session.Tier, 1, MAP_MAX_TIER) - 1);
    for (PlayerSessionData const& pd : session.Players)
    {
        Player* p = ObjectAccessor::FindPlayer(pd.PlayerGuid);
        if (!p || !IsRealPlayer(p) || Session::Contains(session.FirstClears, pd.PlayerGuid))
            continue;

        uint32 const account = p->GetSession()->GetAccountId();
        bool first = false;
        {
            std::lock_guard<std::mutex> lock(_mapItemsMutex);
            uint32& tiers = _accountProgress[(uint64(account) << 32) | session.MapId];
            first = !(tiers & bit);
            tiers |= bit;
        }
        if (!first)
            continue;

        session.FirstClears.push_back(pd.PlayerGuid);
        CharacterDatabase.Execute("INSERT INTO atlas_account_progress (account, source_map, tiers) VALUES "
            "({}, {}, {}) ON DUPLICATE KEY UPDATE tiers = tiers | {}", account, session.MapId, bit, bit);
    }
}

void AtlasMgr::RollPackSigils(Session& session)
{
    if (session.PacksDone.empty())
        return;

    // Members alive and forces per pack; a pack whose members have all fallen
    // rolls once, for each player inside, a chance that grows with the forces
    // it held: lone creatures drop no more than packs of the same weight.
    std::vector<uint16> alive(session.PacksDone.size(), 0);
    std::vector<uint32> forces(session.PacksDone.size(), 0);
    for (SpawnedCreature const& sc : session.SpawnedCreatures)
    {
        if (!sc.PackId || sc.PackId > session.PacksDone.size())
            continue;
        forces[sc.PackId - 1] += std::max<uint32>(sc.Force, 1);
        if (!sc.IsDead)
            ++alive[sc.PackId - 1];
    }

    SigilDropRules const& rules = sAtlasConfig->GetSigilDropRules();
    uint32 const quantity = RunQuantity(session);
    std::array<uint32, 3> const weights = ScaleRarityWeights(sAtlasConfig->GetMapRarityWeights(), RunRarity(session));
    for (std::size_t i = 0; i < session.PacksDone.size(); ++i)
    {
        if (session.PacksDone[i] || !forces[i] || alive[i])
            continue;
        session.PacksDone[i] = true;
        uint32 const chance = PackSigilChance(forces[i], rules.ForcePer100k, quantity);
        if (!chance)
            continue;

        RunRng rng(RunRng::MakeSeed());
        for (PlayerSessionData const& pd : session.Players)
        {
            Player* p = ObjectAccessor::FindPlayer(pd.PlayerGuid);
            if (!p || !IsRealPlayer(p) || p->GetMapId() != session.MapId || !RollPer100k(chance, rng))
                continue;
            uint8 const tier = RollSigilTier(static_cast<uint8>(session.Tier), rules.TierShift, rng);
            GiveMapItem(p, RollMapItem(tier, RollMapRarity(weights, rng), rng));
            Tell(p, Acore::StringFormat("{} Among the fallen, a sigil hums: an echo of tier {}.", CHAT_PREFIX,
                tier));
            LOG_INFO("module", "Atlas: Map run {} — pack {} ({} forces, chance {:.2f}%) dropped a tier {} sigil "
                "for {}.", session.SessionId, i + 1, forces[i], chance / 1000.0f, tier, p->GetName());
        }
    }
}

void AtlasMgr::CompleteMapRun(Session& session, Position const& bossPos)
{
    session.State   = SessionState::Completed;
    session.EndTime = GameTime::GetGameTime().count();

    bool const fell = bossPos.GetPositionX() != 0.0f || bossPos.GetPositionY() != 0.0f;
    Position const at = fell ? bossPos : session.EntrancePos;
    uint32 const lifetime = sAtlasConfig->GetMapCompletedTimeoutSec() + 120;
    RequestMapSpawn(session.InstanceId, GO_ATLAS_CACHE, Offset(at, at.GetOrientation(), 2.0f), lifetime);
    RequestMapSpawn(session.InstanceId, GO_ATLAS_RIFT_HOME, Offset(at, at.GetOrientation() + float(M_PI) / 2.0f,
        5.0f), lifetime);

    MarkFirstClears(session);

    // Bots take their share at once; they have no use for sigils.
    for (PlayerSessionData const& pd : session.Players)
    {
        Player* p = ObjectAccessor::FindPlayer(pd.PlayerGuid);
        if (!p || IsRealPlayer(p))
            continue;
        session.CacheClaimed.push_back(p->GetGUID());
        GiveRunReward(session, p);
    }

    std::string const text = Acore::StringFormat("{} Echo mended! A Cache of the Keepers waits where its master "
        "fell, and a rift home has opened beside it. The echo closes in |cFFFFFFFF{}|r minutes, or once you leave.",
        CHAT_PREFIX, sAtlasConfig->GetMapCompletedTimeoutSec() / 60);
    for (PlayerSessionData const& pd : session.Players)
        if (Player* p = ObjectAccessor::FindPlayer(pd.PlayerGuid))
            if (IsRealPlayer(p))
                Tell(p, text);

    LOG_INFO("module", "Atlas: Map run {} mended (tier {}, {} wipes, {} rifts left).", session.SessionId,
        session.Tier, session.Wipes, session.PortalsLeft);
}

bool AtlasMgr::IsMapObjectLive(GameObject const* go) const
{
    if (!go)
        return false;

    std::lock_guard<std::mutex> lock(_sessionMutex);
    if (go->GetEntry() == GO_ATLAS_RIFT)
    {
        for (auto const& [id, s] : _activeSessions)
            if (s.IsMapRun && s.RiftGuid == go->GetGUID())
                return s.IsActive();
        return false;
    }

    auto iit = _instanceToSession.find(go->GetInstanceId());
    if (iit == _instanceToSession.end())
        return false;
    auto it = _activeSessions.find(iit->second);
    if (it == _activeSessions.end() || !it->second.IsMapRun)
        return false;
    return go->GetEntry() != GO_ATLAS_CACHE || it->second.State == SessionState::Completed;
}

// ---------------------------------------------------------------------------
// Spawns inside a run, from the run map's own update
// ---------------------------------------------------------------------------

void AtlasMgr::RequestMapSpawn(uint32 instanceId, uint32 goEntry, Position const& pos, uint32 lifetimeSec)
{
    if (!instanceId)
        return;
    std::lock_guard<std::mutex> lock(_mapSpawnMutex);
    _mapSpawns[instanceId].push_back({ goEntry, pos, lifetimeSec });
    _mapSpawnCount.fetch_add(1, std::memory_order_relaxed);
}

void AtlasMgr::ProcessMapSpawns(Map* map)
{
    if (!_mapSpawnCount.load(std::memory_order_relaxed) || !map || !map->IsDungeon())
        return;

    uint64 const now = GameTime::GetGameTime().count();
    std::vector<MapSpawnRequest> requests;
    {
        std::lock_guard<std::mutex> lock(_mapSpawnMutex);
        auto it = _mapSpawns.find(map->GetInstanceId());
        if (it == _mapSpawns.end())
            return;
        // Due ones only; the rest wait in the queue.
        std::vector<MapSpawnRequest> waiting;
        for (MapSpawnRequest& r : it->second)
            (r.Due > now ? waiting : requests).push_back(std::move(r));
        if (requests.empty())
        {
            it->second = std::move(waiting);
            return;
        }
        _mapSpawnCount.fetch_sub(static_cast<uint32>(requests.size()), std::memory_order_relaxed);
        if (waiting.empty())
            _mapSpawns.erase(it);
        else
            it->second = std::move(waiting);
    }

    std::vector<MapSpawnRequest> retry;
    for (MapSpawnRequest const& r : requests)
    {
        // A run's masters, beside its party (UpdateForces called them).
        if (r.SessionId)
        {
            std::lock_guard<std::mutex> lock(_sessionMutex);
            auto it = _activeSessions.find(r.SessionId);
            if (it == _activeSessions.end() || !it->second.IsActive() || it->second.PendingBosses.empty())
                continue;
            SummonPendingBosses(it->second, map);
            if (!it->second.PendingBosses.empty())   // nobody alive inside yet: try again shortly
            {
                MapSpawnRequest again = r;
                again.Due = now + 2;
                retry.push_back(again);
            }
            continue;
        }

        float const o = r.Pos.GetOrientation();
        if (!map->SummonGameObject(r.Entry, r.Pos.GetPositionX(), r.Pos.GetPositionY(), r.Pos.GetPositionZ(), o,
            0.0f, 0.0f, std::sin(o / 2.0f), std::cos(o / 2.0f), r.LifetimeSec))
            LOG_ERROR("module", "Atlas: could not summon gameobject {} in instance {}.", r.Entry,
                map->GetInstanceId());
    }

    if (!retry.empty())
    {
        std::lock_guard<std::mutex> lock(_mapSpawnMutex);
        auto& queue = _mapSpawns[map->GetInstanceId()];
        queue.insert(queue.end(), retry.begin(), retry.end());
        _mapSpawnCount.fetch_add(static_cast<uint32>(retry.size()), std::memory_order_relaxed);
    }
}

} // namespace Atlas
