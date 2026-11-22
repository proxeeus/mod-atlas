/*
 * mod-atlas — AtlasRewards.cpp
 * Rewards: reward and loot pools, run-end rewards, kill XP, gold, items and
 * creature loot.
 *
 * AtlasMgr is split across AtlasMgr.cpp, AtlasPopulation.cpp, AtlasScaling.cpp,
 * AtlasRewards.cpp and AtlasRecords.cpp; the class itself is in AtlasMgr.h.
 */

#include "AtlasMgr.h"
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

// Cache equippable reward items (green/blue/purple)
void AtlasMgr::LoadRewardItems()
{
    _rewardItems.clear();

    QueryResult result = WorldDatabase.Query(
        "SELECT entry, RequiredLevel, Quality, InventoryType, class, subclass, "
        "AllowableClass, ItemLevel "
        "FROM item_template "
        "WHERE Quality >= 2 AND Quality <= 4 "
        "AND RequiredLevel > 0 AND RequiredLevel <= 80 "
        "AND InventoryType > 0 AND InventoryType <= 26 "
        "AND InventoryType NOT IN (18, 19, 24) "
        "AND class IN (2, 4) AND (Flags & 0x8) = 0 "
        "AND AllowableClass != 0 "
        "AND RequiredReputationFaction = 0 "
        "AND RequiredHonorRank = 0 "
        "AND name NOT LIKE '%Test%' "
        "AND name NOT LIKE '%Deprecated%' "
        "AND name NOT LIKE '%[PH]%' "
        "AND name NOT LIKE '%OLD%' "
        "AND name NOT LIKE '%Monster -%' "
        "AND name NOT LIKE '%zzOLD%' "
        "ORDER BY RequiredLevel, Quality");

    if (result)
    {
        do
        {
            Field* f = result->Fetch();
            RewardItem ri;
            ri.Entry         = f[0].Get<uint32>();
            ri.MinLevel      = f[1].Get<uint8>();
            ri.MaxLevel      = ri.MinLevel + 5;
            ri.Quality       = f[2].Get<uint8>();
            ri.InventoryType = f[3].Get<uint32>();
            ri.Class         = f[4].Get<uint32>();
            ri.SubClass      = f[5].Get<uint32>();
            ri.AllowableClass = f[6].Get<int32>();
            ri.ItemLevel     = f[7].Get<uint16>();
            _rewardItems.push_back(ri);
        } while (result->NextRow());
    }

    LOG_INFO("module", "Atlas: {} reward items cached.", _rewardItems.size());
}

// Cache items for mob loot drops
void AtlasMgr::LoadLootPool()
{
    _lootPool.clear();

    // Grey junk, white consumables, green/blue/purple equipment
    QueryResult result = WorldDatabase.Query(
        "SELECT entry, RequiredLevel, Quality, class, subclass, AllowableClass, ItemLevel "
        "FROM item_template "
        "WHERE Quality <= 4 "
        "AND ItemLevel <= 300 "
        "AND SellPrice > 0 "
        "AND class IN (0, 2, 4, 7, 15) "
        "AND (Flags & 0x8) = 0 "
        "AND AllowableClass != 0 "
        "AND RequiredReputationFaction = 0 "
        "AND RequiredHonorRank = 0 "
        "AND (RequiredLevel > 0 OR class NOT IN (2, 4)) "               // equipment must have a required level
        "AND name NOT LIKE '%Test%' "
        "AND name NOT LIKE '%Deprecated%' "
        "AND name NOT LIKE '%[PH]%' "
        "AND name NOT LIKE '%OLD%' "
        "AND name NOT LIKE '%Monster -%' "
        "AND name NOT LIKE '%zzOLD%' "
        "AND name NOT LIKE '%Debug%' "
        "ORDER BY RequiredLevel, Quality");

    if (result)
    {
        do
        {
            Field* f = result->Fetch();
            LootPoolItem li;
            li.Entry          = f[0].Get<uint32>();
            li.MinLevel       = f[1].Get<uint8>();
            li.Quality        = f[2].Get<uint8>();
            li.ItemClass      = f[3].Get<uint8>();
            li.SubClass       = f[4].Get<uint8>();
            li.AllowableClass = f[5].Get<int32>();
            li.ItemLevel      = f[6].Get<uint16>();
            _lootPool.push_back(li);
        } while (result->NextRow());
    }

    uint32 counts[5] = {};
    for (auto const& li : _lootPool)
        if (li.Quality <= 4) ++counts[li.Quality];

    LOG_INFO("module", "Atlas: {} mob loot items cached "
        "(grey={}, white={}, green={}, blue={}, epic={}).",
        _lootPool.size(), counts[0], counts[1], counts[2], counts[3], counts[4]);
}

