/*
 * mod-atlas — AtlasAffixEffects.cpp
 * Affixes in play (PLAN 1.2): what a run's affixes do to its creatures and
 * players once the run is under way. Spawn-time effects (health, damage,
 * speed, pack composition) are applied by the population and ScaleCreature;
 * the hit-by-hit ones by the unit script, from the mods baked per creature.
 *
 * Locking: summoning a creature or dealing damage re-enters hooks that take
 * the session lock, so nothing here summons or casts while holding it. Spells
 * are cast from the caster's own event queue (CastLater), summons go through
 * OwnSpawnScope.
 *
 * AtlasMgr is split across several files; the class itself is in AtlasMgr.h.
 */

#include "AtlasMgr.h"
#include "AtlasAffixes.h"
#include "AtlasConfig.h"
#include "AtlasInternal.h"
#include "Chat.h"
#include "Creature.h"
#include "Log.h"
#include "Map.h"
#include "ObjectAccessor.h"
#include "Player.h"
#include "StringFormat.h"
#include "TemporarySummon.h"
#include <cmath>

namespace Atlas
{

namespace
{
    thread_local uint32 t_ownSpawnDepth = 0;

    // "World Invisible Trigger": level 80, not selectable, no AI script.
    constexpr uint32 AFFIX_CASTER_ENTRY = 12999;

    // Summoner's waves, in % of the boss's life.
    constexpr float BOSS_WAVES[] = { 75.0f, 50.0f, 25.0f };

