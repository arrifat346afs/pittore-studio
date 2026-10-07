#pragma once
// User brush-tip file loaders: original parsers for the common tip formats,
// written from their publicly documented layouts. No third-party code is
// used or bundled, and no third-party brush content ships with the app:
// these parsers only read files the user imports themselves.
//
// Supported, best-effort:
//   .gbr  v1/v2 grayscale + RGBA (float-depth variants rejected)
//   .gih  text header + concatenated tip cells (selection: incremental and
//         random; other modes fall back to incremental)
//   .abr  v1/v2 sampled tips + v6 tagged-block sampled items (computed tips
//         are skipped; per-dab dynamics sections are reserved for later)
//   .png decoding and preset-XML mapping live in the UI layer (Qt), not here.
//
// Coverage convention is a paint mask: 1 = full paint.
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "engine/compute/brushes/stamp/stamp.h"

namespace pittore::compute::brushload {

struct LoadedTip {
    StampTip tip;
    std::string name;
    // True when the name came from the file itself (v2 brush names);
    // otherwise it is a synthesized fallback ("tip-N").
    bool nameAuthored = false;
    bool ok = false;
    std::string error;
};

struct LoadedHose {
    std::vector<LoadedTip> cells;
    std::string name;
    int step = 20;                 // spacing % from the text header
    std::string selection;         // "incremental", "random", ... (raw)
    bool ok = false;
    std::string error;
};

struct LoadedAbr {
    std::vector<LoadedTip> tips;
    // Computed (procedural) tips have no public binary layout, so they
    // cannot be sampled safely; they are counted here so importers can
    // report them as auto-tip approximations instead of dropping them
    // silently. Descriptor-block parsing needs a real-file corpus to
    // validate against — see the note in loaders.cpp.
    int skippedComputed = 0;
    bool ok = false;
    std::string error;
};

LoadedTip load_gbr(const std::uint8_t* data, std::size_t n);
LoadedHose load_gih(const std::uint8_t* data, std::size_t n);
LoadedAbr load_abr(const std::uint8_t* data, std::size_t n);

// Hose cell picker shared by the stroke path: incremental cycles, random
// picks uniformly, angular follows the stroke direction, velocity and
// pressure index by their (0..1) level. Every other mode falls back to
// incremental.
std::size_t hose_cell_index(const LoadedHose& hose, std::size_t dabNumber,
                            std::uint64_t seed, double directionRad = 0.0,
                            double pressure = 0.5, double speed01 = 0.0);

}  // namespace pittore::compute::brushload
