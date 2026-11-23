/*
 * mod-atlas — AtlasAffixes.cpp
 * The affix registry (see AtlasAffixes.h).
 */

#include "AtlasAffixes.h"
#include "AtlasConfig.h"
#include "DatabaseEnv.h"
#include "Field.h"
#include "Log.h"
#include "Player.h"
#include "QueryResult.h"
#include "SpellAuraEffects.h"
#include "SpellAuras.h"
#include "SpellMgr.h"
#include "StringFormat.h"

namespace Atlas
{

AffixMgr* AffixMgr::Instance()
{
    static AffixMgr instance;
    return &instance;
}

void AffixMgr::Load()
{
    auto defs = std::make_shared<std::vector<AffixDefinition>>();

    QueryResult result = WorldDatabase.Query(
        "SELECT id, code, name, description, handler, spell_id, param1, param2, value1, value2, value3, "
        "min_tier, max_tier, weight, exclusive_group, bot_safe, quantity_bonus, rarity_bonus "
        "FROM atlas_affix ORDER BY id");
    if (result)
    {
        do
        {
            Field* f = result->Fetch();
            AffixDefinition d;
            d.Id             = f[0].Get<uint32>();
            d.Code           = f[1].Get<std::string>();
            d.Name           = f[2].Get<std::string>();
            d.Description    = f[3].Get<std::string>();
            d.Handler        = ParseAffixHandler(f[4].Get<std::string>());
            d.SpellId        = f[5].Get<uint32>();
            d.Param1         = f[6].Get<float>();
            d.Param2         = f[7].Get<float>();
            d.Values[0]      = f[8].Get<float>();
            d.Values[1]      = f[9].Get<float>();
            d.Values[2]      = f[10].Get<float>();
            d.MinTier        = f[11].Get<uint8>();
            d.MaxTier        = f[12].Get<uint8>();
            d.Weight         = f[13].Get<uint32>();
            d.ExclusiveGroup = f[14].Get<std::string>();
            d.BotSafe        = f[15].Get<uint8>() != 0;
            d.QuantityBonus  = f[16].Get<uint32>();
            d.RarityBonus    = f[17].Get<uint32>();

            if (d.Handler == AffixHandler::Unknown)
            {
                LOG_ERROR("module", "Atlas: affix {} ({}) has an unknown handler '{}'; it will never roll.",
                    d.Id, d.Code, f[4].Get<std::string>());
                continue;
            }
            if (d.SpellId && !sSpellMgr->GetSpellInfo(d.SpellId))
            {
                LOG_ERROR("module", "Atlas: affix {} ({}) uses spell {}, which doesn't exist; it will never roll.",
                    d.Id, d.Code, d.SpellId);
                continue;
            }
            defs->push_back(std::move(d));
        } while (result->NextRow());
    }
    else
        LOG_ERROR("module", "Atlas: table atlas_affix is empty or missing (module SQL not applied?): no affixes.");

    // Affix visibility (PLAN 1.6): visual-only marker auras per affix.
    uint32 markers = 0;
    if (QueryResult rows = WorldDatabase.Query("SELECT affix_id, spell_id FROM atlas_affix_marker"))
    {
        do
        {
            Field* f = rows->Fetch();
            uint32 const affixId = f[0].Get<uint32>();
            uint32 const spellId = f[1].Get<uint32>();
            if (!sSpellMgr->GetSpellInfo(spellId))
            {
                LOG_ERROR("module", "Atlas: affix {} marker spell {} doesn't exist; no marker.", affixId, spellId);
                continue;
            }
            for (AffixDefinition& d : *defs)
                if (d.Id == affixId)
                {
                    d.MarkerSpell = spellId;
                    ++markers;
                }
        } while (rows->NextRow());
    }

    std::unordered_set<uint32> spells;
    std::unordered_set<uint32> playerAuras;
    for (AffixDefinition const& d : *defs)
    {
        if (d.SpellId)
            spells.insert(d.SpellId);
        if (d.Handler == AffixHandler::PlayerAura && d.SpellId)
            playerAuras.insert(d.SpellId);
    }

    std::unique_lock<std::shared_mutex> lock(_defsMutex);
    _defs = std::move(defs);
    _spellIds = std::move(spells);
    _playerAuraSpells = std::move(playerAuras);
    LOG_INFO("module", "Atlas: {} affixes loaded ({} with a marker).", _defs->size(), markers);
}

std::shared_ptr<std::vector<AffixDefinition> const> AffixMgr::Definitions() const
{
    std::shared_lock<std::shared_mutex> lock(_defsMutex);
    return _defs;
}

bool AffixMgr::IsAffixSpell(uint32 spellId) const
{
    std::shared_lock<std::shared_mutex> lock(_defsMutex);
    return _spellIds.count(spellId) != 0;
}

std::vector<ActiveAffix> AffixMgr::Roll(uint32 tier, uint32 count, RunRng& rng) const
{
    auto defs = Definitions();
    return RollAffixes(*defs, tier, count, sAtlasConfig->IsAffixBotSafeOnly(), rng);
}

std::vector<ActiveAffix> AffixMgr::RollRareModifiers(uint32 tier, RunRng& rng) const
{
    auto defs = Definitions();
    std::vector<AffixDefinition> creature;
    for (AffixDefinition const& d : *defs)
        if (IsCreatureAffix(d.Handler))
            creature.push_back(d);
    uint32 const count = rng.Range(2, 3);
    return RollAffixes(creature, tier, count, sAtlasConfig->IsAffixBotSafeOnly(), rng);
}

AffixEffects AffixMgr::Aggregate(std::vector<ActiveAffix> const& affixes) const
{
    auto defs = Definitions();
    return AggregateAffixes(*defs, affixes);
}

std::vector<ActiveAffix> AffixMgr::AtTier(std::vector<ActiveAffix> const& affixes, uint32 tier) const
{
    auto defs = Definitions();
    std::vector<ActiveAffix> out;
    for (ActiveAffix const& a : affixes)
        if (AffixDefinition const* def = FindAffix(*defs, a.Id))
            out.push_back({ def->Id, def->Values[BandForTier(tier)] });
    return out;
}

std::string AffixMgr::Describe(std::vector<ActiveAffix> const& affixes, bool withText) const
{
    auto defs = Definitions();
    std::string out;
    for (ActiveAffix const& a : affixes)
    {
        AffixDefinition const* def = FindAffix(*defs, a.Id);
        if (!def)
            continue;
        if (!out.empty())
            out += ", ";
        out += "|cFFFF8800" + def->Name + "|r";
        if (withText)
            out += " (" + FormatAffix(*def, a.Value) + ")";
    }
    return out;
}

std::string AffixMgr::Codes(std::vector<ActiveAffix> const& affixes) const
{
    auto defs = Definitions();
    std::string out;
    for (ActiveAffix const& a : affixes)
    {
        AffixDefinition const* def = FindAffix(*defs, a.Id);
        if (!out.empty())
            out += ',';
        out += def ? def->Code : std::to_string(a.Id);
    }
    return out;
}

std::vector<ActiveAffix> AffixMgr::Parse(std::string_view codes, uint32 tier, std::string& unknown) const
{
    auto defs = Definitions();
    std::vector<ActiveAffix> out;
    std::size_t start = 0;
    while (start < codes.size())
    {
        std::size_t end = codes.find_first_of(", ", start);
        if (end == std::string_view::npos)
            end = codes.size();
        std::string_view const code = codes.substr(start, end - start);
        if (!code.empty())
        {
            if (AffixDefinition const* def = FindAffixByCode(*defs, code))
                out.push_back({ def->Id, def->Values[BandForTier(tier)] });
            else
            {
                if (!unknown.empty())
                    unknown += ' ';
                unknown += std::string(code);
            }
        }
        start = end + 1;
    }
    return out;
}

void AffixMgr::SetPending(ObjectGuid guid, std::vector<ActiveAffix> affixes)
{
    std::lock_guard<std::mutex> lock(_pendingMutex);
    if (affixes.empty())
        _pending.erase(guid);
    else
        _pending[guid] = std::move(affixes);
}

std::vector<ActiveAffix> AffixMgr::TakePending(ObjectGuid guid)
{
    std::lock_guard<std::mutex> lock(_pendingMutex);
    auto it = _pending.find(guid);
    if (it == _pending.end())
        return {};
    std::vector<ActiveAffix> affixes = std::move(it->second);
    _pending.erase(it);
    return affixes;
}

void AffixMgr::ApplyPlayerAuras(Player* player, AffixEffects const& effects) const
{
    if (!player || !player->IsAlive())
        return;

    for (AffixPlayerAura const& pa : effects.PlayerAuras)
    {
        Aura* aura = player->GetAura(pa.SpellId);
        if (!aura)
            aura = player->AddAura(pa.SpellId, player);
        if (!aura)
            continue;

        // Every effect of the module's affix spells carries the same amount
        // (of the Throne: dodge and parry).
        for (uint8 i = 0; i < MAX_SPELL_EFFECTS; ++i)
            if (AuraEffect* effect = aura->GetEffect(i))
                if (effect->GetAmount() != pa.Amount)
                    effect->ChangeAmount(pa.Amount);
    }
}

void AffixMgr::RemovePlayerAuras(Player* player) const
{
    if (!player)
        return;

    std::unordered_set<uint32> spells;
    {
        std::shared_lock<std::shared_mutex> lock(_defsMutex);
        spells = _playerAuraSpells;
    }
    for (uint32 spellId : spells)
        if (player->HasAura(spellId))
            player->RemoveAurasDueToSpell(spellId);
}

} // namespace Atlas