// Rewards
void AtlasMgr::DistributeRewards(Session* session)
{
    if (!session) return;

    LOG_INFO("module", "Atlas: DistributeRewards — EffectiveLevel={}, rewardPool={} items, players={}",
        session->EffectiveLevel, _rewardItems.size(), session->Players.size());

    for (auto const& pd : session->Players)
    {
        Player* p = ObjectAccessor::FindPlayer(pd.PlayerGuid);
        if (!p || !p->IsInWorld())
        {
            LOG_WARN("module", "Atlas: Player {} not found/not in world for rewards", pd.PlayerGuid.GetCounter());
            continue;
        }
        GiveRunReward(*session, p);
    }
}

void AtlasMgr::GiveRunReward(Session const& session, Player* player)
{
    DifficultyTier const* diff = sAtlasConfig->GetDifficulty(session.DifficultyId);
    if (!diff || !player)
        return;

    uint8  lvl       = session.EffectiveLevel;
    uint32 baseGold  = AtlasConfig::ScaleGoldForLevel(sAtlasConfig->GetBaseGold(), lvl);
    uint32 mobGold   = session.MobsKilled   * AtlasConfig::ScaleGoldForLevel(sAtlasConfig->GetGoldPerMob(), lvl);
    uint32 bossGold  = session.BossesKilled * AtlasConfig::ScaleGoldForLevel(sAtlasConfig->GetGoldPerBoss(), lvl);
    uint32 total     = static_cast<uint32>((baseGold + mobGold + bossGold) * diff->RewardMultiplier);
    uint32 perPlayer = total / std::max<uint32>(1, session.Players.size());

    uint8 rewardLevel = std::min<uint8>(lvl, 80);

    // Gold goes directly to wallet
    GiveGoldReward(player, perPlayer);

    // Completion item (Atlas.Rewards.ItemChance): roll epic first, then
    // rare, fallback green. Goes to the bags, by mail if they are full.
    if (RandInt<uint32>(1, 100) > sAtlasConfig->GetItemChance())
        return;

    uint8 quality = 2;  // green baseline
    if (RandInt<uint32>(1, 100) <= sAtlasConfig->GetEpicChance())
        quality = 4;
    else if (RandInt<uint32>(1, 100) <= sAtlasConfig->GetRareChance())
        quality = 3;

    GiveItemReward(player, rewardLevel, quality);
}

void AtlasMgr::GiveKillXP(Session* session, bool isBoss, bool isElite)
{
    if (!session) return;

    for (auto const& pd : session->Players)
    {
        Player* p = ObjectAccessor::FindPlayer(pd.PlayerGuid);
        if (!p || !p->IsAlive()) continue;
        if (p->GetLevel() >= sWorld->getIntConfig(CONFIG_MAX_PLAYER_LEVEL)) continue;

        uint32 baseXP = (p->GetLevel() * 5) + 45;

        float mult = 1.0f;
        if (isBoss)       mult = 10.0f;
        else if (isElite) mult = 2.0f;

        uint32 xp = static_cast<uint32>(baseXP * mult * sAtlasConfig->GetXPMultiplier());
        p->GiveXP(xp, nullptr);
    }
}

void AtlasMgr::GiveGoldReward(Player* player, uint32 amount)
{
    if (!player || !amount) return;
    player->ModifyMoney(amount);

    if (player->GetSession())
    {
        ChatHandler(player->GetSession()).PSendSysMessage(
            "{} The Keepers of Time reward you: |cFFFFD700{}|rg |cFFC0C0C0{}|rs |cFFB87333{}|rc",
            CHAT_PREFIX, amount / 10000, (amount % 10000) / 100, amount % 100);
    }
}

