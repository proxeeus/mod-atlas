#pragma once
#include "Define.h"
#include <random>
inline uint32 rand32() { static std::mt19937 g(12345); return g(); }
inline uint32 urand(uint32 lo, uint32 hi) { return lo + rand32() % (hi - lo + 1); }
