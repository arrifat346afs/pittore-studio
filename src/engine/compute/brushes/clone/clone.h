#pragma once
// Clone Stamp dab with stroke-wide flow accumulation.
// Split from engine/compute/paint.h.
#include <cstdint>

#include "engine/compute/brushes/selection_mask/selection_mask.h"
#include "engine/core/pixel.h"

namespace pittore::compute {

bool clone_stamp_dab_host(const RGBAf* pre, RGBAf* dst, float* coverage,
                          std::uint32_t w, std::uint32_t h, float cx,
                          float cy, float radius, float hardness,
                          const RGBAf* src, std::uint32_t sw,
                          std::uint32_t sh, float offX, float offY,
                          float opacity, float flow, int* bbox,
                          const SelectionMask* selection = nullptr);

}  // namespace pittore::compute