void AtlasMgr::GiveItemReward(Player* player, uint8 level, uint8 quality)
{
    uint32 playerClass = player->getClass();
    uint32 itemEntry = SelectRewardItem(level, quality, playerClass);

    // Quality fallback: if requested quality isn't found, try lower qualities
    // but still maintain level appropriateness
    if (!itemEntry && quality > 2)
    {
        LOG_WARN("module", "Atlas: No quality {} items for level {}, class {}. Trying lower quality...",
            quality, level, playerClass);
        for (uint8 q = quality - 1; q >= 2 && !itemEntry; --q)
            itemEntry = SelectRewardItem(level, q, playerClass);
    }

    if (!itemEntry)
    {
        LOG_ERROR("module", "Atlas: No suitable reward item for player {} (level {}, class {}, quality {}). "
            "Reward pool has {} items total. Gold only.",
            player->GetName(), level, playerClass, quality, _rewardItems.size());
        if (player->GetSession())
            ChatHandler(player->GetSession()).PSendSysMessage(
                "{} No gear suits your level and class this time; gold only.", CHAT_PREFIX);
        return;
    }

    LOG_DEBUG("module", "Atlas: Giving item {} to {} (level {}, quality {}, class {})",
        itemEntry, player->GetName(), level, quality, playerClass);

    PendingReward reward;
    reward.Entry = itemEntry;
    QueueReward(player, std::move(reward));
}

void AtlasMgr::QueueReward(Player* player, PendingReward reward)
{
    if (!player)
        return;

    bool firstPending = false;
    {
        std::lock_guard<std::mutex> lock(_pendingRewardsMutex);
        std::vector<PendingReward>& pending = _pendingRewards[player->GetGUID()];
        firstPending = pending.empty();
        pending.push_back(std::move(reward));
    }

    // Player events only run while the player is updated on a map, so this
    // fires after any teleport that follows the reward has landed.
    if (firstPending)
    {
        ObjectGuid const guid = player->GetGUID();
        player->m_Events.AddEventAtOffset([guid]() { sAtlasMgr->DeliverPendingRewards(guid); }, 2s);
    }
}

void AtlasMgr::DeliverPendingRewards(ObjectGuid guid)
{
    Player* player = ObjectAccessor::FindPlayer(guid);
    if (!player)
        return;                          // logged out: MailPendingRewards has them

    if (!player->IsInWorld() || player->IsBeingTeleported())
    {
        player->m_Events.AddEventAtOffset([guid]() { sAtlasMgr->DeliverPendingRewards(guid); }, 1s);
        return;
    }

    std::vector<PendingReward> items;
    {
        std::lock_guard<std::mutex> lock(_pendingRewardsMutex);
        auto it = _pendingRewards.find(guid);
        if (it == _pendingRewards.end())
            return;
        items = std::move(it->second);
        _pendingRewards.erase(it);
    }

    for (PendingReward const& reward : items)
        StoreOrMailRewardItem(player, reward);
}

void AtlasMgr::MailPendingRewards(Player* player)
{
    if (!player)
        return;

    std::vector<PendingReward> items;
    {
        std::lock_guard<std::mutex> lock(_pendingRewardsMutex);
        auto it = _pendingRewards.find(player->GetGUID());
        if (it == _pendingRewards.end())
            return;
        items = std::move(it->second);
        _pendingRewards.erase(it);
    }

    CharacterDatabaseTransaction trans = CharacterDatabase.BeginTransaction();
    for (PendingReward const& reward : items)
    {
        Item* mailItem = Item::CreateItem(reward.Entry, 1, player);
        if (!mailItem)
        {
            LOG_ERROR("module", "Atlas: Failed to create mail item {} for {}", reward.Entry, player->GetName());
            continue;
        }
        mailItem->SaveToDB(trans);       // see StoreOrMailRewardItem
        if (reward.Map)
        {
            MapItemData data = *reward.Map;
            data.ItemGuid = mailItem->GetGUID().GetCounter();
            data.OwnerGuid = player->GetGUID().GetCounter();
            PersistMapItem(data);
        }
        MailDraft("A reward from the Keepers of Time",
            "You left before we could hand it to you. The timeways thank you.")
            .AddItem(mailItem)
            .SendMailTo(trans, MailReceiver(player, player->GetGUID().GetCounter()),
                MailSender(MAIL_NORMAL, 0, MAIL_STATIONERY_GM));
    }
    CharacterDatabase.CommitTransaction(trans);
    LOG_INFO("module", "Atlas: {} logged out with {} reward item(s) waiting; sent by mail.", player->GetName(),
        items.size());
}

