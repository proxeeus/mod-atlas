/*
 * mod-atlas — AtlasAffixes.h
 * The affix registry (world table atlas_affix): rolling, aggregation,
 * descriptions, GM-forced affixes and the players' affix auras. The data and
 * math are in AtlasAffix.h; the effects on creatures live with the run's
 * creatures (AtlasMgr, AtlasScaling.cpp).
 */

#ifndef ATLAS_AFFIXES_H
#define ATLAS_AFFIXES_H

#include "AtlasAffix.h"
#include "ObjectGuid.h"

#include <memory>
#include <mutex>
#include <shared_mutex>
#include <unordered_map>
#include <unordered_set>

class Player;

namespace Atlas
{

class AffixMgr
{
public:
    static AffixMgr* Instance();

    // Reads atlas_affix; also used by `.atlas reload`.
    void Load();

    // A snapshot of the definitions; stays valid across a reload.
    std::shared_ptr<std::vector<AffixDefinition> const> Definitions() const;

    // True for every spell an affix casts or applies: the damage hooks leave
    // those alone (their amounts are computed when they are cast).
    bool IsAffixSpell(uint32 spellId) const;

    std::vector<ActiveAffix> Roll(uint32 tier, uint32 count, RunRng& rng) const;
    // A rare's own modifiers: 2 or 3 creature affixes at the run's tier.
    std::vector<ActiveAffix> RollRareModifiers(uint32 tier, RunRng& rng) const;
    AffixEffects Aggregate(std::vector<ActiveAffix> const& affixes) const;
    // Re-bands GM-forced affixes to the tier they now run at.
    std::vector<ActiveAffix> AtTier(std::vector<ActiveAffix> const& affixes, uint32 tier) const;

    // "Brutal (Monsters deal 25% increased physical damage), of Grief (...)".
    std::string Describe(std::vector<ActiveAffix> const& affixes, bool withText) const;
    // "M01,P01", for the run log.
    std::string Codes(std::vector<ActiveAffix> const& affixes) const;
    // Codes ("M01 P02" or "M01,P02") to affixes at a tier; unknown codes are reported.
    std::vector<ActiveAffix> Parse(std::string_view codes, uint32 tier, std::string& unknown) const;

    // GM testing (`.atlas affix next`): the next run of this character gets these.
    void SetPending(ObjectGuid guid, std::vector<ActiveAffix> affixes);
    std::vector<ActiveAffix> TakePending(ObjectGuid guid);

    // Players' affix auras (module passive spells): applied while they are in
    // the run, with the run's amounts; removed when they leave.
    void ApplyPlayerAuras(Player* player, AffixEffects const& effects) const;
    void RemovePlayerAuras(Player* player) const;

private:
    std::shared_ptr<std::vector<AffixDefinition> const> _defs = std::make_shared<std::vector<AffixDefinition>>();
    std::unordered_set<uint32> _spellIds;
    std::unordered_set<uint32> _playerAuraSpells;
    mutable std::shared_mutex _defsMutex;

    std::unordered_map<ObjectGuid, std::vector<ActiveAffix>> _pending;
    std::mutex _pendingMutex;
};

} // namespace Atlas

#define sAtlasAffixes Atlas::AffixMgr::Instance()

#endif
