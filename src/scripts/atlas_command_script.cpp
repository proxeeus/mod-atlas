/*
 * mod-atlas — atlas_command_script.cpp
 * GM commands: .atlas reload, .atlas status, .atlas list, .atlas end, .atlas clearcooldown,
 * .atlas run (the current run and its seed), .atlas seed (replay a seed),
 * .atlas scaling (the role table at a level, tier, party size and difficulty),
 * .atlas affix list|next (the affixes, and forcing some on the next run)
 * Players: .atlas clones [on|off] (runs in clone maps, for clients with the Atlas patch),
 * .atlas fog [list|off|<spell>] (tries the fogs of the Atlas patch),
 * .atlas spike [leave] (the generated-map spike: a kit piece in an empty pool map)
 */

#include "ScriptMgr.h"
#include "Chat.h"
#include "ChatCommand.h"
#include "Player.h"
#include "Creature.h"
#include "Group.h"
#include "Item.h"
#include "ObjectMgr.h"
#include "SpellInfo.h"
#include "SpellMgr.h"
#include "AtlasMgr.h"
#include "AtlasConfig.h"
#include "AtlasAffixes.h"
#include "RoguelikeMgr.h"
#include <algorithm>
#include <cctype>
#include <charconv>

using namespace Acore::ChatCommands;
using namespace Atlas;

class atlas_command_script : public CommandScript
{
public:
    atlas_command_script() : CommandScript("atlas_command_script") {}

    ChatCommandTable GetCommands() const override
    {
        static ChatCommandTable affixTable =
        {
            { "list",          HandleAffixList,      SEC_GAMEMASTER,     Console::Yes },
            { "next",          HandleAffixNext,      SEC_GAMEMASTER,     Console::No  },
        };
        static ChatCommandTable mapTable =
        {
            { "give",          HandleMapGive,        SEC_GAMEMASTER,     Console::No  },
            { "list",          HandleMapList,        SEC_GAMEMASTER,     Console::No  },
        };
        static ChatCommandTable curateTable =
        {
            { "spawn",         HandleCurateSpawn,    SEC_GAMEMASTER,     Console::No  },
            { "",              HandleCurate,         SEC_GAMEMASTER,     Console::Yes },
        };
        static ChatCommandTable atlasTable =
        {
            { "affix",         affixTable },
            { "curate",        curateTable },
            { "map",           mapTable },
            { "loot",          HandleLoot,           SEC_GAMEMASTER,     Console::Yes },
            { "marker",        HandleMarker,         SEC_GAMEMASTER,     Console::No  },
            { "light",         HandleLight,          SEC_GAMEMASTER,     Console::No  },
            { "reload",        HandleReload,        SEC_ADMINISTRATOR,  Console::Yes },
            { "status",        HandleStatus,        SEC_GAMEMASTER,     Console::Yes },
            { "list",          HandleList,           SEC_GAMEMASTER,     Console::Yes },
            { "end",           HandleEnd,            SEC_ADMINISTRATOR,  Console::No  },
            { "clearcooldown", HandleClearCD,        SEC_GAMEMASTER,     Console::No  },
            { "run",           HandleRun,            SEC_GAMEMASTER,     Console::No  },
            { "seed",          HandleSeed,           SEC_GAMEMASTER,     Console::No  },
            { "scaling",       HandleScaling,        SEC_GAMEMASTER,     Console::Yes },
            { "clones",        HandleClones,         SEC_PLAYER,         Console::No  },
            { "fog",           HandleFog,            SEC_PLAYER,         Console::No  },
            { "spike",         HandleSpike,          SEC_PLAYER,         Console::No  },
        };
        static ChatCommandTable root = { { "atlas", atlasTable } };
        return root;
    }

    static bool HandleReload(ChatHandler* h)
    {
        sAtlasConfig->LoadConfig(true);
        sAtlasAffixes->Load();
        sAtlasMgr->LoadThemes();
        sAtlasMgr->LoadLootTiers();
        sAtlasMgr->LoadAtmosphere();
        // Dungeon list or boss count may have changed: rebuild the cached
        // entrances / spawn points too (guarded by their own mutex).
        sAtlasMgr->LoadDungeonGeometry();
        h->SendSysMessage("Atlas: Configuration, affixes and dungeon geometry reloaded.");
        return true;
    }

