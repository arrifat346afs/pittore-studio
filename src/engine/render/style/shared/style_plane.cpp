#include "engine/render/layer_style.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <utility>
#include <vector>

#include "engine/core/parallel.h"
#include "engine/render/style/shared/style_plane.h"
#include "engine/render/style/blend/style_blend.h"
#include "engine/render/style/blur/style_blur.h"
#include "engine/render/style/sdf/style_sdf.h"
#include "engine/render/style/fx/style_fx.h"

namespace pittore::render {
namespace detail {


Plane blankPlane(const Rect& rect) {
    Plane p;
    p.rect = rect;
    const std::size_t n = std::size_t(std::max(0, rect.width())) *
                          std::size_t(std::max(0, rect.height()));
    p.px.assign(n * 4, 0.0f);
    return p;
}


// `dst = src over dst`, scaled by `opacity` and optionally by a mask.
void blendOver(Plane& dst, const Plane& src, StyleBlend mode, float opacity,
                 const std::vector<float>* mask) {
    const std::size_t n = dst.px.size() / 4;
    // Each element reads only its own slots and writes only its own, so the
    // blend splits over contiguous ranges bit-identically.
    pittore::core::parallel_for(
        static_cast<std::uint32_t>(n), 4096, [&](std::uint32_t i0, std::uint32_t i1) {
            for (std::size_t i = i0; i < i1; ++i) {
                float a = src.px[i * 4 + 3] * opacity;
                if (mask) a *= (*mask)[i];
                if (a <= 0.0f) continue;
                const StyleColor top{src.px[i * 4], src.px[i * 4 + 1], src.px[i * 4 + 2], a};
                const StyleColor bottom{dst.px[i * 4], dst.px[i * 4 + 1], dst.px[i * 4 + 2],
                                     dst.px[i * 4 + 3]};
                const StyleColor out = blendPixel(mode, top, bottom);
                dst.px[i * 4] = out.r;
                dst.px[i * 4 + 1] = out.g;
                dst.px[i * 4 + 2] = out.b;
                dst.px[i * 4 + 3] = out.a;
            }
        });
}


Plane planeFromAlpha(const Rect& rect, const std::vector<float>& alpha, const StyleColor& color) {
    Plane p = blankPlane(rect);
    for (std::size_t i = 0; i < alpha.size(); ++i) {
        p.px[i * 4] = color.r;
        p.px[i * 4 + 1] = color.g;
        p.px[i * 4 + 2] = color.b;
        p.px[i * 4 + 3] = alpha[i] * color.a;
    }
    return p;
}

}  // namespace detail
}  // namespace pittore::render
