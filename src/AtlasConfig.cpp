/*
 * mod-atlas — AtlasConfig.cpp
 * Configuration loader.
 */

#include "AtlasConfig.h"
#include "Config.h"
#include "Log.h"
#include <sstream>
#include <algorithm>

namespace Atlas
{

static std::vector<std::string> SplitString(std::string const& str, char delim)
{
    std::vector<std::string> tokens;
    std::stringstream ss(str);
    std::string token;
    while (std::getline(ss, token, delim))
    {
        size_t start = token.find_first_not_of(" \t");
        size_t end   = token.find_last_not_of(" \t");
        if (start != std::string::npos && end != std::string::npos)
            tokens.push_back(token.substr(start, end - start + 1));
        else if (!token.empty())
            tokens.push_back(token);
    }
    return tokens;
}

static std::string StripQuotes(std::string const& s)
{
    std::string out = s;
    if (!out.empty() && out.front() == '"') out.erase(out.begin());
    if (!out.empty() && out.back()  == '"') out.pop_back();
    return out;
}

// Singleton
AtlasConfig* AtlasConfig::Instance()
{
    static AtlasConfig instance;
    return &instance;
}

float AtlasConfig::GetVisualScaleForCreatureType(uint32 type) const
{
    switch (type)
    {
        case 1: return _visualScaleBeast;
        case 2: return _visualScaleDragonkin;
        case 3: return _visualScaleDemon;
        case 4: return _visualScaleElemental;
        case 5: return _visualScaleGiant;
        case 6: return _visualScaleUndead;
        case 7: return _visualScaleHumanoid;
        case 9: return _visualScaleMechanical;
        default: return 1.0f;
    }
}

// Load all config values
void AtlasConfig::LoadConfig(bool reload)
{
    if (reload)
        LOG_INFO("module", "Atlas: Reloading configuration...");

    // Off unless explicitly enabled: a server that compiles the module in must
    // not start running it just because no config file was copied yet.
    _enabled  = sConfigMgr->GetOption<bool>  ("Atlas.Enable", false);
    _debug    = sConfigMgr->GetOption<bool>  ("Atlas.Debug",  false);
    _npcEntry = sConfigMgr->GetOption<uint32>("Atlas.NpcEntry", 500000);

    // Scaling
    _levelBand       = sConfigMgr->GetOption<uint8> ("Atlas.Scaling.LevelBand",        3);
    _perPlayerHealth = sConfigMgr->GetOption<float> ("Atlas.Scaling.PerPlayerHealth",   0.25f);
    _perPlayerDamage = sConfigMgr->GetOption<float> ("Atlas.Scaling.PerPlayerDamage",   0.10f);
    _soloMultiplier  = sConfigMgr->GetOption<float> ("Atlas.Scaling.SoloMultiplier",    0.50f);
    _tierGrowth      = std::max(1.0f, sConfigMgr->GetOption<float>("Atlas.Scaling.TierGrowth", 1.075f));
    _spellScaleMax   = std::max(1.0f, sConfigMgr->GetOption<float>("Atlas.Scaling.SpellScaleMax", 20.0f));

    // Atlas.Scaling.Role.<Name> = "health,damage,armor"
    for (uint8 r = 0; r < static_cast<uint8>(CreatureRole::Count); ++r)
    {
        CreatureRole const role = static_cast<CreatureRole>(r);
        RoleMultipliers mult = DefaultRoleMultipliers(role);
        std::string const key = std::string("Atlas.Scaling.Role.") + RoleName(role);
        std::string const val = StripQuotes(sConfigMgr->GetOption<std::string>(key, "", false));
        if (!val.empty())
        {
            auto parts = SplitString(val, ',');
            try
            {
                if (parts.size() > 0) mult.Health = std::max(0.01f, std::stof(parts[0]));
                if (parts.size() > 1) mult.Damage = std::max(0.01f, std::stof(parts[1]));
                if (parts.size() > 2) mult.Armor  = std::max(0.0f, std::stof(parts[2]));
            }
            catch (...)
            {
                LOG_ERROR("module", "Atlas: Bad {} \"{}\"; using the defaults.", key, val);
                mult = DefaultRoleMultipliers(role);
            }
        }
        _roles[r] = mult;
    }

    // Rewards
    _baseGold     = sConfigMgr->GetOption<uint32>("Atlas.Rewards.BaseGold",    40000);
    _goldPerMob   = sConfigMgr->GetOption<uint32>("Atlas.Rewards.GoldPerMob",  800);
    _goldPerBoss  = sConfigMgr->GetOption<uint32>("Atlas.Rewards.GoldPerBoss", 40000);
    _xpMultiplier = sConfigMgr->GetOption<float> ("Atlas.Rewards.XPMultiplier", 1.0f);
    _itemChance   = sConfigMgr->GetOption<uint32>("Atlas.Rewards.ItemChance",  80);
    _rareChance   = sConfigMgr->GetOption<uint32>("Atlas.Rewards.RareChance",  40);
    _epicChance   = sConfigMgr->GetOption<uint32>("Atlas.Rewards.EpicChance",  15);
    _maxItemLevel = sConfigMgr->GetOption<uint16>("Atlas.Rewards.MaxItemLevel", 219);

    // Dungeon settings
    _bossCount       = sConfigMgr->GetOption<uint32>("Atlas.Dungeon.BossCount",      1);
    _maxRunCreatures = sConfigMgr->GetOption<uint32>("Atlas.Dungeon.MaxCreatures",   450);
    _maxRares        = sConfigMgr->GetOption<uint32>("Atlas.Dungeon.MaxRares",       2);
    _packRadius      = std::clamp(sConfigMgr->GetOption<float>("Atlas.Population.PackRadius", 8.0f), 1.0f, 30.0f);
    _maxPackSize     = std::clamp<uint32>(sConfigMgr->GetOption<uint32>("Atlas.Population.MaxPackSize", 6), 1, 20);
    _keepSmartAI     = sConfigMgr->GetOption<bool>  ("Atlas.Population.KeepSmartAI", true);
    _scourgeThemeId  = sConfigMgr->GetOption<uint32>("Atlas.Corruption.ScourgeTheme", 51);
    _touchedAura     = sConfigMgr->GetOption<uint32>("Atlas.Corruption.TouchedAura", 10848);
    _touchedAffix    = StripQuotes(sConfigMgr->GetOption<std::string>("Atlas.Corruption.TouchedAffix", "M04"));

    // Atlas.Corruption.Shares = "native,touched,scourge;..." for levels 0-5.
    for (uint8 level = 0; level < 6; ++level)
        _corruption[level] = DefaultCorruptionShares(level);
    std::string const shares = StripQuotes(sConfigMgr->GetOption<std::string>("Atlas.Corruption.Shares", "", false));
    if (!shares.empty())
    {
        auto levels = SplitString(shares, ';');
        for (uint8 level = 0; level < 6 && level < levels.size(); ++level)
        {
            auto parts = SplitString(levels[level], ',');
            try
            {
                if (parts.size() == 3)
                    _corruption[level] = { static_cast<uint32>(std::stoul(parts[0])),
                        static_cast<uint32>(std::stoul(parts[1])), static_cast<uint32>(std::stoul(parts[2])) };
            }
            catch (...)
            {
                LOG_ERROR("module", "Atlas: Bad Atlas.Corruption.Shares level {}; using the default.", level);
            }
        }
    }
    _affixBotSafeOnly = sConfigMgr->GetOption<bool>("Atlas.Affix.BotSafeOnly",       true);

    // Maps (PLAN 1.4)
    _mapsEnabled      = sConfigMgr->GetOption<bool>  ("Atlas.Map.Enable",             true);
    _mapMinLevel      = static_cast<uint8>(std::clamp<uint32>(
        sConfigMgr->GetOption<uint32>("Atlas.Map.MinLevel", 80), 1, 80));
    _mapPortals       = std::clamp<uint32>(sConfigMgr->GetOption<uint32>("Atlas.Map.Portals", 6), 1, 20);
    _mapDifficultyId  = sConfigMgr->GetOption<uint32>("Atlas.Map.Difficulty",         0);
    _mapEmptyTimeoutMin = std::clamp<uint32>(sConfigMgr->GetOption<uint32>("Atlas.Map.EmptyTimeout", 20), 1, 25);
    _mapCompletedTimeoutMin =
        std::clamp<uint32>(sConfigMgr->GetOption<uint32>("Atlas.Map.CompletedTimeout", 10), 1, 25);
    _mapStarterCount  = std::min<uint32>(sConfigMgr->GetOption<uint32>("Atlas.Map.StarterSigils", 3), 10);
    // Sigil drops (PLAN 1.5); chances in % (packs in tenths), kept in per mille.
    _sigilRules = SigilDropRules();
    _sigilRules.ExtraPermille = static_cast<uint32>(std::clamp(
        sConfigMgr->GetOption<float>("Atlas.Map.ExtraDropChance", 20.0f), 0.0f, 100.0f) * 10.0f + 0.5f);
    _sigilRules.PackPermille = static_cast<uint32>(std::clamp(
        sConfigMgr->GetOption<float>("Atlas.Map.PackDropChance", 1.0f), 0.0f, 100.0f) * 10.0f + 0.5f);
    {
        auto parts = SplitString(StripQuotes(sConfigMgr->GetOption<std::string>("Atlas.Map.DropTierShift",
            "45,45,9,1")), ',');
        try
        {
            if (parts.size() == 4)
                for (std::size_t i = 0; i < 4; ++i)
                    _sigilRules.TierShift[i] = static_cast<uint32>(std::stoul(parts[i]));
        }
        catch (...)
        {
            LOG_ERROR("module", "Atlas: Bad Atlas.Map.DropTierShift; using 45,45,9,1.");
            _sigilRules.TierShift = { 45, 45, 9, 1 };
        }
    }
    _mapFirstClearBonus = sConfigMgr->GetOption<bool>("Atlas.Map.FirstClearBonus", true);
    _lootMaxQuantity    = std::min<uint32>(sConfigMgr->GetOption<uint32>("Atlas.Loot.MaxQuantity", 100), 1000);
    _lootMaxRarity      = std::min<uint32>(sConfigMgr->GetOption<uint32>("Atlas.Loot.MaxRarity", 150), 1000);
    _mapExcludedSources.clear();
    for (std::string const& id : SplitString(StripQuotes(sConfigMgr->GetOption<std::string>(
        "Atlas.Map.ExcludeSources", "632,658,668")), ','))
    {
        try
        {
            if (!id.empty())
                _mapExcludedSources.push_back(static_cast<uint32>(std::stoul(id)));
        }
        catch (...)
        {
            LOG_ERROR("module", "Atlas: Bad map id '{}' in Atlas.Map.ExcludeSources.", id);
        }
    }
    _mapRarityWeights = { 60, 30, 10 };
    {
        auto parts = SplitString(StripQuotes(sConfigMgr->GetOption<std::string>("Atlas.Map.RarityWeights",
            "60,30,10")), ',');
        try
        {
            if (parts.size() == 3)
                _mapRarityWeights = { static_cast<uint32>(std::stoul(parts[0])),
                    static_cast<uint32>(std::stoul(parts[1])), static_cast<uint32>(std::stoul(parts[2])) };
        }
        catch (...)
        {
            LOG_ERROR("module", "Atlas: Bad Atlas.Map.RarityWeights; using 60,30,10.");
        }
    }
    _eliteChance     = sConfigMgr->GetOption<uint32>("Atlas.Dungeon.EliteChance",    20);
    _aggroRadius     = sConfigMgr->GetOption<float> ("Atlas.Dungeon.AggroRadius",    15.0f);
    _rareSpawnChance = sConfigMgr->GetOption<uint32>("Atlas.Dungeon.RareSpawnChance", 5);
    _recentCreatureEntries = sConfigMgr->GetOption<uint32>("Atlas.Variety.RecentEntries", 12);
    _visualScaleBeast = std::clamp(sConfigMgr->GetOption<float>("Atlas.VisualScale.Beast", 1.0f), 0.55f, 1.0f);
    _visualScaleDragonkin = std::clamp(sConfigMgr->GetOption<float>("Atlas.VisualScale.Dragonkin", 0.85f), 0.55f, 1.0f);
    _visualScaleDemon = std::clamp(sConfigMgr->GetOption<float>("Atlas.VisualScale.Demon", 0.95f), 0.55f, 1.0f);
    _visualScaleElemental = std::clamp(sConfigMgr->GetOption<float>("Atlas.VisualScale.Elemental", 0.90f), 0.55f, 1.0f);
    _visualScaleGiant = std::clamp(sConfigMgr->GetOption<float>("Atlas.VisualScale.Giant", 0.70f), 0.55f, 1.0f);
    _visualScaleUndead = std::clamp(sConfigMgr->GetOption<float>("Atlas.VisualScale.Undead", 1.0f), 0.55f, 1.0f);
    _visualScaleHumanoid = std::clamp(sConfigMgr->GetOption<float>("Atlas.VisualScale.Humanoid", 1.0f), 0.55f, 1.0f);
    _visualScaleMechanical = std::clamp(sConfigMgr->GetOption<float>("Atlas.VisualScale.Mechanical", 0.90f), 0.55f, 1.0f);

    // Timers
    _cooldownMinutes   = sConfigMgr->GetOption<uint32>("Atlas.Cooldown.Minutes",     5);
    _timeLimitEnabled  = sConfigMgr->GetOption<bool>  ("Atlas.TimeLimit.Enable",     false);
    _timeLimitMinutes  = sConfigMgr->GetOption<uint32>("Atlas.TimeLimit.Minutes",    30);
    _maxConcurrentRuns = sConfigMgr->GetOption<uint32>("Atlas.MaxConcurrentRuns",    20);

    // Death
    _respawnAtStart = sConfigMgr->GetOption<bool>  ("Atlas.Death.RespawnAtStart",   true);
    _maxWipes       = sConfigMgr->GetOption<uint32>("Atlas.Death.MaxWipes",         0);

    // Completion
    _completionTeleportDelay = sConfigMgr->GetOption<uint32>("Atlas.Completion.TeleportDelay", 30);
    _announceCompletion      = sConfigMgr->GetOption<bool>  ("Atlas.Completion.Announcement",  true);

    // Roguelike
    _roguelikeEnabled         = sConfigMgr->GetOption<bool>  ("Atlas.Roguelike.Enable",            true);
    _roguelikeTransitionDelay = sConfigMgr->GetOption<uint32>("Atlas.Roguelike.TransitionDelay",   30);
    _roguelikeHpScaling       = sConfigMgr->GetOption<float> ("Atlas.Roguelike.HpScalingPerTier",  0.10f);
    _roguelikeDmgScaling      = sConfigMgr->GetOption<float> ("Atlas.Roguelike.DmgScalingPerTier", 0.08f);
    _roguelikeArmorScaling    = sConfigMgr->GetOption<float> ("Atlas.Roguelike.ArmorScalingPerTier", 0.05f);
    _roguelikeExpThreshold    = sConfigMgr->GetOption<uint32>("Atlas.Roguelike.ExponentialThreshold", 5);
    _roguelikeExpFactor       = sConfigMgr->GetOption<float> ("Atlas.Roguelike.ExponentialFactor",   1.15f);
    _roguelikeAffixStartTier  = sConfigMgr->GetOption<uint32>("Atlas.Roguelike.AffixStartTier",     3);
    _roguelikeSecondAffixTier = sConfigMgr->GetOption<uint32>("Atlas.Roguelike.SecondAffixTier",     7);
    _roguelikeThirdAffixTier  = sConfigMgr->GetOption<uint32>("Atlas.Roguelike.ThirdAffixTier",     10);
    _roguelikeMaxBuffs        = sConfigMgr->GetOption<uint32>("Atlas.Roguelike.MaxBuffs",            20);
    _roguelikeVendorEnabled = sConfigMgr->GetOption<bool>("Atlas.Roguelike.VendorEnable", true);

    // White / black lists
    _dungeonWhitelist.clear();
    _dungeonBlacklist.clear();
    ParseStringList(sConfigMgr->GetOption<std::string>("Atlas.Dungeon.Whitelist", ""), _dungeonWhitelist);
    ParseStringList(sConfigMgr->GetOption<std::string>("Atlas.Dungeon.Blacklist", ""), _dungeonBlacklist);

    // Populate sub-tables
    LoadDifficulties();
    LoadThemes();
    LoadDungeons();
    LoadRoguelikeBuffPool();

    LOG_INFO("module", "Atlas: Config loaded — {} difficulties, {} themes, {} dungeons, {} roguelike buffs.",
        _difficulties.size(), _themes.size(), _dungeons.size(), _roguelikeBuffPool.size());
}

// Load difficulty tiers from config
void AtlasConfig::LoadDifficulties()
{
    _difficulties.clear();

    for (uint32 i = 1; i <= MAX_DIFFICULTIES; ++i)
    {
        std::string val = sConfigMgr->GetOption<std::string>(
            "Atlas.Difficulty." + std::to_string(i), "");
        if (val.empty())
            break;

        auto parts = SplitString(StripQuotes(val), ',');
        if (parts.size() < 7) continue;

        DifficultyTier t;
        t.Id = i;
        t.Name = parts[0];
        try {
            t.MinLevel         = static_cast<uint8>(std::stoi(parts[1]));
            t.MaxLevel         = static_cast<uint8>(std::stoi(parts[2]));
            t.HealthMultiplier = std::stof(parts[3]);
            t.DamageMultiplier = std::stof(parts[4]);
            t.RewardMultiplier = std::stof(parts[5]);
            t.MobCountMultiplier = std::stof(parts[6]);
        } catch (...) {
            LOG_ERROR("module", "Atlas: Bad difficulty entry #{}", i);
            continue;
        }
        _difficulties.push_back(t);
    }

    if (_difficulties.empty())
    {
        DifficultyTier def;
        def.Id = 1; def.Name = "Normal";
        _difficulties.push_back(def);
        LOG_WARN("module", "Atlas: No difficulties configured, using default.");
    }
}

// Load themes from config
void AtlasConfig::LoadThemes()
{
    _themes.clear();

    for (uint32 i = 1; i <= MAX_THEMES; ++i)
    {
        std::string val = sConfigMgr->GetOption<std::string>(
            "Atlas.Theme." + std::to_string(i), "");
        if (val.empty())
            break;

        auto parts = SplitString(StripQuotes(val), ',');
        if (parts.size() < 2) continue;

        Theme theme;
        theme.Id   = i;
        theme.Name = parts[0];
        theme.SortOrder = 100 + static_cast<int32>(i);

        for (size_t j = 1; j < parts.size(); ++j)
        {
            try {
                theme.CreatureTypes.push_back(
                    static_cast<uint32>(std::stoi(parts[j])));
            } catch (...) { /* skip bad token */ }
        }
        _themes.push_back(theme);
    }

    if (_themes.empty())
    {
        Theme def;
        def.Id = 1; def.Name = "Random"; def.CreatureTypes.push_back(uint32(-1));
        _themes.push_back(def);
        LOG_WARN("module", "Atlas: No themes configured, using Random.");
    }

    _configThemes = _themes;
    SetDatabaseThemes(_databaseThemes);
}

void AtlasConfig::SetDatabaseThemes(std::vector<Theme> themes)
{
    _databaseThemes = std::move(themes);
    _themes = _configThemes;
    for (Theme const& t : _databaseThemes)
    {
        // A database theme replaces a config theme with the same ID.
        std::erase_if(_themes, [&t](Theme const& c) { return c.Id == t.Id; });
        _themes.push_back(t);
    }
    std::stable_sort(_themes.begin(), _themes.end(), [](Theme const& a, Theme const& b)
    {
        return a.SortOrder != b.SortOrder ? a.SortOrder < b.SortOrder : a.Id < b.Id;
    });
}