    // Clone maps (PLAN stage 2) need the Atlas client patch; this is the
    // player saying their client has it. Without the patch, a clone leaves
    // the client on its loading screen for good.
    static bool HandleClones(ChatHandler* h, Optional<std::string> state)
    {
        Player* player = h->GetPlayer();
        if (!player)
            return false;
        uint32 const account = player->GetSession()->GetAccountId();

        if (state)
        {
            std::string s = *state;
            std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return std::tolower(c); });
            if (s != "on" && s != "off")
            {
                h->SendSysMessage("Usage: .atlas clones [on|off]");
                return true;
            }
            bool const on = s == "on";
            if (!on)
                if (Session const* run = sAtlasMgr->GetSessionByPlayer(player->GetGUID()))
                    if (run->IsClone())
                    {
                        h->PSendSysMessage("{} Your echo stands in a clone; close it before you turn clones off.",
                            CHAT_PREFIX);
                        return true;
                    }
            sAtlasMgr->SetCloneOptIn(account, on);
        }

        bool const optedIn = sAtlasMgr->IsCloneOptedIn(account);
        CloneRegistry const& clones = sAtlasConfig->GetClones();
        h->PSendSysMessage("{} Clone maps: {} on this realm, {} usable. Your account: {}.", CHAT_PREFIX,
            sAtlasConfig->IsClonesEnabled() ? "on" : "off", clones.Size(), optedIn ? "opted in" : "not opted in");
        if (!optedIn)
            h->SendSysMessage("Echoes can open in clone maps of their dungeons: the same halls, empty of the present. "
                "Only type .atlas clones on if Data\\patch-T.MPQ is in your client: without it, entering a "
                "clone leaves you on the loading screen.");
        else
            h->SendSysMessage("Map runs open in a clone when one exists and everyone in your party opted in, and "
                "carry the fogs of the patch (.atlas fog list). .atlas clones off turns it off again.");
        return true;
    }

    // .atlas fog [list|off|<spell>]: tries a fog of the Atlas patch on you (it
    // goes when you leave the map); without arguments, your run's fog again.
    // Needs the opt-in: only a client with the patch can show it.
    static bool HandleFog(ChatHandler* h, Optional<std::string> arg)
    {
        Player* player = h->GetPlayer();
        if (!player)
            return false;
        if (!sAtlasMgr->IsCloneOptedIn(player->GetSession()->GetAccountId()))
        {
            h->PSendSysMessage("{} Fogs need the Atlas client patch: see .atlas clones.", CHAT_PREFIX);
            return true;
        }

        std::vector<uint32> const fogs = sAtlasMgr->GetFogSpells();
        std::string const what = arg ? *arg : "";
        if (what == "list")
        {
            for (uint32 spell : fogs)
                if (SpellInfo const* info = sSpellMgr->GetSpellInfo(spell))
                    h->PSendSysMessage("  {} — {}", spell, info->SpellName[0]);
            h->PSendSysMessage("{} {} fogs. .atlas fog <id> tries one, .atlas fog off clears it.", CHAT_PREFIX,
                fogs.size());
            return true;
        }
        if (what == "off")
        {
            sAtlasMgr->ClearFogs(player);
            h->PSendSysMessage("{} The mist clears.", CHAT_PREFIX);
            return true;
        }
        if (what.empty())
        {
            Session* s = sAtlasMgr->GetSessionByPlayer(player->GetGUID());
            if (!s || player->GetMapId() != s->MapId)
            {
                h->SendSysMessage("Usage: .atlas fog list | off | <id> (without one: your run's fog, inside a run)");
                return true;
            }
            sAtlasMgr->ApplyRunFog(player, *s);
            h->PSendSysMessage("{} Your echo's fog: {}.", CHAT_PREFIX, sAtlasMgr->GetRunLook(*s).Fog);
            return true;
        }

        uint32 spell = 0;
        std::from_chars(what.data(), what.data() + what.size(), spell);
        if (!sAtlasMgr->IsFogSpell(spell))
        {
            h->PSendSysMessage("{} {} is not a fog; .atlas fog list shows them.", CHAT_PREFIX, what);
            return true;
        }
        sAtlasMgr->ClearFogs(player, spell);
        if (!player->HasAura(spell))
            player->AddAura(spell, player);
        h->PSendSysMessage("{} Fog {} on you until you leave this map (.atlas fog off clears it).", CHAT_PREFIX,
            spell);
        return true;
    }

    // .atlas spike [leave]: the generated-map spike (DESIGN §6.2). Takes you,
    // and the bots beside you, into an empty pool map with one kit piece (a
    // WMO room placed as a gameobject); `leave` brings you all back.
    static bool HandleSpike(ChatHandler* h, Optional<std::string> arg)
    {
        Player* player = h->GetPlayer();
        if (!player)
            return false;
        if (arg && *arg == "leave")
        {
            if (!sAtlasMgr->IsPoolMap(player->GetMapId()))
            {
                h->PSendSysMessage("{} You are not in the Timeways.", CHAT_PREFIX);
                return true;
            }
            sAtlasMgr->LeaveSpike(player);
            return true;
        }
        if (arg)
        {
            h->SendSysMessage("Usage: .atlas spike [leave]");
            return true;
        }
        std::string error;
        if (!sAtlasMgr->EnterSpike(player, error))
        {
            h->PSendSysMessage("{} {}", CHAT_PREFIX, error);
            return true;
        }
        h->PSendSysMessage("{} The Timeways open. Hold still while the room takes shape (.atlas spike leave to "
            "return).", CHAT_PREFIX);
        return true;
    }

    static bool HandleStatus(ChatHandler* h)
    {
        h->SendSysMessage("=== Atlas (V0, Keepers of Time) Status ===");
        h->PSendSysMessage("Enabled: {}", sAtlasConfig->IsEnabled() ? "Yes" : "No");
        h->PSendSysMessage("Active: {} / {}",
            sAtlasMgr->GetActiveSessionCount(), sAtlasConfig->GetMaxConcurrentRuns());
        h->PSendSysMessage("Level Band: +/-{}", sAtlasConfig->GetLevelBand());
        h->PSendSysMessage("Difficulties: {}  Themes: {}  Dungeons: {}",
            sAtlasConfig->GetDifficulties().size(), sAtlasConfig->GetThemes().size(), sAtlasConfig->GetDungeons().size());
        h->PSendSysMessage("Clone maps: {} ({} usable)", sAtlasConfig->IsClonesEnabled() ? "on" : "off",
            sAtlasConfig->GetClones().Size());
        return true;
    }

    static bool HandleList(ChatHandler* h)
    {
        h->PSendSysMessage("Active DM sessions: {}", sAtlasMgr->GetActiveSessionCount());
        return true;
    }

    static bool HandleEnd(ChatHandler* h, Optional<uint32> sessionId)
    {
        if (sessionId)
        {
            if (!sAtlasMgr->GetSession(*sessionId))
            {
                h->PSendSysMessage("Session {} not found.", *sessionId);
                return false;
            }

            sAtlasMgr->EndSession(*sessionId, false);
            h->PSendSysMessage("Session {} ended.", *sessionId);
            return true;
        }

        // Try the invoker's own session first
        Player* invoker = h->GetSession() ? h->GetSession()->GetPlayer() : nullptr;
        Session* s = invoker ? sAtlasMgr->GetSessionByPlayer(invoker->GetGUID()) : nullptr;

        // Fall back to selected player's session
        if (!s)
        {
            Player* t = h->getSelectedPlayer();
            s = t ? sAtlasMgr->GetSessionByPlayer(t->GetGUID()) : nullptr;
        }

        if (!s)
        {
            h->SendSysMessage("Not in a DM session. Select a player or provide session ID.");
            return false;
        }

        uint32 id = s->SessionId;
        sAtlasMgr->EndSession(id, false);
        h->PSendSysMessage("Session {} ended (all players teleported out).", id);
        return true;
    }

    // The selected player, else the invoker.
    static Player* TargetOrSelf(ChatHandler* h)
    {
        if (Player* t = h->getSelectedPlayer())
            return t;
        return h->GetSession() ? h->GetSession()->GetPlayer() : nullptr;
    }

    // .atlas run: the target's current run and the seed that built it.
    static bool HandleRun(ChatHandler* h)
    {
        Player* target = TargetOrSelf(h);
        Session* s = target ? sAtlasMgr->GetSessionByPlayer(target->GetGUID()) : nullptr;
        if (!s)
        {
            h->SendSysMessage("Atlas: no run for that player.");
            return false;
        }

        h->PSendSysMessage("Atlas: {}", sAtlasMgr->GetSessionStatusString(s));
        h->PSendSysMessage(
            "Atlas: level {}, tier {}, health x{:.2f}, damage x{:.2f} (roles: .atlas scaling {} {} {} {})",
            s->EffectiveLevel, s->Tier, sAtlasMgr->SessionHealthFactor(*s), sAtlasMgr->SessionDamageFactor(*s),
            s->EffectiveLevel, s->Tier, s->Players.size(), s->DifficultyId);

        if (!s->Affixes.empty())
            h->PSendSysMessage("Atlas: affixes [{}]: {}", sAtlasAffixes->Codes(s->Affixes),
                sAtlasAffixes->Describe(s->Affixes, true));

        uint32 depth = 0;
        uint64 descentSeed = 0;
        std::string affixIds;
        if (s->RoguelikeRunId
            && sRoguelikeMgr->GetRunLogInfo(s->RoguelikeRunId, depth, descentSeed, affixIds))
            h->PSendSysMessage("Atlas: Endless descent {} at depth {}, descent seed {:016X}, affixes [{}]",
                s->RoguelikeRunId, depth, descentSeed, affixIds);
        return true;
    }

    // .atlas seed <hex>|clear: the target's next run (an echo, or a whole
    // Endless descent) is built from this seed, as printed by `.atlas run` and
    // in the run log. Same seed, same dungeon and settings: same population.
    static bool HandleSeed(ChatHandler* h, std::string const& value)
    {
        Player* target = TargetOrSelf(h);
        if (!target)
            return false;

        if (value == "clear")
        {
            sAtlasMgr->SetPendingSeed(target->GetGUID(), 0);
            h->PSendSysMessage("Atlas: {}'s next run gets a fresh seed.", target->GetName());
            return true;
        }

        std::string_view text(value);
        if (text.size() > 2 && text[0] == '0' && (text[1] == 'x' || text[1] == 'X'))
            text.remove_prefix(2);

        uint64 seed = 0;
        auto const parsed = std::from_chars(text.data(), text.data() + text.size(), seed, 16);
        if (text.empty() || parsed.ec != std::errc{} || parsed.ptr != text.data() + text.size() || !seed)
        {
            h->SendSysMessage("Atlas: usage .atlas seed <hex seed>|clear (for example 3F2A9C0D11E4B7A5).");
            return false;
        }

        sAtlasMgr->SetPendingSeed(target->GetGUID(), seed);
        h->PSendSysMessage("Atlas: {}'s next run will use seed {:016X}.", target->GetName(), seed);
        return true;
    }

    // .atlas scaling [level] [tier] [party] [difficulty]: what each role gets.
    // Defaults: level 80, tier 1, a party of five, the last difficulty.
    static bool HandleScaling(ChatHandler* h, Optional<uint8> level, Optional<uint32> tier,
        Optional<uint32> party, Optional<uint32> difficulty)
    {
        auto const& diffs = sAtlasConfig->GetDifficulties();
        uint32 const diffId = difficulty.value_or(diffs.empty() ? 0 : diffs.back().Id);
        uint8 const lvl = std::clamp<uint8>(level.value_or(80), 1, 83);

        auto const lines = sAtlasMgr->DescribeRoleScaling(lvl, tier.value_or(1), party.value_or(5), diffId);
        for (std::string const& line : lines)
            h->SendSysMessage(line);
        return true;
    }

    // .atlas affix list: every loaded affix.
    static bool HandleAffixList(ChatHandler* h)
    {
        auto defs = sAtlasAffixes->Definitions();
        h->PSendSysMessage("Atlas: {} affixes (code, name, tiers, values per tier band 1-5/6-10/11-16):",
            defs->size());
        for (AffixDefinition const& d : *defs)
            h->PSendSysMessage("  {} {} — tiers {}-{}, {}/{}/{}{} — {}", d.Code, d.Name, d.MinTier, d.MaxTier,
                d.Values[0], d.Values[1], d.Values[2], d.BotSafe ? "" : ", not bot-safe",
                FormatAffix(d, d.Values[0]));
        return true;
    }

    // .atlas affix next <codes>|clear: the target's next run (an echo, or every
    // depth of an Endless descent) carries these affixes, e.g. "M01 P02 A02".
    static bool HandleAffixNext(ChatHandler* h, Tail codes)
    {
        Player* target = TargetOrSelf(h);
        if (!target)
            return false;

        std::string_view const text = codes;
        if (text.empty() || text == "clear")
        {
            sAtlasAffixes->SetPending(target->GetGUID(), {});
            h->PSendSysMessage("Atlas: {}'s next run rolls its own affixes.", target->GetName());
            return true;
        }

        std::string unknown;
        std::vector<ActiveAffix> affixes = sAtlasAffixes->Parse(text, 1, unknown);
        if (!unknown.empty())
        {
            h->PSendSysMessage("Atlas: unknown affix code(s): {} (see .atlas affix list).", unknown);
            return false;
        }

        std::string const described = sAtlasAffixes->Describe(affixes, false);
        sAtlasAffixes->SetPending(target->GetGUID(), std::move(affixes));
        h->PSendSysMessage("Atlas: {}'s next run carries {} (values follow its tier).", target->GetName(), described);
        return true;
    }

    // .atlas curate <entry> [allowed|blocked|untested] [note]: shows, or sets,
    // whether a creature may appear in runs (blocked ones never do).
    static bool HandleCurate(ChatHandler* h, uint32 entry, Optional<std::string> status, Tail note)
    {
        CreatureTemplate const* t = sObjectMgr->GetCreatureTemplate(entry);
        if (!t)
        {
            h->PSendSysMessage("Atlas: no creature {}.", entry);
            return false;
        }

        if (status)
        {
            bool ok = false;
            AtlasMgr::CurationStatus const parsed = AtlasMgr::ParseCuration(*status, ok);
            if (!ok)
            {
                h->SendSysMessage("Atlas: the status is allowed, blocked or untested.");
                return false;
            }
            std::string text(note.data(), note.size());
            if (text.empty())
                sAtlasMgr->GetCuration(entry, &text);
            sAtlasMgr->SetCuration(entry, parsed, text);
        }

        std::string currentNote;
        AtlasMgr::CurationStatus const current = sAtlasMgr->GetCuration(entry, &currentNote);
        std::string const pools = sAtlasMgr->DescribePoolMembership(entry);
        h->PSendSysMessage("Atlas: {} ({}): {}, AI {}{}{}, pools: {}{}{}", t->Name, entry,
            AtlasMgr::CurationName(current), t->AIName.empty() ? "none" : t->AIName, t->ScriptID ? " + script" : "",
            sAtlasMgr->HasQuestOnlyScript(entry) ? " (quest-only: Atlas AI in runs)" : "",
            pools.empty() ? "none" : pools, currentNote.empty() ? "" : " — ", currentNote);
        return true;
    }

    // .atlas curate spawn <entry> [trash|elite|rare|miniboss|boss|add]: a
    // sample at your level, as a run would spawn it (role stats, AI policy).
    static bool HandleCurateSpawn(ChatHandler* h, uint32 entry, Optional<std::string> role)
    {
        Player* gm = h->GetPlayer();
        if (!gm || !sObjectMgr->GetCreatureTemplate(entry))
        {
            h->PSendSysMessage("Atlas: no creature {}.", entry);
            return false;
        }

        CreatureRole parsed = CreatureRole::Elite;
        if (role)
        {
            parsed = CreatureRole::Count;
            for (uint8 r = 0; r < static_cast<uint8>(CreatureRole::Count); ++r)
            {
                std::string name = RoleName(static_cast<CreatureRole>(r));
                std::transform(name.begin(), name.end(), name.begin(),
                    [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });
                if (name == *role)
                    parsed = static_cast<CreatureRole>(r);
            }
            if (parsed == CreatureRole::Count)
            {
                h->SendSysMessage("Atlas: the role is trash, elite, rare, miniboss, boss or add.");
                return false;
            }
        }

        Creature* c = sAtlasMgr->SpawnCurationSample(gm, entry, parsed);
        if (!c)
        {
            h->PSendSysMessage("Atlas: creature {} could not be spawned here.", entry);
            return false;
        }
        h->PSendSysMessage("Atlas: {} spawned as {} at level {}, {} health, {} AI.", c->GetName(), RoleName(parsed),
            c->GetLevel(), c->GetMaxHealth(), AtlasMgr::KeepsStockAI(c) ? "its own" : "Atlas");
        return true;
    }

    // .atlas map give <tier> [normal|magic|rare] [affix codes]: an Echo Sigil
    // for the target (or you). Without codes, a magic or rare one rolls its own.
    static bool HandleMapGive(ChatHandler* h, uint32 tier, Optional<std::string> rarity, Tail codes)
    {
        Player* target = TargetOrSelf(h);
        if (!target)
            return false;
        if (tier < 1 || tier > MAP_MAX_TIER)
        {
            h->PSendSysMessage("Atlas: the tier is 1 to {}.", MAP_MAX_TIER);
            return false;
        }

        MapRarity r = MapRarity::Normal;
        if (rarity)
        {
            r = ParseMapRarity(*rarity);
            if (r == MapRarity::Count)
            {
                h->SendSysMessage("Atlas: the rarity is normal, magic or rare.");
                return false;
            }
        }

        RunRng rng(RunRng::MakeSeed());
        MapItemData data = sAtlasMgr->RollMapItem(static_cast<uint8>(tier), r, rng);
        std::string_view const text = codes;
        if (!text.empty())
        {
            std::string unknown;
            data.Affixes = sAtlasAffixes->Parse(text, tier, unknown);
            if (!unknown.empty())
            {
                h->PSendSysMessage("Atlas: unknown affix code(s): {} (see .atlas affix list).", unknown);
                return false;
            }
            if (r == MapRarity::Normal && !data.Affixes.empty())
                data.Rarity = data.Affixes.size() > 2 ? MapRarity::Rare : MapRarity::Magic;
        }

        std::string const described = sAtlasMgr->DescribeMapItem(data, false);
        sAtlasMgr->GiveMapItem(target, std::move(data));
        h->PSendSysMessage("Atlas: {} receives an Echo Sigil: {}.", target->GetName(), described);
        return true;
    }

    // .atlas map list: the target's (or your) sigils.
    static bool HandleMapList(ChatHandler* h)
    {
        Player* target = TargetOrSelf(h);
        if (!target)
            return false;

        std::vector<Item*> const items = AtlasMgr::FindMapItems(target);
        h->PSendSysMessage("Atlas: {} carries {} Echo Sigil{}{}", target->GetName(), items.size(),
            items.size() != 1 ? "s" : "", items.empty() ? "." : ":");
        for (Item const* item : items)
            h->PSendSysMessage("  {}", sAtlasMgr->DescribeMapItem(sAtlasMgr->GetMapItemData(item), true));
        if (std::string const run = sAtlasMgr->DescribeMapRun(target->GetGUID()); !run.empty())
            h->PSendSysMessage("Atlas: open map run — {}", run);
        return true;
    }

    // .atlas loot [tier]: the rewards ladder of a map tier (default: the
    // target's open map run, else 1), with a sample piece for the target.
    static bool HandleLoot(ChatHandler* h, Optional<uint32> tier)
    {
        Player* target = h->GetSession() ? TargetOrSelf(h) : nullptr;
        uint32 t = tier.value_or(1);
        if (!tier && target)
            if (Session* s = sAtlasMgr->GetSessionByPlayer(target->GetGUID()); s && s->IsMapRun)
                t = s->Tier;
        if (t < 1 || t > MAP_MAX_TIER)
        {
            h->PSendSysMessage("Atlas: the tier is 1 to {}.", MAP_MAX_TIER);
            return false;
        }

        for (std::string const& line : sAtlasMgr->DescribeLootTier(static_cast<uint8>(t), target))
            h->SendSysMessage(line);

        if (target)
            if (Session* s = sAtlasMgr->GetSessionByPlayer(target->GetGUID()); s && s->IsMapRun)
                h->PSendSysMessage("Atlas: {}'s run: item quantity +{}%, rarity +{}% (caps {}% / {}%).",
                    target->GetName(), AtlasMgr::RunQuantity(*s), AtlasMgr::RunRarity(*s),
                    sAtlasConfig->GetLootMaxQuantity(), sAtlasConfig->GetLootMaxRarity());
        return true;
    }

    // .atlas marker <spell>: puts a spell's aura on the selected creature, or
    // takes it off, to audition affix markers (atlas_affix_marker).
    static bool HandleMarker(ChatHandler* h, uint32 spellId)
    {
        Creature* target = h->getSelectedCreature();
        if (!target)
        {
            h->SendSysMessage("Atlas: select a creature first.");
            return false;
        }
        SpellInfo const* info = sSpellMgr->GetSpellInfo(spellId);
        if (!info)
        {
            h->PSendSysMessage("Atlas: no spell {}.", spellId);
            return false;
        }
        if (target->HasAura(spellId))
        {
            target->RemoveAurasDueToSpell(spellId);
            h->PSendSysMessage("Atlas: {} ({}) taken off {}.", info->SpellName[0], spellId, target->GetName());
        }
        else
        {
            target->AddAura(spellId, target);
            h->PSendSysMessage("Atlas: {} ({}) put on {}.", info->SpellName[0], spellId, target->GetName());
        }
        return true;
    }

    // .atlas light [light] [anchor] [fade ms]: shows you a light as the sky (the
    // in-game test of PLAN 1.6; 0 clears it). Without arguments: your run's
    // look, sent again.
    static bool HandleLight(ChatHandler* h, Optional<uint32> light, Optional<uint32> anchor, Optional<uint32> fade)
    {
        Player* player = h->GetPlayer();
        if (!player)
            return false;

        if (!light)
        {
            Session* s = sAtlasMgr->GetSessionByPlayer(player->GetGUID());
            if (!s)
            {
                h->SendSysMessage("Atlas: you are in no run; give a light id to try one.");
                return false;
            }
            h->PSendSysMessage("Atlas: run look: {}.", sAtlasMgr->DescribeRunLook(*s));
            sAtlasMgr->SendAtmosphere(player, *s);
            return true;
        }

        uint32 const anchorLight = anchor.value_or(sAtlasConfig->GetAtmosphereAnchor());
        AtlasMgr::SendLightOverride(player, anchorLight, *light, fade.value_or(sAtlasConfig->GetAtmosphereFadeMs()));
        h->PSendSysMessage("Atlas: light {} over anchor {} (leaving the map clears it; 0 clears it now).", *light,
            anchorLight);
        return true;
    }

    static bool HandleClearCD(ChatHandler* h)
    {
        Player* invoker = h->GetSession() ? h->GetSession()->GetPlayer() : nullptr;
        if (!invoker)
        {
            h->SendSysMessage("In-game only.");
            return false;
        }

        // If invoker is in a group, clear cooldown for ALL group members
        if (Group* g = invoker->GetGroup())
        {
            uint32 cleared = 0;
            for (GroupReference* ref = g->GetFirstMember(); ref; ref = ref->next())
            {
                if (Player* member = ref->GetSource())
                {
                    sAtlasMgr->ClearCooldown(member->GetGUID());
                    ++cleared;
                }
            }
            h->PSendSysMessage("Cooldown cleared for {} group member(s).", cleared);
            return true;
        }

        // Solo — clear for self or selected player
        Player* t = h->getSelectedPlayer();
        if (!t)
            t = invoker;
        sAtlasMgr->ClearCooldown(t->GetGUID());
        h->PSendSysMessage("Cooldown cleared for {}.", t->GetName());
        return true;
    }
};

void AddSC_atlas_command_script()
{
    new atlas_command_script();
}
