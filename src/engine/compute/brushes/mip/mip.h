#pragma once
// Box-filtered mip pyramid over placed-layer native pixels.
// Split from engine/compute/paint.h.
#include <cstdint>
#include <vector>

#include "engine/core/pixel.h"

namespace pittore::compute {

struct MipPyramid {
    struct Level {
        std::uint32_t w = 0, h = 0;
        std::vector<RGBAf> px;
    };
    std::vector<Level> levels;
};

MipPyramid build_pyramid_host(const RGBAf* src, std::uint32_t sw,
                              std::uint32_t sh);

void pyramid_mark_dirty_host(MipPyramid& pyr, int x0, int y0, int x1, int y1);

RGBAf sample_layer_mip_host(const MipPyramid& pyr, const RGBAf* src,
                            std::uint32_t sw, std::uint32_t sh, double docX,
                            double docY, double ox, double oy, double sx,
                            double sy);

}  // namespace pittore::compute
