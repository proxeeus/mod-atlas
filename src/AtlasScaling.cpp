/*
 * mod-atlas — AtlasScaling.cpp
 * Scaling (DESIGN §9): role stats from the stock base stats of the run's
 * level, creature ownership (so the stats survive anything that resets them),
 * and the environmental damage scale. The math is in AtlasScalingMath.h.
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
#include "AtlasScalingMath.h"
#include "AtlasAffixes.h"

namespace Atlas
{

// ---------------------------------------------------------------------------
// Base stats and role stats
// ---------------------------------------------------------------------------

namespace
{
    struct LevelBase
    {
        float Health      = 1.0f;
        float Damage      = 1.0f;
        float AttackPower = 0.0f;
        float Armor       = 0.0f;
    };

    // creature_classlevelstats through AzerothCore's own cache, in the column of
    // `expansion`, falling back to a lower column where the table holds no
    // value (1) for that level.
    LevelBase BaseAt(uint8 level, uint8 unitClass, uint32 expansion)
    {
        CreatureBaseStats const* stats = sObjectMgr->GetCreatureBaseStats(std::max<uint8>(level, 1),
            unitClass ? unitClass : 1);
        LevelBase base;
        if (!stats)
            return base;

        uint32 hpCol = std::min<uint32>(expansion, MAX_EXPANSIONS - 1);
        while (hpCol > 0 && stats->BaseHealth[hpCol] <= 1)
            --hpCol;
        uint32 dmgCol = std::min<uint32>(expansion, MAX_EXPANSIONS - 1);
        while (dmgCol > 0 && stats->BaseDamage[dmgCol] <= 1.0f)
            --dmgCol;

        base.Health      = static_cast<float>(std::max<uint32>(stats->BaseHealth[hpCol], 1));
        base.Damage      = std::max(stats->BaseDamage[dmgCol], 1.0f);
        base.AttackPower = static_cast<float>(stats->AttackPower);
        base.Armor       = stats->BaseArmor;
        return base;
    }
}

AtlasMgr::ScalingContext AtlasMgr::ContextOf(Session const& s)
{
    ScalingContext ctx;
    ctx.Level          = std::max<uint8>(s.EffectiveLevel, 1);
    ctx.Tier           = std::max<uint32>(s.Tier, 1);
    ctx.DifficultyId   = s.DifficultyId;
    ctx.PartySize      = std::max<uint32>(static_cast<uint32>(s.Players.size()), 1);
    ctx.RoguelikeRunId = s.RoguelikeRunId;
    ctx.InstanceId     = s.InstanceId;
    ctx.ThemeId        = s.ThemeId;
    ctx.Affixes        = s.Affixes;
    ctx.Effects        = s.Effects;
    return ctx;
}

float AtlasMgr::SessionHealthFactor(ScalingContext const& s) const
{
    DifficultyTier const* d = sAtlasConfig->GetDifficulty(s.DifficultyId);
    float factor = (d ? d->HealthMultiplier : 1.0f)
        * PartyFactor(s.PartySize, sAtlasConfig->GetSoloMultiplier(), sAtlasConfig->GetPerPlayerHealthMult());
    factor *= s.RoguelikeRunId
        ? sRoguelikeMgr->GetTierHealthMultiplier(s.RoguelikeRunId)
        : TierMultiplier(sAtlasConfig->GetTierGrowth(), s.Tier);
    return factor;
}

float AtlasMgr::SessionDamageFactor(ScalingContext const& s) const
{
    DifficultyTier const* d = sAtlasConfig->GetDifficulty(s.DifficultyId);
    float factor = (d ? d->DamageMultiplier : 1.0f)
        * PartyFactor(s.PartySize, sAtlasConfig->GetSoloMultiplier(), sAtlasConfig->GetPerPlayerDamageMult());
    factor *= s.RoguelikeRunId
        ? sRoguelikeMgr->GetTierDamageMultiplier(s.RoguelikeRunId)
        : TierMultiplier(sAtlasConfig->GetTierGrowth(), s.Tier);
    return factor;
}

AtlasMgr::RoleStats AtlasMgr::ComputeRoleStats(ScalingContext const& session, CreatureTemplate const* tmpl,
    CreatureRole role, float affixHealth, float affixDamage) const
{
    RoleStats stats;
    if (!tmpl)
        return stats;

    uint8 const level = std::max<uint8>(session.Level, 1);
    LevelBase const now = BaseAt(level, tmpl->unit_class, ExpansionForLevel(level));
    RoleMultipliers const& r = sAtlasConfig->GetRole(role);

    float const hpMult  = r.Health * SessionHealthFactor(session) * affixHealth;
    float const dmgMult = r.Damage * SessionDamageFactor(session) * affixDamage;

    stats.Level     = level;
    stats.MaxHealth = std::max<uint32>(1, static_cast<uint32>(now.Health * hpMult));

    // Weapon damage and attack power are set so AzerothCore's own melee formula
    // lands on base x multiplier whatever the template's DamageModifier.
    MeleeSetup const melee = ComputeMelee(now.Damage, now.AttackPower, dmgMult, tmpl->DamageModifier);
    stats.WeaponMin   = melee.WeaponMin;
    stats.WeaponMax   = melee.WeaponMax;
    stats.AttackPower = melee.AttackPower;

    float armor = now.Armor * r.Armor;
    if (session.RoguelikeRunId)
        armor *= std::max(1.0f, sRoguelikeMgr->GetTierArmorMultiplier(session.RoguelikeRunId));
    stats.Armor = static_cast<uint32>(armor);

    // Spells and heals keep their weight against melee and health: scale them
    // by how much this creature's melee and health moved from its template
    // (its own level, expansion and modifiers) to the run.
    uint8 const origLevel = std::max<uint8>(tmpl->maxlevel, 1);
    LevelBase const orig = BaseAt(origLevel, tmpl->unit_class, tmpl->expansion);
    float const origMelee = (orig.Damage + orig.AttackPower / 14.0f) * std::max(tmpl->DamageModifier, 0.01f);
    float const newMelee  = (now.Damage + now.AttackPower / 14.0f) * dmgMult;
    stats.SpellScale = ClampScale(newMelee / std::max(origMelee, 1.0f), 0.05f, sAtlasConfig->GetSpellScaleMax());

    float const origHealth = std::max(orig.Health * tmpl->ModHealth, 1.0f);
    stats.HealScale = ClampScale(static_cast<float>(stats.MaxHealth) / origHealth, 0.05f, 100.0f);
    return stats;
}

void AtlasMgr::ApplyRoleStats(Creature* c, RoleStats const& stats, bool keepHealthPct)
{
    if (!c)
        return;

    float const healthPct = keepHealthPct && c->GetMaxHealth()
        ? static_cast<float>(c->GetHealth()) / static_cast<float>(c->GetMaxHealth())
        : 1.0f;

    // Set the stat system's base values (as Creature::SelectLevel does), not
    // just the fields, so a later stat update (an aura, a buff) keeps them.
    c->SetCreateHealth(stats.MaxHealth);
    c->SetStatFlatModifier(UNIT_MOD_HEALTH, BASE_VALUE, static_cast<float>(stats.MaxHealth));

    for (WeaponAttackType att : { BASE_ATTACK, OFF_ATTACK, RANGED_ATTACK })
    {
        c->SetBaseWeaponDamage(att, MINDAMAGE, stats.WeaponMin);
        c->SetBaseWeaponDamage(att, MAXDAMAGE, stats.WeaponMax);
    }
    c->SetStatFlatModifier(UNIT_MOD_ATTACK_POWER, BASE_VALUE, stats.AttackPower);
    c->SetStatFlatModifier(UNIT_MOD_ATTACK_POWER_RANGED, BASE_VALUE, stats.AttackPower);

    // Armor from the run's level; spell resistances cleared (the template's
    // were tuned for its own level).
    c->SetStatFlatModifier(UNIT_MOD_ARMOR, BASE_VALUE, static_cast<float>(stats.Armor));
    for (uint8 school = SPELL_SCHOOL_HOLY; school < MAX_SPELL_SCHOOL; ++school)
        c->SetStatFlatModifier(UnitMods(UNIT_MOD_RESISTANCE_START + school), BASE_VALUE, 0.0f);

    c->UpdateAllStats();

    uint32 const health = std::max<uint32>(1, static_cast<uint32>(c->GetMaxHealth() * healthPct));
    c->SetHealth(std::min(health, c->GetMaxHealth()));
}

void AtlasMgr::ScaleCreature(Creature* c, ScalingContext const& ctx, CreatureRole role)
{
    if (!c)
        return;

    AffixEffects const& fx = ctx.Effects;
    bool const isBoss = role == CreatureRole::Boss;
    float const affixHealth = isBoss ? fx.BossHealth : fx.MonsterHealth;
    float const affixDamage = isBoss ? fx.BossDamage : 1.0f;

    RoleStats const stats = ComputeRoleStats(ctx, c->GetCreatureTemplate(), role, affixHealth, affixDamage);
    c->SetLevel(stats.Level);
    ApplyRoleStats(c, stats, false);

    // Speed affixes, once at spawn (nothing the module re-applies resets them).
    if (fx.AttackSpeedPct > 0.0f)
    {
        c->ApplyAttackTimePercentMod(BASE_ATTACK, fx.AttackSpeedPct, true);
        c->ApplyAttackTimePercentMod(OFF_ATTACK, fx.AttackSpeedPct, true);
        c->ApplyCastTimePercentMod(fx.AttackSpeedPct, true);
    }
    if (fx.MoveSpeedPct > 0.0f)
        for (UnitMoveType move : { MOVE_WALK, MOVE_RUN })
            c->SetSpeed(move, c->GetSpeedRate(move) * (1.0f + fx.MoveSpeedPct / 100.0f), true);

    // Combat affixes, baked per creature for the hooks.
    CreatureAffixMods mods;
    mods.MeleeDealt = fx.MeleeDealt;
    mods.PhysTaken  = fx.PhysTaken;
    mods.SpellTaken = fx.SpellTaken;
    if (!fx.ExtraHits.empty())
    {
        mods.ExtraHitSpell = fx.ExtraHits.front().SpellId;
        mods.ExtraHitPct   = fx.ExtraHits.front().Pct;
    }
    if (fx.ShieldSpell && fx.ShieldPct > 0.0f)
    {
        mods.ShieldSpell  = fx.ShieldSpell;
        mods.ShieldAmount = static_cast<int32>(stats.MaxHealth * fx.ShieldPct);
    }
    if (fx.BurstSpell && fx.BurstPct > 0.0f)
    {
        mods.BurstSpell   = fx.BurstSpell;
        mods.BurstAmount  = static_cast<int32>(stats.MaxHealth * fx.BurstPct);
        mods.BurstDelayMs = fx.BurstDelayMs;
    }
    if (fx.PoolSpell && fx.PoolPct > 0.0f)
    {
        float const hit = MeleeHit((stats.WeaponMin + stats.WeaponMax) / 2.0f, stats.AttackPower,
            std::max(c->GetCreatureTemplate()->DamageModifier, 0.01f), 2.0f);
        mods.PoolSpell = fx.PoolSpell;
        mods.PoolTick  = std::max(1, static_cast<int32>(hit * fx.PoolPct));
    }
    if (role == CreatureRole::Elite)
        mods.SplitCount = fx.SplitCount;
    if (isBoss)
        mods.AddsPerWave = fx.BossAddsPerWave;

    // Affix visibility (PLAN 1.6): visual-only markers, a rare's own first,
    // then the affixes'. Evading drops them; the creature update hook puts
    // them back once it is idle.
    std::size_t markers = 0;
    std::size_t const maxMarkers = std::min<std::size_t>(sAtlasConfig->GetAffixMaxMarkers(), mods.Markers.size());
    auto addMarker = [&](uint32 spell)
    {
        if (!spell || markers >= maxMarkers
            || std::find(mods.Markers.begin(), mods.Markers.begin() + markers, spell) != mods.Markers.begin() + markers)
            return;
        mods.Markers[markers++] = spell;
        c->AddAura(spell, c);
    };
    if (role == CreatureRole::Rare)
        addMarker(sAtlasConfig->GetRareMarker());
    for (uint32 spell : fx.Markers)
        addMarker(spell);

    RegisterOwnedCreature(c->GetGUID(), stats, mods, c->GetMap() ? c->GetMap()->GetInstanceId() : ctx.InstanceId);
}

void AtlasMgr::ScaleCreature(Creature* c, ScalingContext const& ctx, CreatureRole role,
    std::vector<ActiveAffix> const& ownAffixes)
{
    if (ownAffixes.empty())
    {
        ScaleCreature(c, ctx, role);
        return;
    }

    ScalingContext own = ctx;
    own.Affixes.insert(own.Affixes.end(), ownAffixes.begin(), ownAffixes.end());
    own.Effects = sAtlasAffixes->Aggregate(own.Affixes);
    ScaleCreature(c, own, role);
}

float AtlasMgr::ReferenceTrashHit(ScalingContext const& ctx) const
{
    CreatureTemplate tmpl;
    tmpl.unit_class     = 1;
    tmpl.maxlevel       = ctx.Level;
    tmpl.expansion      = ExpansionForLevel(ctx.Level);
    tmpl.DamageModifier = 1.0f;
    tmpl.ModHealth      = 1.0f;
    RoleStats const st = ComputeRoleStats(ctx, &tmpl, CreatureRole::Trash);
    return MeleeHit((st.WeaponMin + st.WeaponMax) / 2.0f, st.AttackPower, 1.0f, 2.0f);
}

std::vector<std::string> AtlasMgr::DescribeRoleScaling(uint8 level, uint32 tier, uint32 partySize,
    uint32 difficultyId) const
{
    // A warrior-class (unit_class 1) creature with stock modifiers, the
    // reference the role table is tuned on.
    CreatureTemplate tmpl;
    tmpl.unit_class     = 1;
    tmpl.maxlevel       = level;
    tmpl.expansion      = ExpansionForLevel(level);
    tmpl.DamageModifier = 1.0f;
    tmpl.ModHealth      = 1.0f;

    ScalingContext s;
    s.Level        = level;
    s.Tier         = std::max<uint32>(tier, 1);
    s.DifficultyId = difficultyId;
    s.PartySize    = std::max<uint32>(partySize, 1);

    std::vector<std::string> lines;
    DifficultyTier const* d = sAtlasConfig->GetDifficulty(difficultyId);
    lines.push_back(Acore::StringFormat(
        "Level {}, tier {}, party of {}, {}: health x{:.2f}, damage x{:.2f} (warrior-class creature, 2.0 s swing)",
        level, s.Tier, s.PartySize, d ? d->Name : "no difficulty", SessionHealthFactor(s),
        SessionDamageFactor(s)));

    for (uint8 r = 0; r < static_cast<uint8>(CreatureRole::Count); ++r)
    {
        CreatureRole const role = static_cast<CreatureRole>(r);
        RoleStats const st = ComputeRoleStats(s, &tmpl, role, 1.0f, 1.0f);
        float const hitMin = MeleeHit(st.WeaponMin, st.AttackPower, 1.0f, 2.0f);
        float const hitMax = MeleeHit(st.WeaponMax, st.AttackPower, 1.0f, 2.0f);
        lines.push_back(Acore::StringFormat("  {:<8} {:>9} health, hits {:.0f}-{:.0f}, armor {}",
            RoleName(role), st.MaxHealth, hitMin, hitMax, st.Armor));
    }
    return lines;
}

// ---------------------------------------------------------------------------
// Creature level ownership
//
// Cheap global gate so the AllCreatureScript hooks cost nothing on a server with
// no active Atlas session.
// ---------------------------------------------------------------------------
static std::atomic<uint32> g_dmOwnedCount{0};

bool AtlasMgr::HasOwnedCreatures()
{
    return g_dmOwnedCount.load(std::memory_order_relaxed) != 0;
}

void AtlasMgr::RegisterOwnedCreature(ObjectGuid guid, RoleStats const& stats, CreatureAffixMods const& mods,
    uint32 instanceId)
{
    if (!guid || !stats.Level)
        return;

    std::lock_guard<std::mutex> lock(_ownedMutex);

    OwnedCreature& oc = _ownedCreatures[guid];
    if (oc.Stats.Level == 0)
        g_dmOwnedCount.fetch_add(1, std::memory_order_relaxed);

    oc.Stats      = stats;
    oc.Mods       = mods;
    oc.InstanceId = instanceId;
}

bool AtlasMgr::GetOwnedCombat(ObjectGuid guid, CreatureAffixMods& mods, float& spellScale) const
{
    std::lock_guard<std::mutex> lock(_ownedMutex);
    auto it = _ownedCreatures.find(guid);
    if (it == _ownedCreatures.end())
        return false;
    mods = it->second.Mods;
    spellScale = it->second.Stats.SpellScale;
    return true;
}

bool AtlasMgr::GetOwnedCreature(ObjectGuid guid, uint8& level, uint32& maxHealth,
    std::array<uint32, 3>* markers) const
{
    std::lock_guard<std::mutex> lock(_ownedMutex);

    auto it = _ownedCreatures.find(guid);
    if (it == _ownedCreatures.end())
        return false;

    level     = it->second.Stats.Level;
    maxHealth = it->second.Stats.MaxHealth;
    if (markers)
        *markers = it->second.Mods.Markers;
    return true;
}

float AtlasMgr::GetOwnedSpellScale(ObjectGuid guid) const
{
    std::lock_guard<std::mutex> lock(_ownedMutex);
    auto it = _ownedCreatures.find(guid);
    return it != _ownedCreatures.end() ? it->second.Stats.SpellScale : -1.0f;
}

float AtlasMgr::GetOwnedHealScale(ObjectGuid guid) const
{
    std::lock_guard<std::mutex> lock(_ownedMutex);
    auto it = _ownedCreatures.find(guid);
    return it != _ownedCreatures.end() ? it->second.Stats.HealScale : -1.0f;
}

bool AtlasMgr::ReapplyOwnedStats(Creature* creature, bool keepHealthPct)
{
    if (!creature)
        return false;

    RoleStats stats;
    {
        std::lock_guard<std::mutex> lock(_ownedMutex);
        auto it = _ownedCreatures.find(creature->GetGUID());
        if (it == _ownedCreatures.end())
            return false;
        stats = it->second.Stats;
    }

    if (creature->GetLevel() != stats.Level)
        creature->SetLevel(stats.Level);
    ApplyRoleStats(creature, stats, keepHealthPct);
    return true;
}

void AtlasMgr::ForgetOwnedCreature(ObjectGuid guid)
{
    std::lock_guard<std::mutex> lock(_ownedMutex);

    if (_ownedCreatures.erase(guid))
        g_dmOwnedCount.fetch_sub(1, std::memory_order_relaxed);
}

void AtlasMgr::ForgetOwnedCreaturesForInstance(uint32 instanceId)
{
    if (!instanceId)
        return;

    std::lock_guard<std::mutex> lock(_ownedMutex);

    for (auto it = _ownedCreatures.begin(); it != _ownedCreatures.end(); )
    {
        if (it->second.InstanceId == instanceId)
        {
            it = _ownedCreatures.erase(it);
            g_dmOwnedCount.fetch_sub(1, std::memory_order_relaxed);
        }
        else
            ++it;
    }
}

// Adds summoned by Atlas creatures (or appearing mid-run) take the Add role.
// Without it an add enters at its own template level, e.g. a level-69 Fel Imp
// inside a level-25 run, which the client marks with a skull.
void AtlasMgr::ScaleSummonedCreature(Creature* creature, Session* session)
{
    if (!creature || !session || !session->EffectiveLevel)
        return;

    ScaleCreature(creature, ContextOf(*session), CreatureRole::Add);
}

// Scale environmental damage to party level
float AtlasMgr::GetEnvironmentalDamageScale(ObjectGuid playerGuid)
{
    std::lock_guard<std::mutex> lock(_sessionMutex);
    auto pit = _playerToSession.find(playerGuid);
    if (pit == _playerToSession.end())
        return 1.0f;

    auto sit = _activeSessions.find(pit->second);
    if (sit == _activeSessions.end())
        return 1.0f;

    Session const& session = sit->second;

    if (!session.ScaleToParty)
        return 1.0f;

    DungeonInfo const* dg = sAtlasConfig->GetDungeon(session.MapId);
    if (!dg)
        return 1.0f;

    uint8 dungeonLevel = dg->MaxLevel;
    uint8 partyLevel   = session.EffectiveLevel;

    if (partyLevel >= dungeonLevel)
        return 1.0f;

    float ratio = static_cast<float>(partyLevel) / static_cast<float>(dungeonLevel);
    float scale = std::pow(ratio, 1.5f);
    if (scale < 0.05f)
        scale = 0.05f;

    return scale;
}

} // namespace Atlas
