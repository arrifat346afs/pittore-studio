// Clean-room naive CMYK <-> RGB. Standard ink-coverage math (not
// copyrightable expression): r = (1-c)(1-k) and its inverse with the k=1
// (pure black) guard so no division by zero can produce NaN.

#include "engine/color/convert.h"

#include <algorithm>

namespace pittore::color {

CmykF rgbToCmyk(RGBAf rgb) noexcept {
    const float r = std::clamp(rgb.r, 0.0f, 1.0f);
    const float g = std::clamp(rgb.g, 0.0f, 1.0f);
    const float b = std::clamp(rgb.b, 0.0f, 1.0f);
    const float k = 1.0f - std::max({r, g, b});
    if (k >= 1.0f) return CmykF{0.0f, 0.0f, 0.0f, 1.0f};
    const float inv = 1.0f / (1.0f - k);
    return CmykF{(1.0f - r - k) * inv, (1.0f - g - k) * inv,
                 (1.0f - b - k) * inv, k};
}

RGBAf cmykToRgb(CmykF ink, float alpha) noexcept {
    const float c = std::clamp(ink.c, 0.0f, 1.0f);
    const float m = std::clamp(ink.m, 0.0f, 1.0f);
    const float y = std::clamp(ink.y, 0.0f, 1.0f);
    const float k = std::clamp(ink.k, 0.0f, 1.0f);
    const float base = 1.0f - k;
    return RGBAf{(1.0f - c) * base, (1.0f - m) * base, (1.0f - y) * base,
                 alpha};
}

}  // namespace pittore::color
