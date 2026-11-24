/*
 * mod-atlas — AtlasClones.h
 * Clone maps (PLAN stage 2): a stock dungeon's terrain under a new map ID
 * (900-949) with no stock spawns, instance script or gates. The client knows
 * a clone only from the Atlas patch (Data\patch-T.MPQ); an unpatched client
 * sent there stays on its loading screen for good, so a run opens in a clone
 * only when every real player of the party opted in.
 *
 * Pure: no AC dependencies beyond Define.h, unit-tested.
 */

#ifndef ATLAS_CLONES_H
#define ATLAS_CLONES_H

#include "Define.h"

#include <string>
#include <unordered_map>
#include <vector>

namespace Atlas
{

constexpr uint32 CLONE_MAP_ID_MIN = 900;
constexpr uint32 CLONE_MAP_ID_MAX = 949;
// Generated-map pool (DESIGN §6.2): empty maps that kit pieces are placed in.
constexpr uint32 POOL_MAP_ID_MIN  = 950;
constexpr uint32 POOL_MAP_ID_MAX  = 999;

constexpr bool IsReservedCloneId(uint32 mapId)
{
    return mapId >= CLONE_MAP_ID_MIN && mapId <= CLONE_MAP_ID_MAX;
}

struct CloneMap
{
    uint32      CloneId  = 0;
    uint32      SourceId = 0;
    std::string Name;
};

// The usable clones (atlas_map_source rows that passed the startup checks).
// One clone per source; a later row for the same source is ignored.
class CloneRegistry
{
public:
    // Returns the clones kept (rows outside the reserved IDs, on a source
    // that already has one, or cloning a clone are dropped).
    std::vector<CloneMap> Set(std::vector<CloneMap> const& clones)
    {
        _bySource.clear();
        _byClone.clear();
        std::vector<CloneMap> kept;
        for (CloneMap const& c : clones)
        {
            if (!IsReservedCloneId(c.CloneId) || IsReservedCloneId(c.SourceId) || c.SourceId == 0)
                continue;
            if (_bySource.count(c.SourceId) || _byClone.count(c.CloneId))
                continue;
            _bySource[c.SourceId] = c.CloneId;
            _byClone[c.CloneId] = c;
            kept.push_back(c);
        }
        return kept;
    }

    bool IsClone(uint32 mapId) const { return _byClone.count(mapId) != 0; }

    // The stock map a clone reuses; any other map is its own source.
    uint32 SourceOf(uint32 mapId) const
    {
        auto itr = _byClone.find(mapId);
        return itr != _byClone.end() ? itr->second.SourceId : mapId;
    }

    // The clone of a stock map, 0 when it has none.
    uint32 CloneOf(uint32 sourceId) const
    {
        auto itr = _bySource.find(sourceId);
        return itr != _bySource.end() ? itr->second : 0;
    }

    CloneMap const* Get(uint32 cloneId) const
    {
        auto itr = _byClone.find(cloneId);
        return itr != _byClone.end() ? &itr->second : nullptr;
    }

    std::size_t Size() const { return _byClone.size(); }

private:
    std::unordered_map<uint32, uint32>   _bySource;
    std::unordered_map<uint32, CloneMap> _byClone;
};

// Where a map run on `sourceId` opens: its clone when clones are on, the
// source has a usable clone and every real player opted in (a party with no
// real player never opens a clone); the stock map otherwise.
inline uint32 ChooseRunMap(bool clonesEnabled, uint32 sourceId, uint32 cloneId, uint32 realPlayers,
    uint32 realPlayersOptedIn)
{
    if (!clonesEnabled || !cloneId || !realPlayers || realPlayersOptedIn < realPlayers)
        return sourceId;
    return cloneId;
}

} // namespace Atlas

#endif
