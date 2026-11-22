/*
 * mod-atlas — AtlasInternal.h
 * Helpers shared by the files AtlasMgr is split across. Not for other modules.
 */

#ifndef ATLAS_INTERNAL_H
#define ATLAS_INTERNAL_H

#include "Random.h"

namespace Atlas
{

// RNG: AC's helpers (Random.h), thread-safe. RandInt keeps the call sites'
// integer types; every range used here fits in uint32. Run layout choices use
// the run's seeded RunRng instead (RunRng.h); this is for everything else.
template<typename T>
inline T RandInt(T lo, T hi)
{
    return static_cast<T>(urand(static_cast<uint32>(lo), static_cast<uint32>(hi)));
}

} // namespace Atlas

#endif