    // hard-coded list of WotLK 5-man instances with level ranges
void AtlasConfig::LoadDungeons()
{
    _dungeons.clear();

    struct Def { uint32 map; char const* name; uint8 lo; uint8 hi; };

    static const Def kDungeons[] =
    {
        // Classic
        { 389, "Ragefire Chasm",       13, 20 },
        {  36, "Deadmines",            15, 25 },
        {  33, "Shadowfang Keep",      18, 28 },
        {  34, "The Stockade",         20, 30 },
        {  43, "Wailing Caverns",      15, 28 },
        {  48, "Blackfathom Deeps",    20, 32 },
        {  47, "Razorfen Kraul",       25, 35 },
        {  90, "Gnomeregan",           25, 38 },
        { 129, "Razorfen Downs",       35, 45 },
        { 189, "Scarlet Monastery",    30, 45 },
        {  70, "Uldaman",              38, 50 },
        { 209, "Zul'Farrak",           42, 52 },
        { 349, "Maraudon",             40, 52 },
        { 109, "Sunken Temple",        45, 55 },
        { 230, "Blackrock Depths",     48, 60 },
        { 229, "Blackrock Spire",      52, 60 },
        { 289, "Scholomance",          55, 60 },
        { 329, "Stratholme",           55, 60 },
        // TBC
        { 543, "Hellfire Ramparts",    58, 70 },
        { 542, "Blood Furnace",        59, 70 },
        { 547, "Slave Pens",           60, 70 },
        { 546, "Underbog",             61, 70 },
        { 557, "Mana-Tombs",           62, 70 },
        { 558, "Auchenai Crypts",      63, 70 },
        { 556, "Sethekk Halls",        65, 70 },
        { 555, "Shadow Labyrinth",     68, 70 },
        { 540, "Shattered Halls",      68, 70 },
        { 553, "Botanica",             68, 70 },
        { 554, "Mechanar",             68, 70 },
        { 552, "Arcatraz",             68, 70 },
        // WotLK
        { 574, "Utgarde Keep",         68, 80 },
        { 576, "The Nexus",            69, 80 },
        { 601, "Azjol-Nerub",          70, 80 },
        { 619, "Ahn'kahet",            71, 80 },
        { 600, "Drak'Tharon Keep",     72, 80 },
        { 608, "Violet Hold",          73, 80 },
        { 604, "Gundrak",              74, 80 },
        { 599, "Halls of Stone",       75, 80 },
        { 602, "Halls of Lightning",   77, 80 },
        // The Oculus (578) is left out: its platforms are reachable only on
        // drake vehicles, which an emptied instance does not provide.
        { 575, "Utgarde Pinnacle",     78, 80 },
        { 595, "Culling of Stratholme",78, 80 },
        { 632, "Forge of Souls",       79, 80 },
        { 658, "Pit of Saron",         79, 80 },
        { 668, "Halls of Reflection",  79, 80 },
    };

    for (auto const& d : kDungeons)
    {
        if (!IsDungeonAllowed(d.map))
            continue;

        DungeonInfo info;
        info.MapId       = d.map;
        info.Name        = d.name;
        info.MinLevel    = d.lo;
        info.MaxLevel    = d.hi;
        info.IsAvailable = true;
        _dungeons.push_back(info);
    }
}

// Utility
void AtlasConfig::ParseStringList(std::string const& str, std::unordered_set<uint32>& outSet)
{
    if (str.empty()) return;
    for (auto const& tok : SplitString(str, ','))
    {
        try { outSet.insert(static_cast<uint32>(std::stoul(tok))); }
        catch (...) { /* skip */ }
    }
}

DifficultyTier const* AtlasConfig::GetDifficulty(uint32 id) const
{
    for (auto const& d : _difficulties)
        if (d.Id == id) return &d;
    return nullptr;
}

std::vector<DifficultyTier const*> AtlasConfig::GetDifficultiesForLevel(uint8 level) const
{
    std::vector<DifficultyTier const*> out;
    for (auto const& d : _difficulties)
        if (d.IsValidForLevel(level))
            out.push_back(&d);
    return out;
}

Theme const* AtlasConfig::GetTheme(uint32 id) const
{
    for (auto const& t : _themes)
        if (t.Id == id) return &t;
    return nullptr;
}

DungeonInfo const* AtlasConfig::GetDungeon(uint32 mapId) const
{
    for (auto const& d : _dungeons)
        if (d.MapId == mapId) return &d;
    return nullptr;
}

std::vector<DungeonInfo const*> AtlasConfig::GetDungeonsForLevel(uint8 minLevel, uint8 maxLevel) const
{
    std::vector<DungeonInfo const*> out;
    for (auto const& d : _dungeons)
        if (d.MaxLevel >= minLevel && d.MinLevel <= maxLevel && d.IsAvailable)
            out.push_back(&d);
    return out;
}

uint8 AtlasConfig::GetTierEntryLevel(DifficultyTier const& tier) const
{
    uint8 entry = 255;
    for (DungeonInfo const* d : GetDungeonsForLevel(tier.MinLevel, tier.MaxLevel))
        entry = std::min(entry, std::max(tier.MinLevel, d->MinLevel));
    return entry;
}

bool AtlasConfig::IsDungeonAllowed(uint32 mapId) const
{
    if (_dungeonBlacklist.count(mapId)) return false;
    if (!_dungeonWhitelist.empty() && !_dungeonWhitelist.count(mapId)) return false;
    return true;
}

