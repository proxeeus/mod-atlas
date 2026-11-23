/*
 * mod-atlas — AtlasMapItem.h
 * Echo Sigils (map items, DESIGN §11, PLAN 1.4) as pure data and math, so the
 * entries, rarities, drops and the stored affixes can be unit-tested.
 *
 * One item entry per tier and rarity (90001-90016 normal, 90101-90116 magic,
 * 90201-90216 rare), so tier and rarity show without any client change. What
 * makes a sigil unique (its dungeon, theme, seed and affixes) is stored in the
 * characters table atlas_map_item, keyed by the item's guid (decision D2).
 */

#ifndef ATLAS_MAP_ITEM_H
#define ATLAS_MAP_ITEM_H

#include "AtlasAffix.h"
#include "Define.h"
#include "RunRng.h"

#include <algorithm>
#include <array>
#include <charconv>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace Atlas
{

constexpr uint32 MAP_ITEM_BASE  = 90000;
constexpr uint8  MAP_MAX_TIER   = 16;

// Gameobjects (atlas_maps.sql).
constexpr uint32 GO_ATLAS_LENS      = 900000;   // the Timeways Lens, Dalaran
constexpr uint32 GO_ATLAS_RIFT      = 900001;   // Rift in Time, beside the Lens
constexpr uint32 GO_ATLAS_RIFT_HOME = 900002;   // Rift to Dalaran, inside a run
constexpr uint32 GO_ATLAS_CACHE     = 900003;   // Cache of the Keepers

enum class MapRarity : uint8
{
    Normal = 0,
    Magic,
    Rare,
    Count
};

inline char const* MapRarityName(MapRarity rarity)
{
    switch (rarity)
    {
        case MapRarity::Normal: return "normal";
        case MapRarity::Magic:  return "magic";
        case MapRarity::Rare:   return "rare";
        default:                return "?";
    }
}

inline MapRarity ParseMapRarity(std::string_view name)
{
    if (name == "normal")
        return MapRarity::Normal;
    if (name == "magic")
        return MapRarity::Magic;
    if (name == "rare")
        return MapRarity::Rare;
    return MapRarity::Count;
}

inline uint32 MapItemEntry(uint8 tier, MapRarity rarity)
{
    return MAP_ITEM_BASE + static_cast<uint32>(rarity) * 100 + std::clamp<uint8>(tier, 1, MAP_MAX_TIER);
}

// False for anything that isn't a sigil entry.
inline bool DecodeMapItemEntry(uint32 entry, uint8& tier, MapRarity& rarity)
{
    if (entry <= MAP_ITEM_BASE)
        return false;
    uint32 const offset = entry - MAP_ITEM_BASE;
    uint32 const r = offset / 100;
    uint32 const t = offset % 100;
    if (r >= static_cast<uint32>(MapRarity::Count) || t < 1 || t > MAP_MAX_TIER)
        return false;
    tier = static_cast<uint8>(t);
    rarity = static_cast<MapRarity>(r);
    return true;
}

inline bool IsMapItemEntry(uint32 entry)
{
    uint8 tier = 0;
    MapRarity rarity = MapRarity::Normal;
    return DecodeMapItemEntry(entry, tier, rarity);
}

// How many affixes a sigil of a rarity carries (DESIGN §10, Rarity).
inline std::pair<uint32, uint32> MapAffixCountRange(MapRarity rarity)
{
    switch (rarity)
    {
        case MapRarity::Magic: return { 1, 2 };
        case MapRarity::Rare:  return { 3, 4 };
        default:               return { 0, 0 };
    }
}

// Weights in normal, magic, rare order.
inline MapRarity RollMapRarity(std::array<uint32, 3> const& weights, RunRng& rng)
{
    uint32 const total = weights[0] + weights[1] + weights[2];
    if (!total)
        return MapRarity::Normal;
    uint32 const draw = rng.Range(1, total);
    if (draw <= weights[0])
        return MapRarity::Normal;
    if (draw <= weights[0] + weights[1])
        return MapRarity::Magic;
    return MapRarity::Rare;
}

// Dungeons by tier band (stage 1 runs on the stock dungeons): Classic for
// tiers 1-5, The Burning Crusade for 6-10, Wrath of the Lich King from 11.
inline uint8 MapSourceBand(uint8 tier)
{
    return tier <= 5 ? 0 : tier <= 10 ? 1 : 2;
}

// "id:value;id:value" for atlas_map_item.affixes.
inline std::string SerializeMapAffixes(std::vector<ActiveAffix> const& affixes)
{
    std::string out;
    for (ActiveAffix const& a : affixes)
    {
        char buffer[48];
        std::snprintf(buffer, sizeof(buffer), "%s%u:%.2f", out.empty() ? "" : ";", a.Id, a.Value);
        out += buffer;
    }
    return out;
}

inline std::vector<ActiveAffix> ParseMapAffixes(std::string_view text)
{
    std::vector<ActiveAffix> out;
    std::size_t start = 0;
    while (start < text.size())
    {
        std::size_t end = text.find(';', start);
        if (end == std::string_view::npos)
            end = text.size();
        std::string_view const pair = text.substr(start, end - start);
        std::size_t const colon = pair.find(':');
        if (colon != std::string_view::npos)
        {
            uint32 id = 0;
            auto const r = std::from_chars(pair.data(), pair.data() + colon, id);
            std::string const value(pair.substr(colon + 1));
            if (r.ec == std::errc{} && id && !value.empty())
                out.push_back({ id, std::strtof(value.c_str(), nullptr) });
        }
        start = end + 1;
    }
    return out;
}

// What a sigil holds beyond its entry.
struct MapItemData
{
    uint32    ItemGuid  = 0;   // item_instance.guid
    uint32    OwnerGuid = 0;   // the character it was made for
    uint8     Tier      = 1;
    MapRarity Rarity    = MapRarity::Normal;
    uint32    SourceMap = 0;   // the dungeon it opens
    uint32    ThemeId   = 0;
    uint64    Seed      = 0;   // the run's layout (C8): same sigil, same echo
    std::vector<ActiveAffix> Affixes;
};

} // namespace Atlas

#endif
