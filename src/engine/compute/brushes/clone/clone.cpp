#include "engine/compute/brushes/clone/clone.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>

#include "engine/core/pixel.h"

namespace pittore::compute {

bool clone_stamp_dab_host(const RGBAf* pre, RGBAf* dst, float* coverage,
                          std::uint32_t w, std::uint32_t h, float cx,
                          float cy, float radius, float hardness,
                          const RGBAf* src, std::uint32_t sw,
                          std::uint32_t sh, float offX, float offY,
                          float opacity, float flow, int* bbox,
                          const SelectionMask* selection) {
    if (!pre || !dst || !coverage || !src || radius <= 0.0f || w == 0 ||
        h == 0 || sw == 0 || sh == 0)
        return false;
    const float hard = std::clamp(hardness, 0.0f, 1.0f);
    const float op = std::clamp(opacity, 0.0f, 1.0f);
    const float fl = std::clamp(flow, 0.0f, 1.0f);
    if (op <= 0.0f || fl <= 0.0f) return false;

    // Identical dab geometry to paint_dab_host (bbox, hardness falloff,
    // clip) so the stamp feels like every other brush.
    const int y0 = std::max(0, static_cast<int>(std::floor(cy - radius)));
    const int y1 = std::min(static_cast<int>(h) - 1,
                            static_cast<int>(std::ceil(cy + radius)));
    const int x0 = std::max(0, static_cast<int>(std::floor(cx - radius)));
    const int x1 = std::min(static_cast<int>(w) - 1,
                            static_cast<int>(std::ceil(cx + radius)));

    const float inv_r = 1.0f / radius;
    const float soft_span = std::max(1.0f - hard, 1e-4f);

    // Raise coverage (max) under the dab; remember which pixels grew.
    int bx0 = static_cast<int>(w), by0 = static_cast<int>(h), bx1 = -1, by1 = -1;
    for (int y = y0; y <= y1; ++y) {
        const float dy = static_cast<float>(y) + 0.5f - cy;
        std::size_t idx = static_cast<std::size_t>(y) * w + static_cast<std::size_t>(x0);
        for (int x = x0; x <= x1; ++x, ++idx) {
            const float dx = static_cast<float>(x) + 0.5f - cx;
            const float t = std::sqrt(dx * dx + dy * dy) * inv_r;
            if (t >= 1.0f) continue;
            float cov = 1.0f;
            if (t > 1.0f - soft_span) cov = (1.0f - t) / soft_span;
            if (selection) cov *= selection->coverage(x, y);
            if (cov <= coverage[idx]) continue;
            coverage[idx] = cov;
            if (bx1 < 0) {
                bx0 = bx1 = x;
                by0 = by1 = y;
            } else {
                bx0 = std::min(bx0, x);
                by0 = std::min(by0, y);
                bx1 = std::max(bx1, x);
                by1 = std::max(by1, y);
            }
        }
    }
    if (bx1 < 0) return false;

    // Re-render the touched pixels from the pre-stroke image: the source is
    // sampled at the dab position plus the stroke offset (nearest texel —
    // cloning copies exact pixels, never filtered), mixed in at the
    // accumulated coverage capped by opacity, scaled by flow. Pixels already
    // at the same coverage recompute identically, so re-rendering into the
    // overlap is harmless. Only pixels that actually move count toward the
    // changed rect, so a dab over a transparent (or identical) source
    // fabricates no undo step.
    int rx0 = 0, ry0 = 0, rx1 = 0, ry1 = 0;
    bool any = false;
    for (int y = by0; y <= by1; ++y) {
        std::size_t idx = static_cast<std::size_t>(y) * w + static_cast<std::size_t>(bx0);
        for (int x = bx0; x <= bx1; ++x, ++idx) {
            const float c = coverage[idx];
            if (c <= 0.0f) continue;
            const int sx = static_cast<int>(std::floor(x + offX));
            const int sy = static_cast<int>(std::floor(y + offY));
            if (sx < 0 || sy < 0 || sx >= static_cast<int>(sw) ||
                sy >= static_cast<int>(sh))
                continue;
            const RGBAf& s = src[std::size_t(sy) * sw + sx];
            if (s.a <= 0.0f) continue;
            const float k = std::min(c, op) * fl;
            if (k <= 0.0f) continue;
            const RGBAf& p = pre[idx];
            RGBAf& d = dst[idx];
            const RGBAf before = d;
            // Straight-alpha source-over with the dab strength folded into
            // the source's effective alpha (unlike paint_dab_host, whose
            // colour is always opaque, the clone source carries real alpha).
            const float ka = k * s.a;
            const float out_a = ka + p.a * (1.0f - ka);
            if (out_a <= 0.0f) continue;
            d.r = (ka * s.r + p.r * p.a * (1.0f - ka)) / out_a;
            d.g = (ka * s.g + p.g * p.a * (1.0f - ka)) / out_a;
            d.b = (ka * s.b + p.b * p.a * (1.0f - ka)) / out_a;
            d.a = out_a;
            if (std::fabs(d.r - before.r) < 1e-6f &&
                std::fabs(d.g - before.g) < 1e-6f &&
                std::fabs(d.b - before.b) < 1e-6f &&
                std::fabs(d.a - before.a) < 1e-6f)
                continue;
            if (!any) {
                rx0 = rx1 = x;
                ry0 = ry1 = y;
                any = true;
            } else {
                rx0 = std::min(rx0, x);
                ry0 = std::min(ry0, y);
                rx1 = std::max(rx1, x);
                ry1 = std::max(ry1, y);
            }
        }
    }

    if (!any) return false;
    if (bbox) {
        bbox[0] = rx0;
        bbox[1] = ry0;
        bbox[2] = rx1 + 1;  // half-open
        bbox[3] = ry1 + 1;
    }
    return true;
}

// Straight-alpha blend of one pixel with a full layer blend mode — the exact
// math the CPU/GPU composites apply; shared by the region composite and the
// fused placed-layer composite. x/y are document coordinates (Dissolve only).
}  // namespace pittore::compute