    // Cast from the caster's own event queue: never under the module's locks,
    // and the event dies with the caster.
    void CastLater(Creature* caster, uint32 spellId, int32 amount, Milliseconds delay)
    {
        if (!caster || !spellId)
            return;
        caster->m_Events.AddEventAtOffset([caster, spellId, amount]()
        {
            if (caster->IsInWorld())
                caster->CastCustomSpell(caster, spellId, &amount, nullptr, nullptr, true);
        }, delay);
    }
}

AtlasMgr::OwnSpawnScope::OwnSpawnScope()
{
    ++t_ownSpawnDepth;
}

AtlasMgr::OwnSpawnScope::~OwnSpawnScope()
{
    --t_ownSpawnDepth;
}

bool AtlasMgr::IsSpawningOwn()
{
    return t_ownSpawnDepth != 0;
}

void AtlasMgr::SetSessionAffixes(Session* session, std::vector<ActiveAffix> affixes)
{
    if (!session)
        return;

    session->Affixes = std::move(affixes);
    session->Effects = sAtlasAffixes->Aggregate(session->Affixes);
    session->GroundTimersMs.clear();
    for (AffixGround const& g : session->Effects.Ground)
        session->GroundTimersMs.push_back(static_cast<int32>(g.IntervalMs));
}

bool AtlasMgr::GetScalingContext(uint32 instanceId, ScalingContext& out)
{
    std::lock_guard<std::mutex> lock(_sessionMutex);
    auto it = _instanceToSession.find(instanceId);
    if (it == _instanceToSession.end())
        return false;
    auto sit = _activeSessions.find(it->second);
    if (sit == _activeSessions.end())
        return false;
    out = ContextOf(sit->second);
    return true;
}

// A short-lived, untargetable caster for an affix's spell (ground zones,
// bursts). Damage immunity keeps the players' area spells from killing it
// (and its zone with it) early.
Creature* AtlasMgr::SpawnAffixCaster(Map* map, Position const& pos, uint8 level, uint32 lifetimeMs)
{
    if (!map)
        return nullptr;

    OwnSpawnScope const own;
    TempSummon* caster = map->SummonCreature(AFFIX_CASTER_ENTRY, pos, nullptr, lifetimeMs);
    if (!caster)
        return nullptr;

    caster->SetLevel(std::max<uint8>(level, 1));
    caster->SetFaction(14);
    caster->SetReactState(REACT_PASSIVE);
    caster->SetUnitFlag(UNIT_FLAG_NOT_SELECTABLE);
    caster->ApplySpellImmune(0, IMMUNITY_DAMAGE, SPELL_SCHOOL_MASK_ALL, true);
    return caster;
}

void AtlasMgr::OnRunCreatureEngaged(Creature* creature)
{
    if (!creature || !HasOwnedCreatures())
        return;

    CreatureAffixMods mods;
    float spellScale = 1.0f;
    if (!GetOwnedCombat(creature->GetGUID(), mods, spellScale) || !mods.ShieldSpell || mods.ShieldAmount <= 0)
        return;

    creature->CastCustomSpell(creature, mods.ShieldSpell, &mods.ShieldAmount, nullptr, nullptr, true);
}

void AtlasMgr::OnRunCreatureDied(Creature* creature)
{
    if (!creature || !HasOwnedCreatures())
        return;

    CreatureAffixMods mods;
    float spellScale = 1.0f;
    if (!GetOwnedCombat(creature->GetGUID(), mods, spellScale))
        return;

    Map* map = creature->GetMap();
    Position const pos = creature->GetPosition();
    uint8 const level = creature->GetLevel();

    // Bursting: a shadow burst where it fell, after a short warning.
    if (mods.BurstSpell && mods.BurstAmount > 0)
        if (Creature* caster = SpawnAffixCaster(map, pos, level, mods.BurstDelayMs + 3000))
            CastLater(caster, mods.BurstSpell, mods.BurstAmount, Milliseconds(std::max<uint32>(mods.BurstDelayMs, 1)));

    // Desecrating: a pool of desecrated ground where it fell.
    if (mods.PoolSpell && mods.PoolTick > 0)
        if (Creature* caster = SpawnAffixCaster(map, pos, level, 12000))
            CastLater(caster, mods.PoolSpell, mods.PoolTick, 1ms);

    // Splitting: lesser copies of an elite.
    if (mods.SplitCount && map)
    {
        ScalingContext ctx;
        if (!GetScalingContext(map->GetInstanceId(), ctx))
            return;

        for (uint32 i = 0; i < mods.SplitCount; ++i)
        {
            float const angle = static_cast<float>(i) * 2.0f * float(M_PI) / static_cast<float>(mods.SplitCount);
            SpawnRunAdd(map, creature->GetEntry(), creature->GetNearPosition(2.0f, angle), ctx, CreatureRole::Add,
                0.7f, nullptr);
        }
    }
}

void AtlasMgr::OnRunCreatureDamaged(Creature* creature, uint32 damage)
{
    if (!creature || !HasOwnedCreatures() || !creature->GetMaxHealth())
        return;

    // Summoner: one wave of adds per threshold crossed (75%, 50%, 25%).
    uint32 const healthAfter = creature->GetHealth() > damage ? creature->GetHealth() - damage : 0;
    float const pctAfter = 100.0f * static_cast<float>(healthAfter) / static_cast<float>(creature->GetMaxHealth());

    uint8 due = 0;
    for (float wave : BOSS_WAVES)
        if (pctAfter <= wave)
            ++due;

    uint32 addsPerWave = 0;
    uint8 from = 0;
    {
        std::lock_guard<std::mutex> lock(_ownedMutex);
        auto it = _ownedCreatures.find(creature->GetGUID());
        if (it == _ownedCreatures.end() || !it->second.Mods.AddsPerWave || due <= it->second.Mods.WavesDone)
            return;
        addsPerWave = it->second.Mods.AddsPerWave;
        from = it->second.Mods.WavesDone;
        it->second.Mods.WavesDone = due;
    }

    Map* map = creature->GetMap();
    ScalingContext ctx;
    if (!map || !GetScalingContext(map->GetInstanceId(), ctx))
        return;

    Theme const* theme = sAtlasConfig->GetTheme(ctx.ThemeId);
    if (!theme)
        return;

    // Combat-time randomness, not part of the run's layout (C8 covers layout).
    RunRng rng(RunRng::MakeSeed());
    uint32 const count = addsPerWave * (due - from);
    for (uint32 i = 0; i < count; ++i)
    {
        uint32 const entry = SelectCreatureForTheme(theme, false, rng);
        if (!entry)
            continue;
        float const angle = static_cast<float>(i) * 2.0f * float(M_PI) / static_cast<float>(count);
        SpawnRunAdd(map, entry, creature->GetNearPosition(6.0f, angle), ctx, CreatureRole::Add, 1.0f,
            creature->GetVictim());
    }

    creature->TextEmote(Acore::StringFormat("{} calls servants from other timelines!", creature->GetName()),
        nullptr, true);
}

void AtlasMgr::QueueExtraHit(Unit* attacker, Unit* target, uint32 spellId, int32 amount)
{
    if (!attacker || !target || !spellId || amount <= 0)
        return;

    ObjectGuid const targetGuid = target->GetGUID();
    attacker->m_Events.AddEventAtOffset([attacker, targetGuid, spellId, amount]()
    {
        Unit* victim = ObjectAccessor::GetUnit(*attacker, targetGuid);
        if (victim && victim->IsAlive() && attacker->IsAlive())
            attacker->CastCustomSpell(victim, spellId, &amount, nullptr, nullptr, true);
    }, 1ms);
}

// Once a second per run, under the session lock: the players' affix auras,
// and the timers of ground affixes (which only fire while the party fights).
// What the timers fire is returned and cast after the lock is released.
void AtlasMgr::TickSessionAffixes(Session& session, std::vector<GroundRequest>& out)
{
    bool const hasAuras = !session.Effects.PlayerAuras.empty();
    std::vector<Player*> present;
    for (auto const& pd : session.Players)
    {
        Player* p = ObjectAccessor::FindPlayer(pd.PlayerGuid);
        if (!p || !p->IsInWorld())
            continue;

        bool const inRun = p->GetMapId() == session.MapId
            && (!session.InstanceId || p->GetInstanceId() == session.InstanceId);
        if (!inRun)
        {
            sAtlasAffixes->RemovePlayerAuras(p);
            continue;
        }
        if (hasAuras)
            sAtlasAffixes->ApplyPlayerAuras(p, session.Effects);
        if (p->IsAlive())
            present.push_back(p);
    }

    if (session.Effects.Ground.empty() || present.empty() || !session.IsGroupInCombat())
        return;

    ScalingContext const ctx = ContextOf(session);
    for (std::size_t i = 0; i < session.Effects.Ground.size() && i < session.GroundTimersMs.size(); ++i)
    {
        session.GroundTimersMs[i] -= static_cast<int32>(UPDATE_INTERVAL);
        if (session.GroundTimersMs[i] > 0)
            continue;

        AffixGround const& g = session.Effects.Ground[i];
        session.GroundTimersMs[i] = static_cast<int32>(g.IntervalMs);

        Player* target = present[RandInt<std::size_t>(0, present.size() - 1)];
        GroundRequest req;
        req.Player     = target->GetGUID();
        req.InstanceId = target->GetInstanceId();
        req.SpellId    = g.SpellId;
        req.Amount     = std::max(1, static_cast<int32>(ReferenceTrashHit(ctx) * g.Pct));
        req.Level      = ctx.Level;
        out.push_back(req);
    }
}

void AtlasMgr::ExecuteGroundRequests(std::vector<GroundRequest> const& requests)
{
    for (GroundRequest const& req : requests)
    {
        Player* p = ObjectAccessor::FindPlayer(req.Player);
        if (!p || !p->IsInWorld() || !p->IsAlive() || p->GetInstanceId() != req.InstanceId)
            continue;

        if (Creature* caster = SpawnAffixCaster(p->GetMap(), p->GetPosition(), req.Level, 15000))
            CastLater(caster, req.SpellId, req.Amount, 1ms);
    }
}

} // namespace Atlas
