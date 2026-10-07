#pragma once
#include <cstdint>
#include <utility>
#include <vector>

#include "engine/render/layer_style.h"

namespace pittore::render {
namespace detail {

struct Rect {
    int x0 = 0, y0 = 0, x1 = 0, y1 = 0;
    int width() const { return x1 - x0; }
    int height() const { return y1 - y0; }
    bool empty() const { return x1 <= x0 || y1 <= y0; }
};

constexpr std::size_t kMaxStylePixels = 1ull << 25;  // styles on bigger layers are skipped

struct Plane {
    Rect rect;
    std::vector<float> px;  // straight-alpha RGBA, 4 per pixel
    int w() const { return rect.width(); }
    int h() const { return rect.height(); }
};

Plane blankPlane(const Rect& rect);
void blendOver(Plane& dst, const Plane& src, StyleBlend mode, float opacity,
                 const std::vector<float>* mask);
Plane planeFromAlpha(const Rect& rect, const std::vector<float>& alpha, const StyleColor& color);

}  // namespace detail
}  // namespace pittore::render
