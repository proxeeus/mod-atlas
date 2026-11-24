/*
 * mod-atlas — AtlasAtmosphere.cpp
 * AtlasMgr: the sky and fog of a run (PLAN 1.6, see AtlasAtmosphere.h).
 */

#include "AtlasMgr.h"
#include "AtlasAffixes.h"
#include "AtlasChronicle.h"
#include "AtlasConfig.h"
#include "AtlasTheme.h"
#include "DatabaseEnv.h"
#include "Field.h"
#include "Log.h"
#include "MiscPackets.h"
#include "ObjectAccessor.h"
#include "Player.h"
#include "QueryResult.h"
#include "SpellMgr.h"
#include "StringFormat.h"
#include "WorldPacket.h"
#include "WorldSession.h"
#include <algorithm>

namespace Atlas
{

void AtlasMgr::LoadAtmosphere()
{
    std::vector<AtmosphereRow> rows;
    if (QueryResult result = WorldDatabase.Query("SELECT theme_id, corruption, affix_id, light_id, music_id "
        "FROM atlas_atmosphere ORDER BY id"))
    {
        do
        {
            Field* f = result->Fetch();
            AtmosphereRow r;
            r.ThemeId    = f[0].Get<uint32>();
            r.Corruption = f[1].Get<uint8>();
            r.AffixId    = f[2].Get<uint32>();
            r.LightId    = f[3].Get<uint32>();
            r.MusicId    = f[4].Get<uint32>();
            rows.push_back(r);
        } while (result->NextRow());
    }
    else
        LOG_ERROR("module", "Atlas: table atlas_atmosphere is empty or missing (module SQL not applied?): "
            "runs keep the dungeon's own sky.");

    // Fogs (tools/clones/fogs.json, staged until installed): their spells
    // must exist server-side too, or AddAura would do nothing.
    std::vector<AtmosphereRow> fogs;
    std::vector<uint32> fogSpells;
    if (WorldDatabase.Query("SELECT 1 FROM information_schema.tables WHERE table_schema = DATABASE() "
            "AND table_name = 'atlas_fog'"))
    {
        if (QueryResult result = WorldDatabase.Query("SELECT theme_id, corruption, affix_id, spell_id "
            "FROM atlas_fog ORDER BY id"))
        {
            do
            {
                Field* f = result->Fetch();
                AtmosphereRow r;
                r.ThemeId    = f[0].Get<uint32>();
                r.Corruption = f[1].Get<uint8>();
                r.AffixId    = f[2].Get<uint32>();
                r.FogSpell   = f[3].Get<uint32>();
                if (!sSpellMgr->GetSpellInfo(r.FogSpell))
                {
                    LOG_ERROR("module", "Atlas: atlas_fog names spell {}, which the server does not know "
                        "(atlas_fogs.sql not applied?): row skipped.", r.FogSpell);
                    continue;
                }
                if (std::find(fogSpells.begin(), fogSpells.end(), r.FogSpell) == fogSpells.end())
                    fogSpells.push_back(r.FogSpell);
                fogs.push_back(r);
            } while (result->NextRow());
        }
    }

    std::lock_guard<std::mutex> lock(_atmosphereMutex);
    _atmosphere = std::move(rows);
    _fogs = std::move(fogs);
    _fogSpells = std::move(fogSpells);
    LOG_INFO("module", "Atlas: {} atmosphere looks and {} fog rows ({} fogs) loaded.", _atmosphere.size(),
        _fogs.size(), _fogSpells.size());
}

bool AtlasMgr::IsFogSpell(uint32 spellId) const
{
    std::lock_guard<std::mutex> lock(_atmosphereMutex);
    return std::find(_fogSpells.begin(), _fogSpells.end(), spellId) != _fogSpells.end();
}

std::vector<uint32> AtlasMgr::GetFogSpells() const
{
    std::lock_guard<std::mutex> lock(_atmosphereMutex);
    return _fogSpells;
}

bool AtlasMgr::ClearFogs(Player* player, uint32 keep) const
{
    if (!player)
        return false;
    bool removed = false;
    for (uint32 spell : GetFogSpells())
        if (spell != keep && player->HasAura(spell))
        {
            player->RemoveAurasDueToSpell(spell);
            removed = true;
        }
    return removed;
}

void AtlasMgr::ApplyRunFog(Player* player, Session const& session)
{
    if (!player || !sAtlasConfig->IsAtmosphereEnabled() || !sAtlasConfig->IsFogEnabled() || !IsRealPlayer(player)
        || player->GetMapId() != session.MapId)
        return;
    // Only a client with the Atlas patch knows the spell and its screen
    // effect (an unpatched one would skip the aura, but show nothing).
    if (!IsCloneOptedIn(player->GetSession()->GetAccountId()))
        return;

    uint32 const fog = GetRunLook(session).Fog;
    ClearFogs(player, fog);
    if (fog && !player->HasAura(fog))
        player->AddAura(fog, player);
}

void AtlasMgr::SendLightOverride(Player* player, uint32 anchorLight, uint32 lightId, uint32 fadeMs)
{
    if (!player || !player->GetSession() || !anchorLight)
        return;

    WorldPacket data(SMSG_OVERRIDE_LIGHT, 4 + 4 + 4);
    data << uint32(anchorLight);
    data << uint32(lightId);
    data << uint32(fadeMs);
    player->SendDirectMessage(&data);
}

AtlasMgr::RunLook AtlasMgr::GetRunLook(Session const& session) const
{
    RunLook look;
    std::vector<uint32> affixIds;
    for (ActiveAffix const& a : session.Affixes)
        affixIds.push_back(a.Id);

    std::lock_guard<std::mutex> lock(_atmosphereMutex);
    if (AtmosphereRow const* row = PickThemeLook(_atmosphere, session.ThemeId, CorruptionLevel(session.Tier)))
    {
        look.Light = row->LightId;
        look.Music = row->MusicId;
    }
    if (AtmosphereRow const* row = PickAffixLook(_atmosphere, affixIds))
    {
        look.AffixLight = row->LightId;
        if (!look.Music)
            look.Music = row->MusicId;
    }
    look.Fog = PickFog(_fogs, session.ThemeId, CorruptionLevel(session.Tier), affixIds);
    return look;
}

void AtlasMgr::SendAtmosphere(Player* player, Session const& session, bool log)
{
    if (!player || !sAtlasConfig->IsAtmosphereEnabled())
        return;

    RunLook const look = GetRunLook(session);
    if (log)
        LOG_INFO("module", "Atlas: Atmosphere for {} on map {} — light {} over anchor {}, affix light {} over {}, "
            "music {}, fog {}.", player->GetName(), session.MapId, look.Light, sAtlasConfig->GetAtmosphereAnchor(),
            look.AffixLight, sAtlasConfig->GetAtmosphereAffixAnchor(), look.Music, look.Fog);
    uint32 const fade = sAtlasConfig->GetAtmosphereFadeMs();
    // Always send both slots: a run without an affix look clears the second
    // anchor in case the previous Endless depth had one.
    SendLightOverride(player, sAtlasConfig->GetAtmosphereAnchor(), look.Light, fade);
    SendLightOverride(player, sAtlasConfig->GetAtmosphereAffixAnchor(), look.AffixLight, fade);
    if (look.Music)
        player->SendDirectMessage(WorldPackets::Misc::PlayMusic(look.Music).Write());
    ApplyRunFog(player, session);
}

void AtlasMgr::OnPlayerEnteredRunMap(Player* player)
{
    if (!player || !sAtlasConfig->IsAtmosphereEnabled())
        return;

    Session* session = GetSessionByPlayer(player->GetGUID());
    if (!session || player->GetMapId() != session->MapId)
        return;
    SendAtmosphere(player, *session, true);
    PushRunHud(player);

    // Sent again once the loading screen is surely over: the client may still
    // be loading the map when the first copy arrives.
    ObjectGuid const guid = player->GetGUID();
    player->m_Events.AddEventAtOffset([guid]()
    {
        Player* p = ObjectAccessor::FindPlayer(guid);
        if (!p)
            return;
        Session* s = sAtlasMgr->GetSessionByPlayer(guid);
        if (s && p->GetMapId() == s->MapId)
            sAtlasMgr->SendAtmosphere(p, *s);
    }, 3s);
}

// ---------------------------------------------------------------------------
// The run HUD (PLAN 1.7)
// ---------------------------------------------------------------------------

namespace
{
    Chronicle::RunHud BuildRunHud(Session const& s)
    {
        Chronicle::RunHud hud;
        hud.Kind    = s.RoguelikeRunId ? 2 : s.IsMapRun ? 1 : 0;
        hud.Tier    = s.Tier;
        hud.Rarity  = s.MapRarity;
        hud.Portals = s.IsMapRun ? static_cast<int32>(s.PortalsLeft) : -1;
        hud.Forces  = s.ForcesThreshold ? ForcesProgress(s.ForcesKilled, s.ForcesThreshold) : 0;
        hud.Master  = (s.TotalBosses && s.BossesKilled >= s.TotalBosses) || s.State == SessionState::Completed ? 2
            : !s.ForcesThreshold ? 3 : s.BossCalled ? 1 : 0;
        hud.Slain   = s.MobsKilled;
        hud.Total   = s.TotalMobs;
        DungeonInfo const* dg = sAtlasConfig->GetDungeon(s.MapId);
        Theme const* theme = sAtlasConfig->GetTheme(s.ThemeId);
        hud.Dungeon = dg ? dg->Name : "an unknown place";
        hud.Theme   = theme ? theme->Name : "";
        return hud;
    }