void AtlasMgr::StoreOrMailRewardItem(Player* player, PendingReward const& reward)
{
    uint32 const itemEntry = reward.Entry;
    ItemTemplate const* t = sObjectMgr->GetItemTemplate(itemEntry);

    // A sigil's data goes with the item it was made into.
    auto persist = [&](Item const* item)
    {
        if (!reward.Map || !item)
            return;
        MapItemData data = *reward.Map;
        data.ItemGuid = item->GetGUID().GetCounter();
        data.OwnerGuid = player->GetGUID().GetCounter();
        PersistMapItem(data);
    };

    ItemPosCountVec dest;
    if (player->CanStoreNewItem(NULL_BAG, NULL_SLOT, dest, itemEntry, 1) == EQUIP_ERR_OK)
    {
        if (Item* item = player->StoreNewItem(dest, itemEntry, true))
        {
            persist(item);
            player->SendNewItem(item, 1, true, false);
            if (t && player->GetSession())
            {
                std::string const what = reward.Map ? DescribeMapItem(*reward.Map, false) : t->Name1;
                ChatHandler(player->GetSession()).PSendSysMessage(
                    "{} The Keepers of Time reward you: |cFFFFFFFF{}|r", CHAT_PREFIX, what);
            }
        }
        return;
    }

    // Bags full — mail the item instead.
    Item* mailItem = Item::CreateItem(itemEntry, 1, player);
    if (!mailItem)
    {
        LOG_ERROR("module", "Atlas: Failed to create mail item {} for {}", itemEntry, player->GetName());
        return;
    }

    CharacterDatabaseTransaction trans = CharacterDatabase.BeginTransaction();
    // The mail only links the item: save it first, or the core finds no item
    // behind the mail when it loads and deletes it.
    mailItem->SaveToDB(trans);
    persist(mailItem);
    MailDraft("A reward from the Keepers of Time",
        "Your bags were full, so we sent it this way. The timeways thank you.")
        .AddItem(mailItem)
        .SendMailTo(trans,
            MailReceiver(player, player->GetGUID().GetCounter()),
            MailSender(MAIL_NORMAL, 0, MAIL_STATIONERY_GM));
    CharacterDatabase.CommitTransaction(trans);
    if (player->GetSession())
        ChatHandler(player->GetSession()).PSendSysMessage(
            "{} Your bags are full; your reward was sent by mail.", CHAT_PREFIX);
}

    // Mail a reward item to player
void AtlasMgr::MailItemReward(Player* player, uint8 level, uint8 quality,
                                       std::string const& subject, std::string const& body)
{
    if (!player || !player->IsInWorld()) return;

    uint32 playerClass = player->getClass();
    uint32 itemEntry = SelectRewardItem(level, quality, playerClass);

    // Quality fallback
    if (!itemEntry && quality > 2)
    {
        for (uint8 q = quality - 1; q >= 2 && !itemEntry; --q)
            itemEntry = SelectRewardItem(level, q, playerClass);
    }

    // Level window fallback
    if (!itemEntry)
    {
        uint8 windows[] = { 15, 25, 80 };
        for (uint8 w : windows)
        {
            uint8 lo = (level > w) ? level - w : 1;
            uint8 hi = std::min<uint8>(level + w, 80);
            for (auto const& ri : _rewardItems)
            {
                if (ri.Quality < 2 || ri.Quality > 4) continue;
                if (ri.MinLevel < lo || ri.MinLevel > hi) continue;
                if (ri.AllowableClass != -1 && !(ri.AllowableClass & (1 << (playerClass - 1))))
                    continue;
                itemEntry = ri.Entry;
                break;
            }
            if (itemEntry) break;
        }
    }

    if (!itemEntry)
    {
        LOG_ERROR("module", "Atlas: No reward item found for mail to {} (level {}, class {})",
            player->GetName(), level, playerClass);
        return;
    }

    Item* mailItem = Item::CreateItem(itemEntry, 1, player);
    if (mailItem)
    {
        // Must use a real transaction (nullptr crashes SendMailTo)
        // inside SendMailTo when it calls trans->Append() on the null ptr.
        CharacterDatabaseTransaction trans = CharacterDatabase.BeginTransaction();
        mailItem->SaveToDB(trans);       // see GiveItemReward

        MailDraft(subject, body)
            .AddItem(mailItem)
            .SendMailTo(trans,
                MailReceiver(player, player->GetGUID().GetCounter()),
                MailSender(MAIL_NORMAL, 0, MAIL_STATIONERY_GM));

        CharacterDatabase.CommitTransaction(trans);

        if (ItemTemplate const* t = sObjectMgr->GetItemTemplate(itemEntry))
        {
            if (player->GetSession())
            {
                ChatHandler(player->GetSession()).PSendSysMessage(
                    "{} Reward sent by mail: |cFFFFFFFF{}|r", CHAT_PREFIX, t->Name1);
            }
        }
    }
    else
    {
        LOG_ERROR("module", "Atlas: Failed to create mail item {} for {}", itemEntry, player->GetName());
    }
}

