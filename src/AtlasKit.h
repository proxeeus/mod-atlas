/*
 * mod-atlas — AtlasKit.h
 * Generated maps (DESIGN §6.2): a layout built from kit pieces. A kit piece is
 * a stock WMO placed as a gameobject; its sockets are points on its edge where
 * another piece can join, each with the direction it faces out of the piece.
 * Two pieces join when a socket of one sits on a socket of the other, the two
 * facing opposite ways. A cap piece closes a doorway the layout leaves open.
 *
 * Positions are yards, angles radians, a piece's local frame is its WMO's own
 * (the gameobject's orientation turns it about the vertical, counter-clockwise,
 * as the server's collision model does).
 *
 * Pure: no AC dependencies beyond Define.h and RunRng.h, unit-tested.
 */

#ifndef ATLAS_KIT_H
#define ATLAS_KIT_H

#include "Define.h"
#include "RunRng.h"

#include <algorithm>
#include <cmath>
#include <utility>
#include <vector>

namespace Atlas
{

// Overlap of two pieces' boxes that a joint is allowed: the boxes are crude
// (they hold every vertex), and joined pieces meet at their edges.
constexpr float KIT_JOINT_TOLERANCE = 5.0f;

struct KitSocket
{
    float X = 0.0f, Y = 0.0f, Z = 0.0f;
    float Facing = 0.0f;               // outward
};

struct KitPiece
{
    uint32 Entry = 0;                  // gameobject_template (type 14)
    float  EntranceX = 0.0f, EntranceY = 0.0f, EntranceZ = 0.0f;  // where arrivals stand (start piece)
    float  MinX = 0.0f, MinY = 0.0f, MaxX = 0.0f, MaxY = 0.0f;    // the WMO's box, top view
    bool   Cap = false;                // closes open doorways; never grows a layout
    std::vector<KitSocket> Sockets;
};

// Two placed sockets this close (yards) are one joint.
constexpr float KIT_SOCKET_MATCH = 0.5f;

struct KitPlacement
{
    uint32 Entry = 0;
    float  X = 0.0f, Y = 0.0f, Z = 0.0f, O = 0.0f;
};

struct KitPoint
{
    float X = 0.0f, Y = 0.0f, Z = 0.0f, O = 0.0f;
};

struct KitBox
{
    float MinX = 0.0f, MinY = 0.0f, MaxX = 0.0f, MaxY = 0.0f;
};

inline float KitAngle(float a)
{
    float const turn = 2.0f * float(M_PI);
    a = std::fmod(a, turn);
    return a < 0.0f ? a + turn : a;
}

// A point of a placed piece's local frame, in the world; `lo` is a local direction.
inline KitPoint KitToWorld(KitPlacement const& p, float lx, float ly, float lz, float lo = 0.0f)
{
    float const c = std::cos(p.O), s = std::sin(p.O);
    return { p.X + c * lx - s * ly, p.Y + s * lx + c * ly, p.Z + lz, KitAngle(p.O + lo) };
}

// Where a placed piece's socket is, facing out of the piece.
inline KitPoint KitSocketInWorld(KitPlacement const& p, KitSocket const& s)
{
    return KitToWorld(p, s.X, s.Y, s.Z, s.Facing);
}

// The placement of `entry` that puts its socket `s` on `at` (a socket of a
// placed piece, facing out of it), facing the other way.
inline KitPlacement KitJoin(KitPoint const& at, uint32 entry, KitSocket const& s)
{
    float const o = KitAngle(at.O + float(M_PI) - s.Facing);
    float const c = std::cos(o), sn = std::sin(o);
    return { entry, at.X - (c * s.X - sn * s.Y), at.Y - (sn * s.X + c * s.Y), at.Z - s.Z, o };
}

// The world box (top view) of a placed piece: the box around its turned box.
inline KitBox KitWorldBox(KitPiece const& piece, KitPlacement const& p)
{
    KitBox box{ 1e9f, 1e9f, -1e9f, -1e9f };
    for (float lx : { piece.MinX, piece.MaxX })
        for (float ly : { piece.MinY, piece.MaxY })
        {
            KitPoint const w = KitToWorld(p, lx, ly, 0.0f);
            box.MinX = std::min(box.MinX, w.X);
            box.MaxX = std::max(box.MaxX, w.X);
            box.MinY = std::min(box.MinY, w.Y);
            box.MaxY = std::max(box.MaxY, w.Y);
        }
    return box;
}

// Two boxes overlap when they share more than `tolerance` yards both ways.
inline bool KitOverlaps(KitBox const& a, KitBox const& b, float tolerance = KIT_JOINT_TOLERANCE)
{
    float const dx = std::min(a.MaxX, b.MaxX) - std::max(a.MinX, b.MinX);
    float const dy = std::min(a.MaxY, b.MaxY) - std::max(a.MinY, b.MinY);
    return dx > tolerance && dy > tolerance;
}

inline KitPiece const* KitFind(std::vector<KitPiece> const& kit, uint32 entry)
{
    for (KitPiece const& p : kit)
        if (p.Entry == entry)
            return &p;
    return nullptr;
}

// Grows a layout from the start piece at `origin`: takes a free socket at
// random, then a piece and one of its sockets at random, and joins them unless
// the new piece overlaps a placed one; a socket nothing fits stays closed.
// Stops at `maxPieces` or when no socket is free. The first placement is the
// start piece (empty when it is not in the kit).
inline std::vector<KitPlacement> KitBuildLayout(std::vector<KitPiece> const& kit, uint32 startEntry,
    KitPoint const& origin, uint32 maxPieces, RunRng& rng)
{
    std::vector<KitPlacement> placed;
    KitPiece const* start = KitFind(kit, startEntry);
    if (!start || !maxPieces)
        return placed;
    placed.push_back({ startEntry, origin.X, origin.Y, origin.Z, KitAngle(origin.O) });

    std::vector<KitBox> boxes{ KitWorldBox(*start, placed.front()) };
    std::vector<std::pair<std::size_t, std::size_t>> open;   // (placement, socket)
    for (std::size_t s = 0; s < start->Sockets.size(); ++s)
        open.emplace_back(0, s);

    while (placed.size() < maxPieces && !open.empty())
    {
        std::size_t const pick = rng.Index(open.size());
        auto const [from, socket] = open[pick];
        open.erase(open.begin() + pick);
        KitPiece const* owner = KitFind(kit, placed[from].Entry);
        KitPoint const at = KitSocketInWorld(placed[from], owner->Sockets[socket]);

        // Every (piece, socket) that could join here, in a random order.
        std::vector<std::pair<std::size_t, std::size_t>> options;
        for (std::size_t k = 0; k < kit.size(); ++k)
            if (!kit[k].Cap)
                for (std::size_t s = 0; s < kit[k].Sockets.size(); ++s)
                    options.emplace_back(k, s);
        rng.Shuffle(options);

        for (auto const& [k, s] : options)
        {
            KitPlacement const next = KitJoin(at, kit[k].Entry, kit[k].Sockets[s]);
            KitBox const box = KitWorldBox(kit[k], next);
            if (std::any_of(boxes.begin(), boxes.end(), [&](KitBox const& b) { return KitOverlaps(b, box); }))
                continue;
            placed.push_back(next);
            boxes.push_back(box);
            for (std::size_t o = 0; o < kit[k].Sockets.size(); ++o)
                if (o != s)
                    open.emplace_back(placed.size() - 1, o);
            break;
        }
    }
    return placed;
}

// The world sockets of a layout that no other piece's socket meets.
inline std::vector<KitPoint> KitOpenSockets(std::vector<KitPiece> const& kit, std::vector<KitPlacement> const& layout)
{
    std::vector<KitPoint> all;
    for (KitPlacement const& p : layout)
        if (KitPiece const* piece = KitFind(kit, p.Entry))
            for (KitSocket const& s : piece->Sockets)
                all.push_back(KitSocketInWorld(p, s));

    std::vector<KitPoint> open;
    for (std::size_t i = 0; i < all.size(); ++i)
    {
        bool joined = false;
        for (std::size_t j = 0; j < all.size() && !joined; ++j)
            joined = j != i && std::fabs(all[i].X - all[j].X) < KIT_SOCKET_MATCH
                && std::fabs(all[i].Y - all[j].Y) < KIT_SOCKET_MATCH
                && std::fabs(all[i].Z - all[j].Z) < KIT_SOCKET_MATCH;
        if (!joined)
            open.push_back(all[i]);
    }
    return open;
}

// Closes every open doorway of a layout with the kit's first cap piece (its
// first socket). Returns how many caps were added.
inline std::size_t KitCapLayout(std::vector<KitPiece> const& kit, std::vector<KitPlacement>& layout)
{
    KitPiece const* cap = nullptr;
    for (KitPiece const& p : kit)
        if (p.Cap && !p.Sockets.empty())
        {
            cap = &p;
            break;
        }
    if (!cap)
        return 0;
    std::vector<KitPoint> const open = KitOpenSockets(kit, layout);
    for (KitPoint const& at : open)
        layout.push_back(KitJoin(at, cap->Entry, cap->Sockets.front()));
    return open.size();
}

} // namespace Atlas

#endif
