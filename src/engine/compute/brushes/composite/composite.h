#pragma once
// Incremental region composite + placed-layer sampler.
// Split from engine/compute/paint.h.
#include <cstdint>

#include "engine/compute/backend.h"
#include "engine/core/pixel.h"

namespace pittore::compute {

void composite_region_host(RGBAf* bottom, const RGBAf* top, std::uint32_t w,
                           std::uint32_t x0, std::uint32_t y0,
                           std::uint32_t x1, std::uint32_t y1,
                           BlendMode mode);

void composite_placed_host(RGBAf* bottom, const RGBAf* src, std::uint32_t sw,
                           std::uint32_t sh, double ox, double oy, double sx,
                           double sy, std::uint32_t w, std::uint32_t x0,
                           std::uint32_t y0, std::uint32_t x1, std::uint32_t y1,
                           float alpha_fold, BlendMode mode);

RGBAf sample_placed_host(const RGBAf* src, std::uint32_t sw,
                         std::uint32_t sh, double docX, double docY, double ox,
                         double oy, double sx, double sy);

}  // namespace pittore::compute