void AtlasMgr::DistributeRoguelikeRewards(uint32 tier, uint8 effectiveLevel,
                                                    std::vector<ObjectGuid> const& playerGuids)
{
    uint8 rewardLevel = static_cast<uint8>(std::min<uint32>(effectiveLevel, 80));

    uint32 baseGold = AtlasConfig::ScaleGoldForLevel(sAtlasConfig->GetBaseGold(), effectiveLevel);
    uint32 tierGold = baseGold * tier;

    // Epic chance scales with tier
    uint32 epicChance = std::min<uint32>(5 + (tier * 5), 80);

    uint32 blueItems  = 1;
    uint32 greenItems = 0;
    uint32 epicItems  = 0;

    if (tier >= 9)      { blueItems = 3; epicItems = 1; }
    else if (tier >= 7) { blueItems = 2; epicItems = 1; }
    else if (tier >= 5) { blueItems = 2; }
    else if (tier >= 3) { blueItems = 1; greenItems = 1; }

    LOG_INFO("module", "Atlas: DistributeRoguelikeRewards — tier={}, level={}, "
        "blue={}, green={}, epic={}, epicChance={}%, gold={}",
        tier, rewardLevel, blueItems, greenItems, epicItems, epicChance, tierGold);

    for (auto const& guid : playerGuids)
    {
        Player* p = ObjectAccessor::FindPlayer(guid);
        if (!p || !p->IsInWorld()) continue;

        // Gold
        GiveGoldReward(p, tierGold);

        // Items go directly to inventory (mail fallback if bags full)

        // Guaranteed epic items
        for (uint32 i = 0; i < epicItems; ++i)
            GiveItemReward(p, rewardLevel, 4);

        // Roll for bonus epics
        if (epicItems == 0 && RandInt<uint32>(1, 100) <= epicChance)
            GiveItemReward(p, rewardLevel, 4);
        else if (epicItems > 0 && tier >= 9 && RandInt<uint32>(1, 100) <= 25)
            GiveItemReward(p, rewardLevel, 4);

        // Blue items
        for (uint32 i = 0; i < blueItems; ++i)
            GiveItemReward(p, rewardLevel, 3);

        // Green items
        for (uint32 i = 0; i < greenItems; ++i)
            GiveItemReward(p, rewardLevel, 2);

        if (p->GetSession())
            ChatHandler(p->GetSession()).PSendSysMessage(
                "{} The Keepers of Time reward your descent. Check your bags.", CHAT_PREFIX_ENDLESS);
    }
}

// Armor type by class
static uint8 GetMaxArmorSubclass(uint32 playerClass)
{
    switch (playerClass)
    {
        case 5: case 8: case 9:              return 1;  // cloth: Priest, Mage, Warlock
        case 4: case 11:                     return 2;  // leather: Rogue, Druid
        case 3: case 7:                      return 3;  // mail: Hunter, Shaman
        case 1: case 2: case 6:              return 4;  // plate: Warrior, Paladin, DK
        default:                             return 4;
    }
}

static uint32 GetClassBitmask(uint32 playerClass)
{
    if (playerClass == 0 || playerClass > 11) return 0x7FF;  // all classes
    return 1 << (playerClass - 1);
}

// Primary stat for class-based reward weighting
// Returns: ITEM_MOD_AGILITY(3), ITEM_MOD_STRENGTH(4), ITEM_MOD_INTELLECT(5)
static uint32 GetPrimaryStatForClass(uint32 playerClass)
{
    switch (playerClass)
    {
        case 1:  return 4;  // Warrior  -> STR
        case 2:  return 4;  // Paladin  -> STR
        case 3:  return 3;  // Hunter   -> AGI
        case 4:  return 3;  // Rogue    -> AGI
        case 5:  return 5;  // Priest   -> INT
        case 6:  return 4;  // DK       -> STR
        case 7:  return 5;  // Shaman   -> INT
        case 8:  return 5;  // Mage     -> INT
        case 9:  return 5;  // Warlock  -> INT
        case 11: return 3;  // Druid    -> AGI
        default: return 4;
    }
}

