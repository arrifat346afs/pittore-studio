#include "engine/render/layer_style.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <utility>
#include <vector>

#include "engine/render/style/shared/style_plane.h"
#include "engine/render/style/blend/style_blend.h"
#include "engine/render/style/blur/style_blur.h"
#include "engine/render/style/sdf/style_sdf.h"
#include "engine/render/style/fx/style_fx.h"

namespace pittore::render {
namespace detail {


float sepBlend(StyleBlend m, float b, float s) {
    auto screen = [](float x, float y) { return x + y - x * y; };
    auto hardLight = [&](float x, float y) {
        return y <= 0.5f ? x * 2.0f * y : screen(x, 2.0f * y - 1.0f);
    };
    float v;
    switch (m) {
        case StyleBlend::Darken: v = std::min(b, s); break;
        case StyleBlend::Multiply: v = b * s; break;
        case StyleBlend::ColorBurn:
            v = b >= 1.0f ? 1.0f : (s <= 0.0f ? 0.0f : 1.0f - std::min((1.0f - b) / s, 1.0f));
            break;
        case StyleBlend::Lighten: v = std::max(b, s); break;
        case StyleBlend::Screen: v = screen(b, s); break;
        case StyleBlend::ColorDodge:
            v = b <= 0.0f ? 0.0f : (s >= 1.0f ? 1.0f : std::min(b / (1.0f - s), 1.0f));
            break;
        case StyleBlend::Add: v = b + s; break;
        case StyleBlend::Overlay: v = hardLight(s, b); break;
        case StyleBlend::HardLight: v = hardLight(b, s); break;
        case StyleBlend::SoftLight: {
            if (s <= 0.5f) {
                v = b - (1.0f - 2.0f * s) * b * (1.0f - b);
            } else {
                const float d = b <= 0.25f ? ((16.0f * b - 12.0f) * b + 4.0f) * b
                                           : std::sqrt(std::max(0.0f, b));
                v = b + (2.0f * s - 1.0f) * (d - b);
            }
            break;
        }
        case StyleBlend::Difference: v = std::abs(b - s); break;
        case StyleBlend::Exclusion: v = b + s - 2.0f * b * s; break;
        case StyleBlend::Subtract: v = b - s; break;
        case StyleBlend::Normal:
        default: v = s; break;
    }
    return std::clamp(v, 0.0f, 1.0f);
}


StyleColor blendPixel(StyleBlend mode, const StyleColor& top, const StyleColor& bottom) {
    const float as = top.a, ab = bottom.a;
    if (as <= 0.0f) return bottom;
    const float ao = as + ab * (1.0f - as);
    if (ao <= 0.0f) return StyleColor{0.0f, 0.0f, 0.0f, 0.0f};
    auto comp = [&](float cs, float cb) {
        const float bl = sepBlend(mode, cb, cs);
        return (cs * as * (1.0f - ab) + bl * as * ab + cb * ab * (1.0f - as)) / ao;
    };
    return StyleColor{comp(top.r, bottom.r), comp(top.g, bottom.g), comp(top.b, bottom.b), ao};
}

}  // namespace detail
}  // namespace pittore::render
