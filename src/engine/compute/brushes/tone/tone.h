#pragma once
// Dodge / Burn / Sponge tonal brushes.
// Split from engine/compute/paint.h.
#include <cstdint>

#include "engine/compute/brushes/selection_mask/selection_mask.h"
#include "engine/core/pixel.h"

namespace pittore::compute {

enum class ToneOp : int {
    Dodge = 0,
    Burn = 1,
    Desaturate = 2,
    Saturate = 3,
};

void tone_dab_host(RGBAf* dst, std::uint32_t w, std::uint32_t h, float cx,
                   float cy, float radius, float hardness, float amount,
                   ToneOp op, int range, bool protect_tones, bool vibrance,
                   const SelectionMask* selection = nullptr);

bool tone_stroke_dab_host(const RGBAf* pre, RGBAf* dst, float* coverage,
                          std::uint32_t w, std::uint32_t h, float cx, float cy,
                          float radius, float hardness, float amount, ToneOp op,
                          int range, bool protect_tones, bool vibrance,
                          const SelectionMask* selection, int* bbox);

}  // namespace pittore::compute
