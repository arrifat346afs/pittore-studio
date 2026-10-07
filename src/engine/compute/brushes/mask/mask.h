#pragma once
// Masked second tip: a bitmap sampled per dab and combined into the primary
// coverage (dual-brush texturing). Standalone by design (no stamp header
// dependency): callers fill the bitmap view from their own tip storage.
// The mask's longest side spans the dab diameter times sizeRatio, rotated
// by angleDeg. Combine modes: 0 multiply, 1 subtract, 2 darken (minimum),
// 3 lighten (maximum).
#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace pittore::compute {

struct MaskBitmap {
    const float* alpha = nullptr;  // w*h coverage in [0,1], row-first
    std::uint32_t w = 0, h = 0;

    bool valid() const {
        return alpha != nullptr && w > 0 && h > 0 && w <= 1024 && h <= 1024;
    }
};

struct MaskTip {
    MaskBitmap bitmap;
    float sizeRatio = 1.0f;  // mask diameter vs dab diameter
    float angleDeg = 0.0f;
    int mode = 0;

    bool valid() const {
        return bitmap.valid() && sizeRatio > 0.0f && std::isfinite(sizeRatio);
    }
};

inline float sample_mask_tip(const MaskTip* m, float dx, float dy,
                             float radius) {
    const std::uint32_t tw = m->bitmap.w, th = m->bitmap.h;
    const float rad = m->angleDeg * 3.14159265358979323846f / 180.0f;
    const float cosA = std::cos(rad), sinA = std::sin(rad);
    const float longest = float(tw > th ? tw : th);
    const float scale = longest / (2.0f * radius * m->sizeRatio);
    const float cxTip = (float(tw) - 1.0f) * 0.5f;
    const float cyTip = (float(th) - 1.0f) * 0.5f;
    const float u = (dx * cosA - dy * sinA) * scale + cxTip;
    const float v = (dx * sinA + dy * cosA) * scale + cyTip;
    if (u < 0.0f || v < 0.0f || u > float(tw) - 1.0f ||
        v > float(th) - 1.0f)
        return 0.0f;
    const std::uint32_t x0 = static_cast<std::uint32_t>(u);
    const std::uint32_t y0 = static_cast<std::uint32_t>(v);
    const std::uint32_t x1 = x0 + 1 < tw ? x0 + 1 : x0;
    const std::uint32_t y1 = y0 + 1 < th ? y0 + 1 : y0;
    const float fx = u - float(x0), fy = v - float(y0);
    const float* p = m->bitmap.alpha;
    const float a = p[std::size_t(y0) * tw + x0];
    const float b = p[std::size_t(y0) * tw + x1];
    const float c = p[std::size_t(y1) * tw + x0];
    const float d = p[std::size_t(y1) * tw + x1];
    return std::clamp(a + (b - a) * fx + (c - a) * fy + (a - b - c + d) * fx * fy,
                      0.0f, 1.0f);
}

// Combine primary coverage with the mask sample.
inline float combine_mask(float cov, const MaskTip* m, float dx, float dy,
                          float radius) {
    if (m == nullptr || !m->valid()) return cov;
    const float mask = sample_mask_tip(m, dx, dy, radius);
    switch (m->mode) {
        case 1:
            return std::max(0.0f, cov - mask);
        case 2:
            return std::min(cov, mask);
        case 3:
            return std::max(cov, mask);
        default:
            return cov * mask;
    }
}

}  // namespace pittore::compute
