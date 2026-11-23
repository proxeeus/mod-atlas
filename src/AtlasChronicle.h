/*
 * mod-atlas — AtlasChronicle.h
 * Server side of the Chronicle window (client addon AtlasChronicle).
 *
 * The addon whispers its own character with addon messages (prefix "ATLAS");
 * atlas_chronicle_script swallows them and answers the same way. Players
 * without the addon keep the Chronicler's chat records.
 */

#ifndef ATLAS_CHRONICLE_H
#define ATLAS_CHRONICLE_H

#include "ObjectGuid.h"

#include <array>
#include <string>
#include <vector>

class Player;

namespace Atlas::Chronicle
{
    // True once this character's addon has said hello this session.
    bool HasAddon(ObjectGuid guid);

    // Asks the addon to open the Chronicle window.
    void Open(Player* player);

    // The run HUD (PLAN 1.7): pushed to players with the addon.
    struct RunHud
    {
        uint8       Kind      = 0;   // 0 echo, 1 map, 2 Endless depth
        uint32      Tier      = 1;
        uint8       Rarity    = 0;   // maps: 0 normal, 1 magic, 2 rare
        int32       Portals   = -1;  // maps: rifts left; -1 none
        uint32      Forces    = 0;   // the master's bar, 0-100
        uint8       Master    = 0;   // 0 bar filling, 1 called / walking, 2 slain, 3 waits at its spot
        uint32      Slain     = 0;
        uint32      Total     = 0;
        std::string Dungeon;
        std::string Theme;
    };
    // Header only, or with the affix rows (code, name, text).
    void SendRun(Player* player, RunHud const& hud, std::vector<std::array<std::string, 3>> const* affixes);
    void SendRunEnded(Player* player);
}

#endif
