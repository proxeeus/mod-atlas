/*
 * mod-atlas — AtlasAtmosphere.cpp
 * AtlasMgr: the sky and fog of a run (PLAN 1.6, see AtlasAtmosphere.h).
 */

#include "AtlasMgr.h"
#include "AtlasConfig.h"
#include "AtlasTheme.h"
#include "DatabaseEnv.h"
#include "Field.h"
#include "Log.h"
#include "MiscPackets.h"
#include "Player.h"
#include "QueryResult.h"
#include "StringFormat.h"
#include "WorldPacket.h"
#include "WorldSession.h"

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

    std::lock_guard<std::mutex> lock(_atmosphereMutex);
    _atmosphere = std::move(rows);
    LOG_INFO("module", "Atlas: {} atmosphere looks loaded.", _atmosphere.size());
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
    return look;
}

void AtlasMgr::SendAtmosphere(Player* player, Session const& session)
{
    if (!player || !sAtlasConfig->IsAtmosphereEnabled())
        return;

    RunLook const look = GetRunLook(session);
    uint32 const fade = sAtlasConfig->GetAtmosphereFadeMs();
    // Always send both slots: a run without an affix look clears the second
    // anchor in case the previous Endless depth had one.
    SendLightOverride(player, sAtlasConfig->GetAtmosphereAnchor(), look.Light, fade);
    SendLightOverride(player, sAtlasConfig->GetAtmosphereAffixAnchor(), look.AffixLight, fade);
    if (look.Music)
        player->SendDirectMessage(WorldPackets::Misc::PlayMusic(look.Music).Write());
}

void AtlasMgr::OnPlayerEnteredRunMap(Player* player)
{
    if (!player || !sAtlasConfig->IsAtmosphereEnabled())
        return;

    Session* session = GetSessionByPlayer(player->GetGUID());
    if (!session || player->GetMapId() != session->MapId)
        return;
    SendAtmosphere(player, *session);

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

std::string AtlasMgr::DescribeRunLook(Session const& session) const
{
    RunLook const look = GetRunLook(session);
    return Acore::StringFormat("light {} (theme {}, corruption {}), affix light {}, music {}, anchors {} / {}",
        look.Light, session.ThemeId, CorruptionLevel(session.Tier), look.AffixLight, look.Music,
        sAtlasConfig->GetAtmosphereAnchor(), sAtlasConfig->GetAtmosphereAffixAnchor());
}

} // namespace Atlas
