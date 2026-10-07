#pragma once
// Adjustment Brush dab: apply one adjustment under the dab mask, scaled by
// `strength` (opacity × flow). Only LUT-free kinds (Brightness/Contrast,
// Exposure, Hue/Saturation) — Curves/Levels tables belong to the dialog
// path. Shared adjust::apply math (CPU + GPU identical by construction).
// parallel_rows over circle rows (disjoint dst rows, read-only params).
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>

#include "engine/compute/adjust.h"
#include "engine/compute/brushes/selection_mask/selection_mask.h"
#include "engine/core/pixel.h"

namespace pittore::compute {

inline bool adjustment_dab_host(RGBAf* dst, std::uint32_t w, std::uint32_t h,
                                float cx, float cy, float radius,
                                float hardness, float strength,
                                AdjustmentKind kind, const float* p,
                                int* bboxOut,
                                const SelectionMask* selection = nullptr) {
    if (!dst || !p || radius <= 0.0f || w == 0 || h == 0) return false;
    const float st = std::clamp(strength, 0.0f, 1.0f);
    if (!(st > 0.0f)) return false;
    if (kind != AdjustmentKind::BrightnessContrast &&
        kind != AdjustmentKind::Exposure &&
        kind != AdjustmentKind::HueSaturation)
        return false;
    const float hard = std::clamp(hardness, 0.0f, 1.0f);
    const int y0 = std::max(0, static_cast<int>(std::floor(cy - radius)));
    const int y1 = std::min(static_cast<int>(h) - 1,
                            static_cast<int>(std::ceil(cy + radius)));
    const int x0 = std::max(0, static_cast<int>(std::floor(cx - radius)));
    const int x1 = std::min(static_cast<int>(w) - 1,
                            static_cast<int>(std::ceil(cx + radius)));
    if (y1 < y0 || x1 < x0) return false;
    const float inv_r = 1.0f / radius;
    bool touched = false;
    // Small bbox: serial loop avoids thread-spawn cost dominating µs dabs
    // (same rationale as the history dab core).
    for (int y = y0; y <= y1; ++y) {
        const float dy = static_cast<float>(y) + 0.5f - cy;
        std::size_t idx = std::size_t(y) * w + std::size_t(x0);
        for (int x = x0; x <= x1; ++x, ++idx) {
            const float dx = static_cast<float>(x) + 0.5f - cx;
            const float t = std::sqrt(dx * dx + dy * dy) * inv_r;
            if (t >= 1.0f) continue;
            float m = 1.0f;
            if (t > hard) {
                const float span = std::max(1.0f - hard, 1e-4f);
                m = (1.0f - t) / span;
            }
            m *= st;
            if (!(m > 0.0f)) continue;
            if (selection) {
                m *= selection->coverage(x, y);
                if (!(m > 0.0f)) continue;
            }
            RGBAf& o = dst[idx];
            float ar = 0, ag = 0, ab = 0;
            adjust::apply(kind, p, nullptr, o.r, o.g, o.b, ar, ag, ab);
            const RGBAf before = o;
            o.r = ar * m + o.r * (1.0f - m);
            o.g = ag * m + o.g * (1.0f - m);
            o.b = ab * m + o.b * (1.0f - m);
            if (std::memcmp(&before, &o, sizeof(RGBAf)) != 0) touched = true;
        }
    }
    if (bboxOut) {
        bboxOut[0] = x0;
        bboxOut[1] = y0;
        bboxOut[2] = x1 + 1;
        bboxOut[3] = y1 + 1;
    }
    return touched;
}

}  // namespace pittore::compute