    uint64 HudStamp(Chronicle::RunHud const& hud)
    {
        uint64 h = 1469598103934665603ULL;
        for (uint64 v : { uint64(hud.Kind), uint64(hud.Tier), uint64(uint32(hud.Portals)), uint64(hud.Forces),
            uint64(hud.Master), uint64(hud.Slain), uint64(hud.Total) })
            h = (h ^ v) * 1099511628211ULL;
        return h;
    }
}

void AtlasMgr::PushRunHud(Session& session, bool full)
{
    Chronicle::RunHud const hud = BuildRunHud(session);
    uint64 const stamp = HudStamp(hud);
    if (!full && stamp == session.HudStamp)
        return;
    session.HudStamp = stamp;

    std::vector<std::array<std::string, 3>> rows;
    if (full)
        rows = sAtlasAffixes->DescribeRows(session.Affixes);
    for (PlayerSessionData const& pd : session.Players)
        if (Player* p = ObjectAccessor::FindPlayer(pd.PlayerGuid))
            if (p->GetMapId() == session.MapId)
                Chronicle::SendRun(p, hud, full ? &rows : nullptr);
}

void AtlasMgr::PushRunHud(Player* player)
{
    if (!player)
        return;
    Session* s = GetSessionByPlayer(player->GetGUID());
    if (!s || player->GetMapId() != s->MapId)
        return;
    std::vector<std::array<std::string, 3>> const rows = sAtlasAffixes->DescribeRows(s->Affixes);
    Chronicle::SendRun(player, BuildRunHud(*s), &rows);
}

void AtlasMgr::PushRunEnded(Session const& session)
{
    for (PlayerSessionData const& pd : session.Players)
        if (Player* p = ObjectAccessor::FindPlayer(pd.PlayerGuid))
            Chronicle::SendRunEnded(p);
}

std::string AtlasMgr::DescribeRunLook(Session const& session) const
{
    RunLook const look = GetRunLook(session);
    return Acore::StringFormat("light {} (theme {}, corruption {}), affix light {}, music {}, anchors {} / {}, "
        "fog {}", look.Light, session.ThemeId, CorruptionLevel(session.Tier), look.AffixLight, look.Music,
        sAtlasConfig->GetAtmosphereAnchor(), sAtlasConfig->GetAtmosphereAffixAnchor(), look.Fog);
}

} // namespace Atlas