// Score item stat alignment with player class (0.0 = bad, 1.0 = perfect match)
static float ScoreItemForClass(uint32 itemEntry, uint32 playerClass)
{
    ItemTemplate const* proto = sObjectMgr->GetItemTemplate(itemEntry);
    if (!proto) return 0.0f;

    uint32 primaryStat = GetPrimaryStatForClass(playerClass);
    float totalStats = 0.0f;
    float primaryTotal = 0.0f;

    for (uint8 i = 0; i < MAX_ITEM_PROTO_STATS; ++i)
    {
        int32 val = proto->ItemStat[i].ItemStatValue;
        uint32 type = proto->ItemStat[i].ItemStatType;
        if (val <= 0) continue;

        totalStats += static_cast<float>(val);
        if (type == primaryStat)
            primaryTotal += static_cast<float>(val);
    }

    if (totalStats <= 0.0f) return 0.5f;  // No stats (trinket, etc.) = neutral
    return primaryTotal / totalStats;
}

// Atlas.Rewards.MaxItemLevel: keeps raid-tier gear (ICC, arena) out of the pools.
// 0 disables the ceiling.
static bool IsUnderItemLevelCeiling(uint16 itemLevel)
{
    uint16 const ceiling = sAtlasConfig->GetMaxItemLevel();
    return ceiling == 0 || itemLevel <= ceiling;
}

uint32 AtlasMgr::SelectRewardItem(uint8 level, uint8 quality, uint32 playerClass)
{
    uint8  maxArmor   = GetMaxArmorSubclass(playerClass);
    uint32 classMask  = GetClassBitmask(playerClass);

    // Try progressively wider level windows, but always prefer closer to player level
    struct { uint8 below; uint8 above; } windows[] = {
        { 3, 0 },    // strict: [level-3, level]
        { 8, 0 },    // medium: [level-8, level]
        { 15, 0 },   // wide: [level-15, level]
        { 25, 0 },   // very wide: [level-25, level]
        { 80, 0 },   // last resort: [1, level] (never items above player level)
    };

    for (auto const& win : windows)
    {
        std::vector<uint32> cands;
        uint8 lo = (level > win.below) ? (level - win.below) : 1;
        uint8 hi = level;  // Never give items above player level

        for (auto const& ri : _rewardItems)
        {
            // Quality filter
            if (ri.Quality != quality) continue;
            if (!IsUnderItemLevelCeiling(ri.ItemLevel)) continue;

            // Level filter: item RequiredLevel must be within window
            if (ri.MinLevel < lo || ri.MinLevel > hi) continue;

            // Class restriction: AllowableClass bitmask check
            if (ri.AllowableClass != -1 && !(ri.AllowableClass & classMask))
                continue;

            // Armor subclass: player can only wear their class's max armor or lower
            if (ri.Class == 4 && ri.SubClass > 0 && ri.SubClass <= 4)
            {
                if (ri.SubClass > maxArmor) continue;
            }

            cands.push_back(ri.Entry);
        }

        if (!cands.empty())
        {
            LOG_DEBUG("module", "Atlas: SelectRewardItem(level={}, quality={}, class={}) "
                "-> {} candidates in window [{}, {}]",
                level, quality, playerClass, cands.size(), lo, hi);

            // 75% chance: bias toward items with matching primary stat
            if (cands.size() > 3 && playerClass > 0 && RandInt<uint32>(1, 100) <= 75)
            {
                std::vector<std::pair<uint32, float>> scored;
                scored.reserve(cands.size());
                for (uint32 entry : cands)
                    scored.push_back({entry, ScoreItemForClass(entry, playerClass)});

                std::sort(scored.begin(), scored.end(),
                    [](auto const& a, auto const& b) { return a.second > b.second; });

                // Pick from the top third (at least 3 items)
                size_t topN = std::max<size_t>(3, scored.size() / 3);
                return scored[RandInt<size_t>(0, topN - 1)].first;
            }

            // 25% chance: purely random from valid pool
            return cands[RandInt<size_t>(0, cands.size() - 1)];
        }
    }

    LOG_WARN("module", "Atlas: SelectRewardItem(level={}, quality={}, class={}) "
        "-> NO candidates found in reward pool ({} items total)",
        level, quality, playerClass, _rewardItems.size());

    return 0;
}

