/*
 * mod-atlas — AtlasCloneMaps.cpp
 * AtlasMgr: clone maps (PLAN stage 2, AtlasClones.h). The registry comes from
 * atlas_map_source (world DB, data/sql/db-world/base/atlas_clones.sql); every
 * row is checked against what the server actually loaded before a run may
 * open there. The per-account opt-in says the account's client has the Atlas
 * patch: an unpatched client sent to a clone never leaves its loading screen.
 */

#include "AtlasMgr.h"
#include "AtlasClones.h"
#include "AtlasConfig.h"
#include "DBCStores.h"
#include "DatabaseEnv.h"
#include "Field.h"
#include "Log.h"
#include "ObjectMgr.h"
#include "Player.h"
#include "QueryResult.h"
#include "StringFormat.h"
#include "World.h"
#include "WorldSession.h"
#include <algorithm>
#include <filesystem>

namespace Atlas
{

namespace
{
    bool WorldTableExists(char const* table)
    {
        return WorldDatabase.Query("SELECT 1 FROM information_schema.tables WHERE table_schema = DATABASE() "
            "AND table_name = '{}'", table) != nullptr;
    }

    bool CharacterTableExists(char const* table)
    {
        return CharacterDatabase.Query("SELECT 1 FROM information_schema.tables WHERE table_schema = DATABASE() "
            "AND table_name = '{}'", table) != nullptr;
    }

    // Why a clone row can't be used, or "" when it can. The server must know
    // the clone as a dungeon like its source, with an instance template and a
    // Normal difficulty, and see the source's collision and navmesh files
    // under the clone's ID (tools/clones/link_data.sh); without them creatures
    // and players would fall through the floor.
    std::string CheckClone(CloneMap const& c)
    {
        if (!IsReservedCloneId(c.CloneId))
            return Acore::StringFormat("map {} is outside the reserved clone IDs {}-{}", c.CloneId,
                CLONE_MAP_ID_MIN, CLONE_MAP_ID_MAX);

        auto const& dungeons = sAtlasConfig->GetDungeons();
        if (std::none_of(dungeons.begin(), dungeons.end(),
                [&c](DungeonInfo const& d) { return d.MapId == c.SourceId; }))
            return Acore::StringFormat("source map {} is not an Atlas dungeon", c.SourceId);

        MapEntry const* clone = sMapStore.LookupEntry(c.CloneId);
        MapEntry const* source = sMapStore.LookupEntry(c.SourceId);
        if (!clone)
            return "no map_dbc row";
        if (!source || !clone->IsDungeon() || clone->map_type != source->map_type
            || clone->Expansion() != source->Expansion())
            return Acore::StringFormat("its map_dbc row does not match source map {}", c.SourceId);
        if (!sObjectMgr->GetInstanceTemplate(c.CloneId))
            return "no instance_template row";
        if (!GetMapDifficultyData(c.CloneId, REGULAR_DIFFICULTY))
            return "no Normal mapdifficulty_dbc row";

        std::string const& data = sWorld->GetDataPath();
        for (char const* pattern : { "{}vmaps/{:03}.vmtree", "{}mmaps/{:03}.mmap" })
        {
            std::error_code ec;
            std::string const sourceFile = Acore::StringFormat(pattern, data, c.SourceId);
            std::string const cloneFile = Acore::StringFormat(pattern, data, c.CloneId);
            if (std::filesystem::exists(sourceFile, ec) && !std::filesystem::exists(cloneFile, ec))
                return Acore::StringFormat("{} is missing (run tools/clones/link_data.sh --apply)", cloneFile);
        }
        return {};
    }
}

void AtlasMgr::LoadCloneMaps()
{
    std::vector<CloneMap> usable;
    if (WorldTableExists("atlas_map_source"))
    {
        if (QueryResult result = WorldDatabase.Query(
                "SELECT clone_map, source_map, name FROM atlas_map_source WHERE enabled = 1 ORDER BY clone_map"))
        {
            do
            {
                Field* f = result->Fetch();
                CloneMap c;
                c.CloneId  = f[0].Get<uint32>();
                c.SourceId = f[1].Get<uint32>();
                c.Name     = f[2].Get<std::string>();
                std::string const why = CheckClone(c);
                if (!why.empty())
                {
                    LOG_ERROR("module", "Atlas: clone map {} ({}, source {}) is not usable: {}.", c.CloneId,
                        c.Name, c.SourceId, why);
                    continue;
                }
                usable.push_back(std::move(c));
            } while (result->NextRow());
        }
    }

    std::vector<CloneMap> const kept = sAtlasConfig->SetClones(usable);
    for (CloneMap const& c : kept)
        LOG_INFO("module", "Atlas: clone map {} ({}) echoes map {}.", c.CloneId, c.Name, c.SourceId);
    if (kept.size() != usable.size())
        LOG_ERROR("module", "Atlas: {} clone row(s) dropped: a second clone of the same source, or a clone of a "
            "clone.", usable.size() - kept.size());

    if (sAtlasConfig->IsClonesEnabled())
        LOG_INFO("module", "Atlas: clone maps ON, {} usable; runs open there for accounts that opted in.",
            kept.size());
    else
        LOG_INFO("module", "Atlas: clone maps off (Atlas.Clones.Enable = 0); {} usable clone(s) wait.", kept.size());
}

void AtlasMgr::LoadCloneOptIns()
{
    std::unordered_set<uint32> accounts;
    if (CharacterTableExists("atlas_clone_optin"))
        if (QueryResult result = CharacterDatabase.Query("SELECT account FROM atlas_clone_optin"))
            do
                accounts.insert(result->Fetch()[0].Get<uint32>());
            while (result->NextRow());

    std::lock_guard<std::mutex> lock(_cloneMutex);
    _cloneOptIn = std::move(accounts);
}

bool AtlasMgr::IsCloneOptedIn(uint32 account) const
{
    std::lock_guard<std::mutex> lock(_cloneMutex);
    return _cloneOptIn.count(account) != 0;
}

void AtlasMgr::SetCloneOptIn(uint32 account, bool on)
{
    {
        std::lock_guard<std::mutex> lock(_cloneMutex);
        if (on)
            _cloneOptIn.insert(account);
        else
            _cloneOptIn.erase(account);
    }
    if (on)
        CharacterDatabase.Execute("REPLACE INTO atlas_clone_optin (account, opted_at) VALUES ({}, UNIX_TIMESTAMP())",
            account);
    else
        CharacterDatabase.Execute("DELETE FROM atlas_clone_optin WHERE account = {}", account);
}

uint32 AtlasMgr::ChooseMapRunMap(std::vector<Player*> const& party, uint32 sourceMap) const
{
    uint32 real = 0;
    uint32 optedIn = 0;
    for (Player const* p : party)
    {
        if (!p || !IsRealPlayer(p))
            continue;
        ++real;
        if (IsCloneOptedIn(p->GetSession()->GetAccountId()))
            ++optedIn;
    }
    return ChooseRunMap(sAtlasConfig->IsClonesEnabled(), sourceMap, sAtlasConfig->GetClones().CloneOf(sourceMap),
        real, optedIn);
}

} // namespace Atlas