    // sequential entries from Atlas
void AtlasConfig::LoadRoguelikeBuffPool()
{
    _roguelikeBuffPool.clear();

    for (uint32 i = 1; i <= MAX_ROGUELIKE_BUFFS; ++i)
    {
        // Optional keys: don't log "missing property" for each absent slot.
        std::string val = sConfigMgr->GetOption<std::string>(
            "Atlas.Roguelike.Buff." + std::to_string(i), "", false);
        if (val.empty())
            break;

        // Strip surrounding quotes
        std::string stripped = val;
        if (!stripped.empty() && stripped.front() == '"') stripped.erase(stripped.begin());
        if (!stripped.empty() && stripped.back()  == '"') stripped.pop_back();

        // Split by comma
        std::stringstream ss(stripped);
        std::string token;
        std::vector<std::string> parts;
        while (std::getline(ss, token, ','))
        {
            size_t s = token.find_first_not_of(" \t");
            size_t e = token.find_last_not_of(" \t");
            if (s != std::string::npos && e != std::string::npos)
                parts.push_back(token.substr(s, e - s + 1));
        }

        if (parts.size() < 3) continue;

        RoguelikeBuff b;
        b.Id = i;
        try {
            b.SpellId = static_cast<uint32>(std::stoul(parts[0]));
            b.Name    = parts[1];
            b.Weight  = static_cast<uint32>(std::stoul(parts[2]));
        } catch (...) {
            LOG_ERROR("module", "Atlas: Bad roguelike buff entry #{}", i);
            continue;
        }
        _roguelikeBuffPool.push_back(b);
    }

    // Default pool if none configured
    if (_roguelikeBuffPool.empty())
    {
        LOG_INFO("module", "Atlas: No roguelike buffs configured — using defaults.");

        // World buffs (Classic — stack with everything, universally applicable)
        _roguelikeBuffPool.push_back({1, 15366, "Songflower Serenade",   100});  // +15 stats, +5% crit
        _roguelikeBuffPool.push_back({2, 22888, "Rallying Cry",          80});   // +140 AP, +10% spell crit
        _roguelikeBuffPool.push_back({3, 24425, "Spirit of Zandalar",    80});   // +15% move speed, +10% stats
        _roguelikeBuffPool.push_back({4, 16609, "Warchief's Blessing",   80});   // +300 HP, haste, MP5
        _roguelikeBuffPool.push_back({5, 23768, "Fortune of Damage",     60});   // +10% damage

        // Class buffs (work on any target via AddAura)
        _roguelikeBuffPool.push_back({6, 20217, "Blessing of Kings",     90});   // +10% all stats
        _roguelikeBuffPool.push_back({7, 48161, "Power Word: Fortitude", 100});  // +stamina
        _roguelikeBuffPool.push_back({8, 48469, "Gift of the Wild",      100});  // +stats/armor/resists
        _roguelikeBuffPool.push_back({9, 19506, "Trueshot Aura",         70});   // +10% AP
        _roguelikeBuffPool.push_back({10, 24932, "Leader of the Pack",   70});   // +5% crit
    }
}

uint32 AtlasConfig::GetMapDifficultyId() const
{
    if (_mapDifficultyId && GetDifficulty(_mapDifficultyId))
        return _mapDifficultyId;
    return _difficulties.empty() ? 0 : _difficulties.back().Id;
}

bool AtlasConfig::IsMapSourceExcluded(uint32 mapId) const
{
    return std::find(_mapExcludedSources.begin(), _mapExcludedSources.end(), mapId) != _mapExcludedSources.end();
}

} // namespace Atlas