uint32 AtlasMgr::SelectLootItem(uint8 level, uint8 minQuality, uint8 maxQuality,
                                        bool equipmentOnly, uint32 playerClass)
{
    // Expected ItemLevel range for this level
    uint16 expectedMaxIlvl = static_cast<uint16>(level) * 2 + 10;

    uint8  maxArmor  = playerClass ? GetMaxArmorSubclass(playerClass) : 4;
    uint32 classMask = playerClass ? GetClassBitmask(playerClass) : 0x7FF;

    // Progressively widen level windows, always preferring items closer to player level
    struct { uint8 below; uint8 above; } windows[] = {
        { 3, 1 },   // strict: RequiredLevel in [level-3, level+1]
        { 5, 2 },   // medium
        { 8, 3 },   // wide
        { 15, 5 },  // very wide
        { 25, 8 },  // extremely wide (last resort)
    };

    for (auto const& win : windows)
    {
        uint8 lo = (level > win.below) ? (level - win.below) : 0;
        uint8 hi = std::min<uint16>(level + win.above, 83);

        std::vector<uint32> cands;
        for (auto const& li : _lootPool)
        {
            if (li.Quality < minQuality || li.Quality > maxQuality) continue;
            if (!IsUnderItemLevelCeiling(li.ItemLevel)) continue;
            if (equipmentOnly && li.ItemClass != 2 && li.ItemClass != 4) continue;

            // Level filter for items with RequiredLevel > 0
            if (li.MinLevel > 0)
            {
                if (li.MinLevel < lo || li.MinLevel > hi) continue;
            }
            else
            {
                // RequiredLevel = 0: use ItemLevel as a sanity check
                if (li.ItemLevel > expectedMaxIlvl) continue;
            }

            // Class restriction for equipment items
            if (equipmentOnly || (li.ItemClass == 2 || li.ItemClass == 4))
            {
                // AllowableClass bitmask check
                if (li.AllowableClass != -1 && !(li.AllowableClass & classMask))
                    continue;

                // Armor subclass check (only for armor, not weapons)
                if (li.ItemClass == 4 && li.SubClass > 0 && li.SubClass <= 4)
                    if (li.SubClass > maxArmor) continue;
            }

            cands.push_back(li.Entry);
        }

        if (!cands.empty())
        {
            LOG_DEBUG("module", "Atlas: SelectLootItem(level={}, quality={}-{}, eqOnly={}, class={}) "
                "-> {} candidates in window [{}, {}]",
                level, minQuality, maxQuality, equipmentOnly, playerClass, cands.size(), lo, hi);

            // Bias equipment loot toward matching primary stat (75% chance)
            if (equipmentOnly && playerClass > 0 && cands.size() > 3
                && RandInt<uint32>(1, 100) <= 75)
            {
                std::vector<std::pair<uint32, float>> scored;
                scored.reserve(cands.size());
                for (uint32 entry : cands)
                    scored.push_back({entry, ScoreItemForClass(entry, playerClass)});

                std::sort(scored.begin(), scored.end(),
                    [](auto const& a, auto const& b) { return a.second > b.second; });

                size_t topN = std::max<size_t>(3, scored.size() / 3);
                return scored[RandInt<size_t>(0, topN - 1)].first;
            }

            return cands[RandInt<size_t>(0, cands.size() - 1)];
        }
    }

    LOG_WARN("module", "Atlas: SelectLootItem(level={}, quality={}-{}, eqOnly={}, class={}) "
        "-> NO candidates found in loot pool ({} items total)",
        level, minQuality, maxQuality, equipmentOnly, playerClass, _lootPool.size());

    return 0;
}

