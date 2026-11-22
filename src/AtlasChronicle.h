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

class Player;

namespace Atlas::Chronicle
{
    // True once this character's addon has said hello this session.
    bool HasAddon(ObjectGuid guid);

    // Asks the addon to open the Chronicle window.
    void Open(Player* player);
}

#endif
