/*
 * mod-atlas — AtlasAffix.h
 * Affixes (DESIGN §10, AFFIXES appendix) as pure data and math, so rolling and
 * aggregation can be unit-tested. Definitions come from the world table
 * atlas_affix (AtlasAffixes.cpp); a run carries its rolled affixes, and their
 * combined effect is folded once into an AffixEffects.
 */

#ifndef ATLAS_AFFIX_H
#define ATLAS_AFFIX_H

#include "Define.h"
#include "RunRng.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <set>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace Atlas
{

// One handler per mechanism; the table's `handler` column names one of these.
enum class AffixHandler : uint8
{
    Unknown = 0,
    MonsterLife,     // monsters (not the boss) have X% more life
    BossLife,        // the boss has X% more life
    BossDamage,      // the boss deals X% more damage
    MeleeDealt,      // monster melee deals X% more damage
    ExtraHit,        // monster melee adds X% of its damage as a hit of spell_id's school
    AttackSpeed,     // monsters attack and cast X% faster
    MoveSpeed,       // monsters move X% faster
    PhysTaken,       // monsters take X% less physical damage
    SpellTaken,      // monsters take X% less spell damage
    CombatShield,    // on engaging, a spell_id absorb of X% max life
    DeathBurst,      // on death, spell_id bursts for X% of max life after param1 ms
    DeathPool,       // on death, a spell_id pool ticking X% of the creature's melee hit
    SplitOnDeath,    // elites split into X smaller copies on death
    PlayerAura,      // players carry spell_id with amount X x param1 (sign)
    GroundPeriodic,  // every param1 ms, spell_id under a random player, ticking X% of a trash hit
    PackSize,        // X% chance per anchor of one more creature
    Swarm,           // X% chance per anchor of one more non-elite creature
    EliteChance,     // +X points of elite chance
    RareChance,      // X% more chance of the rare
    BossAdds,        // the boss calls X adds at 75%, 50% and 25% life
};

inline AffixHandler ParseAffixHandler(std::string_view name)
{
    static constexpr std::pair<std::string_view, AffixHandler> names[] = {
        { "MONSTER_LIFE", AffixHandler::MonsterLife },     { "BOSS_LIFE", AffixHandler::BossLife },
        { "BOSS_DAMAGE", AffixHandler::BossDamage },       { "MELEE_DEALT", AffixHandler::MeleeDealt },
        { "EXTRA_HIT", AffixHandler::ExtraHit },           { "ATTACK_SPEED", AffixHandler::AttackSpeed },
        { "MOVE_SPEED", AffixHandler::MoveSpeed },         { "PHYS_TAKEN", AffixHandler::PhysTaken },
        { "SPELL_TAKEN", AffixHandler::SpellTaken },       { "COMBAT_SHIELD", AffixHandler::CombatShield },
        { "DEATH_BURST", AffixHandler::DeathBurst },       { "DEATH_POOL", AffixHandler::DeathPool },
        { "SPLIT_ON_DEATH", AffixHandler::SplitOnDeath },  { "PLAYER_AURA", AffixHandler::PlayerAura },
        { "GROUND_PERIODIC", AffixHandler::GroundPeriodic }, { "PACK_SIZE", AffixHandler::PackSize },
        { "SWARM", AffixHandler::Swarm },                  { "ELITE_CHANCE", AffixHandler::EliteChance },
        { "RARE_CHANCE", AffixHandler::RareChance },       { "BOSS_ADDS", AffixHandler::BossAdds },
    };
    for (auto const& [text, handler] : names)
        if (text == name)
            return handler;
    return AffixHandler::Unknown;
}

struct AffixDefinition
{
    uint32       Id = 0;
    std::string  Code;              // M01, P02... (AFFIXES appendix)
    std::string  Name;              // "Brutal", "of Grief"
    std::string  Description;       // "{}" is replaced by the rolled value
    AffixHandler Handler = AffixHandler::Unknown;
    uint32       SpellId = 0;
    float        Param1 = 0.0f;
    float        Param2 = 0.0f;
    float        Values[3] = { 0.0f, 0.0f, 0.0f };   // tier bands 1-5, 6-10, 11-16
    uint32       MinTier = 1;
    uint32       MaxTier = 16;
    uint32       Weight = 100;
    std::string  ExclusiveGroup;    // affixes of one group never roll together
    bool         BotSafe = true;
    uint32       QuantityBonus = 0;
    uint32       RarityBonus = 0;
};

// An affix on a run, with the value rolled for its tier.
struct ActiveAffix
{
    uint32 Id = 0;
    float  Value = 0.0f;
};

inline uint8 BandForTier(uint32 tier)
{
    if (tier <= 5)
        return 0;
    if (tier <= 10)
        return 1;
    return 2;
}

inline AffixDefinition const* FindAffix(std::vector<AffixDefinition> const& defs, uint32 id)
{
    for (AffixDefinition const& def : defs)
        if (def.Id == id)
            return &def;
    return nullptr;
}

inline AffixDefinition const* FindAffixByCode(std::vector<AffixDefinition> const& defs, std::string_view code)
{
    for (AffixDefinition const& def : defs)
        if (def.Code.size() == code.size()
            && std::equal(def.Code.begin(), def.Code.end(), code.begin(),
                [](char a, char b) { return std::toupper(static_cast<unsigned char>(a))
                    == std::toupper(static_cast<unsigned char>(b)); }))
            return &def;
    return nullptr;
}

// Roll `count` affixes for a run of `tier`: from the definitions eligible at
// that tier (and bot-safe ones only if asked), weighted, never two from one
// exclusive group. Deterministic for a given RNG state (C8): candidates are
// taken in the definitions' order.
inline std::vector<ActiveAffix> RollAffixes(std::vector<AffixDefinition> const& defs, uint32 tier, uint32 count,
    bool botSafeOnly, RunRng& rng)
{
    std::vector<ActiveAffix> rolled;
    std::set<std::string> groups;
    std::set<uint32> taken;

    while (rolled.size() < count)
    {
        std::vector<AffixDefinition const*> eligible;
        uint32 totalWeight = 0;
        for (AffixDefinition const& def : defs)
        {
            if (def.Handler == AffixHandler::Unknown || !def.Weight)
                continue;
            if (tier < def.MinTier || tier > def.MaxTier)
                continue;
            if (botSafeOnly && !def.BotSafe)
                continue;
            if (taken.count(def.Id) || (!def.ExclusiveGroup.empty() && groups.count(def.ExclusiveGroup)))
                continue;
            eligible.push_back(&def);
            totalWeight += def.Weight;
        }
        if (eligible.empty())
            break;

        uint32 pick = rng.Range(1, totalWeight);
        AffixDefinition const* chosen = eligible.back();
        for (AffixDefinition const* def : eligible)
        {
            if (pick <= def->Weight)
            {
                chosen = def;
                break;
            }
            pick -= def->Weight;
        }

        taken.insert(chosen->Id);
        if (!chosen->ExclusiveGroup.empty())
            groups.insert(chosen->ExclusiveGroup);
        rolled.push_back({ chosen->Id, chosen->Values[BandForTier(tier)] });
    }
    return rolled;
}

// "Monsters deal {}% increased physical damage" with 25 -> "... 25% ...".
inline std::string FormatAffix(AffixDefinition const& def, float value)
{
    std::string number;
    if (std::fabs(value - std::round(value)) < 0.05f)
        number = std::to_string(static_cast<int32>(std::lround(value)));
    else
    {
        number = std::to_string(std::lround(value * 10.0f) / 10.0f);
        number.erase(number.find_last_not_of('0') + 1);
    }
    std::string text = def.Description;
    std::size_t const at = text.find("{}");
    if (at != std::string::npos)
        text.replace(at, 2, number);
    return text;
}

// ---------------------------------------------------------------------------
// The combined effect of a run's affixes, folded once when they are set.
// ---------------------------------------------------------------------------

struct AffixExtraHit
{
    uint32 SpellId = 0;
    float  Pct     = 0.0f;
};

struct AffixPlayerAura
{
    uint32 SpellId = 0;
    int32  Amount  = 0;
};

struct AffixGround
{
    uint32 SpellId    = 0;
    uint32 IntervalMs = 0;
    float  Pct        = 0.0f;
};

struct AffixEffects
{
    float  MonsterHealth    = 1.0f;
    float  BossHealth       = 1.0f;
    float  BossDamage       = 1.0f;
    float  MeleeDealt       = 1.0f;
    float  PhysTaken        = 1.0f;
    float  SpellTaken       = 1.0f;
    float  AttackSpeedPct   = 0.0f;
    float  MoveSpeedPct     = 0.0f;
    uint32 ShieldSpell      = 0;
    float  ShieldPct        = 0.0f;
    uint32 BurstSpell       = 0;
    float  BurstPct         = 0.0f;
    uint32 BurstDelayMs     = 0;
    uint32 PoolSpell        = 0;
    float  PoolPct          = 0.0f;
    uint32 SplitCount       = 0;
    uint32 BossAddsPerWave  = 0;
    float  PackExtraPct     = 0.0f;
    float  SwarmExtraPct    = 0.0f;
    float  EliteChanceBonus = 0.0f;
    float  RareChanceMult   = 1.0f;
    uint32 QuantityBonus    = 0;
    uint32 RarityBonus      = 0;
    std::vector<AffixExtraHit>   ExtraHits;
    std::vector<AffixPlayerAura> PlayerAuras;
    std::vector<AffixGround>     Ground;
};

inline AffixEffects AggregateAffixes(std::vector<AffixDefinition> const& defs, std::vector<ActiveAffix> const& affixes)
{
    AffixEffects fx;
    for (ActiveAffix const& a : affixes)
    {
        AffixDefinition const* def = FindAffix(defs, a.Id);
        if (!def)
            continue;

        float const pct = a.Value / 100.0f;
        fx.QuantityBonus += def->QuantityBonus;
        fx.RarityBonus   += def->RarityBonus;

        switch (def->Handler)
        {
            case AffixHandler::MonsterLife:  fx.MonsterHealth *= 1.0f + pct; break;
            case AffixHandler::BossLife:     fx.BossHealth    *= 1.0f + pct; break;
            case AffixHandler::BossDamage:   fx.BossDamage    *= 1.0f + pct; break;
            case AffixHandler::MeleeDealt:   fx.MeleeDealt    *= 1.0f + pct; break;
            case AffixHandler::PhysTaken:    fx.PhysTaken     *= std::max(0.0f, 1.0f - pct); break;
            case AffixHandler::SpellTaken:   fx.SpellTaken    *= std::max(0.0f, 1.0f - pct); break;
            case AffixHandler::AttackSpeed:  fx.AttackSpeedPct += a.Value; break;
            case AffixHandler::MoveSpeed:    fx.MoveSpeedPct   += a.Value; break;
            case AffixHandler::ExtraHit:     fx.ExtraHits.push_back({ def->SpellId, pct }); break;
            case AffixHandler::CombatShield: fx.ShieldSpell = def->SpellId; fx.ShieldPct += pct; break;
            case AffixHandler::DeathBurst:
                fx.BurstSpell = def->SpellId;
                fx.BurstPct += pct;
                fx.BurstDelayMs = static_cast<uint32>(std::max(0.0f, def->Param1));
                break;
            case AffixHandler::DeathPool:    fx.PoolSpell = def->SpellId; fx.PoolPct += pct; break;
            case AffixHandler::SplitOnDeath:
                fx.SplitCount = std::max(fx.SplitCount, static_cast<uint32>(a.Value));
                break;
            case AffixHandler::PlayerAura:
                fx.PlayerAuras.push_back({ def->SpellId,
                    static_cast<int32>(std::lround(a.Value * (def->Param1 < 0.0f ? -1.0f : 1.0f))) });
                break;
            case AffixHandler::GroundPeriodic:
                fx.Ground.push_back({ def->SpellId, static_cast<uint32>(std::max(1000.0f, def->Param1)), pct });
                break;
            case AffixHandler::PackSize:     fx.PackExtraPct  += a.Value; break;
            case AffixHandler::Swarm:        fx.SwarmExtraPct += a.Value; break;
            case AffixHandler::EliteChance:  fx.EliteChanceBonus += a.Value; break;
            case AffixHandler::RareChance:   fx.RareChanceMult *= 1.0f + pct; break;
            case AffixHandler::BossAdds:
                fx.BossAddsPerWave = std::max(fx.BossAddsPerWave, static_cast<uint32>(a.Value));
                break;
            default:
                break;
        }
    }
    return fx;
}

} // namespace Atlas

#endif
