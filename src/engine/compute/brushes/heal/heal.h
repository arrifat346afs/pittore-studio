#pragma once
// Spot Healing Brush dab (ContentAware / CreateTexture / Proximity).
// Split from engine/compute/paint.h.
#include <cstdint>

#include "engine/compute/brushes/selection_mask/selection_mask.h"
#include "engine/core/pixel.h"

namespace pittore::compute {

enum class HealType : int {
    ContentAware = 0,
    CreateTexture = 1,
    Proximity = 2,
};

bool spot_heal_host(RGBAf* dst, std::uint32_t w, std::uint32_t h, float cx,
                     float cy, float radius, float hardness,
                     const RGBAf* src, HealType type, int diffusion,
                     int* bbox, const SelectionMask* selection = nullptr);

// Donor translate for the Healing Brush: donor[i] = base[i - offset]
// (offset in layer px, clamped replicate edges). One full-frame pass per
// stroke/press — amortized over the stroke's dabs. parallel_rows over
// disjoint dst rows.
void translate_heal_donor_host(const RGBAf* base, RGBAf* donor,
                               std::uint32_t w, std::uint32_t h, float ox,
                               float oy);

// Red-eye fix in `box` (layer pixels): red-dominant texels (r > 0.25,
// r > 1.4g, r > 1.4b) desaturate to luma and scale by (1 - 0.75*darken).
// 1px feather at the box rim. RGB only, alpha untouched. parallel_rows.
bool redeye_fix_host(RGBAf* dst, std::uint32_t w, std::uint32_t h,
                     int x0, int y0, int x1, int y1, float darken,
                     int* bbox);

}  // namespace pittore::compute
