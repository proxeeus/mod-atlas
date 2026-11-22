/*
 * mod-atlas — RunRng.h
 * Reproducible random numbers for everything that shapes a run (DESIGN C8).
 *
 * The same seed gives the same draws on every build and platform, so a run can
 * be replayed from its logged seed. AC's Random.h helpers are global and can't
 * be seeded per run, and <random>'s distributions are implementation-defined,
 * so this carries its own engine (xoshiro256**, seeded through splitmix64) and
 * its own unbiased range mapping. Use it only for run layout choices; anything
 * else keeps using Random.h.
 */

#ifndef ATLAS_RUN_RNG_H
#define ATLAS_RUN_RNG_H

#include "Define.h"
#include "Random.h"

#include <cstddef>
#include <cstdint>
#include <utility>
#include <vector>

namespace Atlas
{

class RunRng
{
public:
    explicit RunRng(uint64 seed = 0) { Seed(seed); }

    void Seed(uint64 seed)
    {
        _seed = seed;
        uint64 x = seed;
        for (uint64& word : _state)
            word = SplitMix64(x);
    }

    uint64 GetSeed() const { return _seed; }

    uint64 Next()
    {
        uint64 const result = Rotl(_state[1] * 5, 7) * 9;
        uint64 const t = _state[1] << 17;
        _state[2] ^= _state[0];
        _state[3] ^= _state[1];
        _state[1] ^= _state[2];
        _state[0] ^= _state[3];
        _state[2] ^= t;
        _state[3] = Rotl(_state[3], 45);
        return result;
    }

    // Uniform in [lo, hi], both included. Rejection sampling keeps it unbiased.
    uint32 Range(uint32 lo, uint32 hi)
    {
        if (hi <= lo)
            return lo;
        uint64 const span = uint64(hi) - lo + 1;
        uint64 const limit = UINT64_MAX - UINT64_MAX % span;
        uint64 draw;
        do
            draw = Next();
        while (draw >= limit);
        return lo + uint32(draw % span);
    }

    // True with a chance of percent in 100 (0 never, 100 or more always).
    bool Chance(uint32 percent) { return Range(1, 100) <= percent; }

    // An index into a container of the given size (size must be > 0).
    std::size_t Index(std::size_t size) { return Range(0, uint32(size - 1)); }

    template<typename T>
    T const& Pick(std::vector<T> const& items) { return items[Index(items.size())]; }

    // Fisher-Yates, back to front.
    template<typename T>
    void Shuffle(std::vector<T>& items)
    {
        for (std::size_t i = items.size(); i > 1; --i)
            std::swap(items[i - 1], items[Index(i)]);
    }

    // A fresh, non-zero seed for a new run.
    static uint64 MakeSeed()
    {
        uint64 const seed = (uint64(rand32()) << 32) | rand32();
        return seed ? seed : 1;
    }

private:
    static uint64 SplitMix64(uint64& x)
    {
        uint64 z = (x += 0x9E3779B97F4A7C15ULL);
        z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ULL;
        z = (z ^ (z >> 27)) * 0x94D049BB133111EBULL;
        return z ^ (z >> 31);
    }

    static uint64 Rotl(uint64 x, int k) { return (x << k) | (x >> (64 - k)); }

    uint64 _seed = 0;
    uint64 _state[4] = {};
};

} // namespace Atlas

#endif
