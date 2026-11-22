/*
 * mod-atlas — AtlasScalingMath.h
 * Role scaling (DESIGN §9) as pure math, so it can be unit-tested.
 *
 * Every run creature gets a role and its stats come from the stock base stats
 * of the run's level (creature_classlevelstats, the column of that level's
 * expansion), never from the template's own modifiers:
 *
 *   health = base health × role health × session health factor × affix
 *   melee  = base melee  × role damage × session damage factor × affix
 *   armor  = base armor  × role armor
 *
 * The session factors are tier growth × the difficulty bracket's multiplier ×
 * the party factor. The level is a parameter (BACKLOG R1): echoes use the
 * party's level, maps will use 80.
 */

#ifndef ATLAS_SCALING_MATH_H
#define ATLAS_SCALING_MATH_H

#include "Define.h"

#include <algorithm>
#include <cmath>

namespace Atlas
{

enum class CreatureRole : uint8
{
    Trash = 0,
    Elite,
    Rare,
    Miniboss,
    Boss,
    Add,
    Count
};

inline char const* RoleName(CreatureRole role)
{
    switch (role)
    {
        case CreatureRole::Trash:    return "Trash";
        case CreatureRole::Elite:    return "Elite";
        case CreatureRole::Rare:     return "Rare";
        case CreatureRole::Miniboss: return "Miniboss";
        case CreatureRole::Boss:     return "Boss";
        case CreatureRole::Add:      return "Add";
        default:                     return "?";
    }
}

struct RoleMultipliers
{
    float Health = 1.0f;
    float Damage = 1.0f;
    float Armor  = 1.0f;
};

// DESIGN §9 starting values, tuned for a party of five at tier 1.
inline RoleMultipliers DefaultRoleMultipliers(CreatureRole role)
{
    switch (role)
    {
        case CreatureRole::Trash:    return { 1.0f,  1.0f, 1.0f };
        case CreatureRole::Elite:    return { 2.5f,  1.5f, 1.0f };
        case CreatureRole::Rare:     return { 6.0f,  2.0f, 1.0f };
        case CreatureRole::Miniboss: return { 15.0f, 2.5f, 1.0f };
        case CreatureRole::Boss:     return { 30.0f, 3.0f, 1.0f };
        case CreatureRole::Add:      return { 0.5f,  0.7f, 1.0f };
        default:                     return {};
    }
}

// growth^(tier-1): tier 1 is 1.0; 1.075 makes tier 16 about 3x.
inline float TierMultiplier(float growth, uint32 tier)
{
    return std::pow(growth, static_cast<float>(std::max<uint32>(tier, 1) - 1));
}

// The party factor, normalised so a party of five (what the role values are
// tuned for) is 1.0. Raw factor: `solo` for one player, else
// 1 + (size - 1) × perExtraPlayer, the V0 Atlas.Scaling.* settings.
inline float PartyFactor(uint32 partySize, float solo, float perExtraPlayer)
{
    auto raw = [solo, perExtraPlayer](uint32 n)
    {
        return n <= 1 ? solo : 1.0f + static_cast<float>(n - 1) * perExtraPlayer;
    };
    float const five = raw(5);
    return five > 0.0f ? raw(partySize) / five : 1.0f;
}

// Which creature_classlevelstats column (0 Classic, 1 TBC, 2 WotLK) stock
// creatures of this level use. Callers fall back to a lower column where the
// table has no value (1).
inline uint8 ExpansionForLevel(uint8 level)
{
    if (level > 70)
        return 2;
    if (level > 60)
        return 1;
    return 0;
}

// AzerothCore's creature melee: per hit = (weapon + AP / 14 × variance) ×
// DamageModifier × attack time (s). To land on `target × multiplier` whatever
// the template's DamageModifier, weapon damage and attack power are both set
// divided by it (dmgModifier is clamped away from zero).
struct MeleeSetup
{
    float WeaponMin   = 1.0f;
    float WeaponMax   = 1.0f;
    float AttackPower = 0.0f;
};

inline MeleeSetup ComputeMelee(float baseDamage, float baseAttackPower, float multiplier, float dmgModifier)
{
    float const dm = std::max(dmgModifier, 0.01f);
    MeleeSetup m;
    m.WeaponMin   = std::max(1.0f, baseDamage * multiplier / dm);
    m.WeaponMax   = std::max(m.WeaponMin, baseDamage * 1.5f * multiplier / dm);
    m.AttackPower = std::max(0.0f, baseAttackPower * multiplier / dm);
    return m;
}

// What AzerothCore's formula then yields for one hit (for checks and tests).
inline float MeleeHit(float weapon, float attackPower, float dmgModifier, float attackTimeSec, float variance = 1.0f)
{
    return (weapon + attackPower / 14.0f * variance) * dmgModifier * attackTimeSec;
}

inline float ClampScale(float value, float lo, float hi)
{
    if (std::isnan(value))
        return 1.0f;
    return std::clamp(value, lo, hi);
}

} // namespace Atlas

#endif
