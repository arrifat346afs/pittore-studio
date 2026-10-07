#pragma once
// Mixer Brush dab: wet paint mixing (clean-room concepts, no third-party
// formulation). The brush holds `loaded` paint: each texel mixes the load
// with the canvas sample by `mix`, paints the result at `flow`, then the
// load relaxes toward the canvas by `wet`. Serial bbox loop — the load
// update feeds back per texel, so rows are not independent. Deterministic
// for a fixed dab sequence.
#include <algorithm>
#include <cmath>
#include <cstdint>

#include "engine/compute/brushes/selection_mask/selection_mask.h"
#include "engine/core/pixel.h"

namespace pittore::compute {

inline bool mixer_dab_host(RGBAf* dst, const RGBAf* sample, std::uint32_t w,
                           std::uint32_t h, float cx, float cy, float radius,
                           float hardness, float flow, float mix, float wet,
                           RGBAf& loaded, int* bboxOut,
                           const SelectionMask* selection = nullptr) {
    if (!dst || !sample || w == 0 || h == 0 || radius <= 0.0f) return false;
    const float fl = std::clamp(flow, 0.0f, 1.0f);
    const float mx = std::clamp(mix, 0.0f, 1.0f);
    const float wt = std::clamp(wet, 0.0f, 1.0f);
    if (!(fl > 0.0f)) return false;
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
    for (int y = y0; y <= y1; ++y) {
        for (int x = x0; x <= x1; ++x) {
            const float dx = float(x) + 0.5f - cx;
            const float dy = float(y) + 0.5f - cy;
            const float t = std::sqrt(dx * dx + dy * dy) * inv_r;
            if (t >= 1.0f) continue;
            float m = 1.0f;
            if (t > hard) {
                const float span = std::max(1.0f - hard, 1e-4f);
                m = (1.0f - t) / span;
            }
            m *= fl;
            if (!(m > 0.0f)) continue;
            if (selection) {
                m *= selection->coverage(x, y);
                if (!(m > 0.0f)) continue;
            }
            const std::size_t i = std::size_t(y) * w + x;
            const RGBAf& cs = sample[i];
            // Mix the load with the canvas sample, paint, relax the load.
            RGBAf mixed{loaded.r + (cs.r - loaded.r) * mx,
                        loaded.g + (cs.g - loaded.g) * mx,
                        loaded.b + (cs.b - loaded.b) * mx,
                        loaded.a + (cs.a - loaded.a) * mx};
            RGBAf& o = dst[i];
            const float inv_a = 1.0f - m;
            const float out_a = m * mixed.a + o.a * inv_a;
            if (out_a > 0.0f) {
                o.r = (m * mixed.r * mixed.a + o.r * o.a * inv_a) / out_a;
                o.g = (m * mixed.g * mixed.a + o.g * o.a * inv_a) / out_a;
                o.b = (m * mixed.b * mixed.a + o.b * o.a * inv_a) / out_a;
                o.a = out_a;
                touched = true;
            }
            loaded.r += (cs.r - loaded.r) * wt * m;
            loaded.g += (cs.g - loaded.g) * wt * m;
            loaded.b += (cs.b - loaded.b) * wt * m;
            loaded.a += (cs.a - loaded.a) * wt * m;
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