void AtlasMgr::FillCreatureLoot(Creature* creature, Session* session, bool isBoss)
{
    if (!creature || !session) return;

    Loot& loot = creature->loot;
    loot.clear();

    uint8 level = session->EffectiveLevel;

    // Pick a random party member's class for loot filtering
    uint32 lootClass = 0;
    if (!session->Players.empty())
    {
        // Try to pick a random alive player's class
        std::vector<uint32> classes;
        for (auto const& pd : session->Players)
        {
            Player* p = ObjectAccessor::FindPlayer(pd.PlayerGuid);
            if (p && p->IsAlive())
                classes.push_back(p->getClass());
        }
        if (classes.empty())
        {
            // All dead? Just pick from any player
            for (auto const& pd : session->Players)
            {
                Player* p = ObjectAccessor::FindPlayer(pd.PlayerGuid);
                if (p) { classes.push_back(p->getClass()); break; }
            }
        }
        if (!classes.empty())
            lootClass = classes[RandInt<size_t>(0, classes.size() - 1)];
    }

    // Gold drop
    uint32 baseGold = isBoss ? (level * 2000u) : (level * 200u);
    loot.gold = std::max(500u, baseGold + RandInt<uint32>(0, baseGold / 3));

    // Item drops
    uint32 itemsAdded = 0;
    auto addItem = [&](uint8 minQ, uint8 maxQ, bool eqOnly) -> bool
    {
        uint32 entry = SelectLootItem(level, minQ, maxQ, eqOnly, eqOnly ? lootClass : 0);
        if (!entry)
        {
            LOG_WARN("module", "Atlas: FillCreatureLoot failed to find item (level={}, quality={}-{}, eqOnly={}, class={})",
                level, minQ, maxQ, eqOnly, lootClass);
            return false;
        }

        LootStoreItem storeItem(entry, 0, 100.0f, false, 1, 0, 1, 1);
        loot.AddItem(storeItem);
        ++itemsAdded;
        LOG_DEBUG("module", "Atlas: Added loot item {} (quality {}-{}) to {} (boss={})",
            entry, minQ, maxQ, creature->GetName(), isBoss);
        return true;
    };

    if (isBoss)
    {
        // Boss: 2 guaranteed rare (blue) equipment pieces
        if (!addItem(3, 3, true))
            addItem(2, 3, true);   // fallback to green/blue if no rare at this level
        if (!addItem(3, 3, true))
            addItem(2, 3, true);
    }
    else
    {
        bool isElite = false;
        bool isRare  = false;
        for (auto const& sc : session->SpawnedCreatures)
        {
            if (sc.Guid == creature->GetGUID())
            {
                isElite = sc.IsElite;
                isRare  = sc.IsRare;
                break;
            }
        }

        if (isRare)
        {
            // Rare spawn: guaranteed blue equipment piece
            if (!addItem(3, 3, true))
                addItem(2, 3, true);
        }
        else if (isElite)
        {
            // Elite: 40% chance of green equipment
            if (RandInt<uint32>(1, 100) <= 40)
            {
                if (!addItem(2, 2, true))
                    addItem(2, 2, false);
            }
        }
        else
        {
            // Trash: 15% grey/white junk, 3% green equipment
            if (RandInt<uint32>(1, 100) <= 15)
                addItem(0, 1, false);
            if (RandInt<uint32>(1, 100) <= 3)
                addItem(2, 2, true);
        }
    }

    // Ensure lootable flag is set (critical for boss loot)
    creature->SetDynamicFlag(UNIT_DYNFLAG_LOOTABLE);

    // --- Group Loot Support (Need/Greed) ---
    // If the killing player is in a group with Group Loot or Need Before Greed,
    // trigger the group's loot distribution system for qualifying items.
    loot.loot_type = LOOT_CORPSE;
    Player* looter = nullptr;
    Group*  group  = nullptr;
    for (auto const& pd : session->Players)
    {
        Player* p = ObjectAccessor::FindPlayer(pd.PlayerGuid);
        if (p && p->IsInWorld() && p->GetGroup())
        {
            looter = p;
            group  = p->GetGroup();
            break;
        }
    }

    if (group && looter)
    {
        // Set both individual and group loot recipient so the group's loot
        // distribution system (Need/Greed/Pass) is properly triggered.
        creature->SetLootRecipient(looter, true);

        // Mark items above the group's loot threshold for rolling;
        // items below threshold become free-for-all (direct loot)
        uint8 threshold = group->GetLootThreshold();
        for (auto& item : loot.items)
        {
            ItemTemplate const* proto = sObjectMgr->GetItemTemplate(item.itemid);
            if (!proto || proto->Quality < threshold)
                item.is_underthreshold = true;
        }

        // Trigger group loot distribution — sends Need/Greed/Pass rolls
        // to all eligible group members for qualifying items
        group->GroupLoot(&loot, creature);

        // Force dynamic flag update to all session players so they see the lootable corpse
        for (auto const& pd : session->Players)
        {
            Player* p = ObjectAccessor::FindPlayer(pd.PlayerGuid);
            if (p && p->IsInWorld() && p->GetMapId() == session->MapId)
                creature->SendUpdateToPlayer(p);
        }

        LOG_DEBUG("module", "Atlas: Group loot triggered for {} — {} items, "
            "lootMethod={}, threshold={}, groupSize={}",
            creature->GetName(), loot.items.size(),
            static_cast<int>(group->GetLootMethod()), threshold, group->GetMembersCount());
    }
    else if (looter)
    {
        creature->SetLootRecipient(looter);
    }

    LOG_DEBUG("module", "Atlas: FillCreatureLoot complete for {} (GUID: {}, Boss: {}, Level: {}, Gold: {}, Items: {})",
        creature->GetName(), creature->GetGUID().GetCounter(), isBoss, level, loot.gold, itemsAdded);
}

} // namespace Atlas
