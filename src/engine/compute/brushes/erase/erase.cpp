#include "engine/compute/brushes/erase/erase.h"

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <limits>

#include "engine/compute/brushes/erase/bg_erase.h"
#include "engine/core/parallel.h"
#include "engine/core/pixel.h"

namespace pittore::compute {

void erase_dab_host(RGBAf* dst, std::uint32_t w, std::uint32_t h, float cx,
                    float cy, float radius, float hardness, float opacity,
                    const SelectionMask* selection,
                    const PatternTex* tex, const DabDensity* den,
                        const MaskTip* mask) {
    if (radius <= 0.0f || opacity <= 0.0f || w == 0 || h == 0) return;

    // Identical mask geometry to paint_dab_host (same bbox, hardness falloff,
    // covered region) so an eraser stroke is an exact inverse of a paint one.
    const float hard = std::clamp(hardness, 0.0f, 1.0f);
    const float op = std::clamp(opacity, 0.0f, 1.0f);

    const int y0 = std::max(0, static_cast<int>(std::floor(cy - radius)));
    const int y1 = std::min(static_cast<int>(h) - 1,
                            static_cast<int>(std::ceil(cy + radius)));
    const int x0 = std::max(0, static_cast<int>(std::floor(cx - radius)));
    const int x1 = std::min(static_cast<int>(w) - 1,
                            static_cast<int>(std::ceil(cx + radius)));

    const float inv_r = 1.0f / radius;
    const float soft_span = std::max(1.0f - hard, 1e-4f);

    for (int y = y0; y <= y1; ++y) {
        const float dy = static_cast<float>(y) + 0.5f - cy;
        std::size_t idx = static_cast<std::size_t>(y) * w + static_cast<std::size_t>(x0);
        for (int x = x0; x <= x1; ++x, ++idx) {
            const float dx = static_cast<float>(x) + 0.5f - cx;
            float t = std::sqrt(dx * dx + dy * dy) * inv_r;
            if (t >= 1.0f) continue;

            float cov = 1.0f;
            if (t > 1.0f - soft_span)
                cov = (1.0f - t) / soft_span;
            cov = combine_mask(cov, mask, dx, dy, radius);
            if (cov <= 0.0f) continue;
            if (selection) cov *= selection->coverage(x, y);
            if (tex) cov *= texture_mult(tex, float(x), float(y));
            if (cov <= 0.0f || !density_keep(den, x, y)) continue;
            const float a = cov * op;
            if (a <= 0.0f) continue;

            // Erase = alpha multiplied down by the mask; colour is untouched
            // (the pixel becomes invisible, other layers show through).
            RGBAf& d = dst[idx];
            d.a *= (1.0f - a);
        }
    }
}

// Auto-tip twin of erase_dab_host: same rotated ellipse/square measure as
// paint_tip_dab_host, erase blending instead of source-over.
void erase_tip_dab_host(RGBAf* dst, std::uint32_t w, std::uint32_t h, float cx,
                        float cy, float radius, const AutoTip& tip,
                        float opacity,
                        const SelectionMask* selection,
                        const PatternTex* tex, const DabDensity* den,
                        const MaskTip* mask) {
    if (radius <= 0.0f || opacity <= 0.0f || w == 0 || h == 0) return;
    AutoTip t = tip;
    t.sanitize();
    const float op = std::clamp(opacity, 0.0f, 1.0f);
    const float rad = t.angleDeg * 3.14159265358979323846f / 180.0f;
    const float cosA = std::cos(rad), sinA = std::sin(rad);
    const float invRatio = 1.0f / t.ratio;
    const int y0 = std::max(0, static_cast<int>(std::floor(cy - radius)));
    const int y1 = std::min(static_cast<int>(h) - 1,
                            static_cast<int>(std::ceil(cy + radius)));
    const int x0 = std::max(0, static_cast<int>(std::floor(cx - radius)));
    const int x1 = std::min(static_cast<int>(w) - 1,
                            static_cast<int>(std::ceil(cx + radius)));
    const float inv_r = 1.0f / radius;
    for (int y = y0; y <= y1; ++y) {
        const float dy = static_cast<float>(y) + 0.5f - cy;
        std::size_t idx = static_cast<std::size_t>(y) * w + static_cast<std::size_t>(x0);
        for (int x = x0; x <= x1; ++x, ++idx) {
            const float dx = static_cast<float>(x) + 0.5f - cx;
            float cov = auto_tip_coverage(dx, dy, inv_r, t, cosA, sinA,
                                          invRatio);
            if (cov <= 0.0f) continue;
            cov = combine_mask(cov, mask, dx, dy, radius);
            if (cov <= 0.0f) continue;
            if (selection) cov *= selection->coverage(x, y);
            if (tex) cov *= texture_mult(tex, float(x), float(y));
            if (cov <= 0.0f || !density_keep(den, x, y)) continue;
            const float a = cov * op;
            if (a <= 0.0f) continue;
            RGBAf& d = dst[idx];
            d.a *= (1.0f - a);
        }
    }
}

bool background_erase_dab_host(RGBAf* dst, std::uint32_t w, std::uint32_t h,
                               float cx, float cy, float radius,
                               float hardness, float opacity,
                               const RGBAf& sample, float tolerance,
                               bool protectFg, const RGBAf& fg, int* bboxOut,
                               const SelectionMask* selection) {
    if (!dst || radius <= 0.0f || opacity <= 0.0f || w == 0 || h == 0)
        return false;
    const float hard = std::clamp(hardness, 0.0f, 1.0f);
    const float op = std::clamp(opacity, 0.0f, 1.0f);
    const float tol = std::clamp(tolerance, 0.0f, 1.0f);
    const int y0 = std::max(0, static_cast<int>(std::floor(cy - radius)));
    const int y1 = std::min(static_cast<int>(h) - 1,
                            static_cast<int>(std::ceil(cy + radius)));
    const int x0 = std::max(0, static_cast<int>(std::floor(cx - radius)));
    const int x1 = std::min(static_cast<int>(w) - 1,
                            static_cast<int>(std::ceil(cx + radius)));
    if (y1 < y0 || x1 < x0) return false;
    const float inv_r = 1.0f / radius;
    const float sr = sample.r, sg = sample.g, sb = sample.b;
    const float fr = fg.r, fgb = fg.g, fb = fg.b;
    // Rows write disjoint dst rows; sample/fg read-only: parallel_rows-safe.
    std::atomic<bool> touched{false};
    core::parallel_rows(std::uint32_t(y1 - y0 + 1),
                        [&](std::uint32_t lo, std::uint32_t hi) {
                            bool local = false;
                            for (std::uint32_t r = lo; r < hi; ++r) {
                                const int y = y0 + int(r);
                                const float dy =
                                    static_cast<float>(y) + 0.5f - cy;
                                std::size_t idx =
                                    std::size_t(y) * w + std::size_t(x0);
                                for (int x = x0; x <= x1; ++x, ++idx) {
                                    const float dx =
                                        static_cast<float>(x) + 0.5f - cx;
                                    const float t = std::sqrt(dx * dx +
                                                              dy * dy) *
                                                    inv_r;
                                    float m = bgEraseRim(t, hard);
                                    if (!(m > 0.0f)) continue;
                                    if (selection) {
                                        m *= selection->coverage(x, y);
                                        if (!(m > 0.0f)) continue;
                                    }
                                    RGBAf& d = dst[idx];
                                    const float f = bgEraseFrac(
                                        bgEraseDist(d.r, d.g, d.b, sr, sg,
                                                    sb),
                                        tol);
                                    if (!(f > 0.0f)) continue;
                                    if (protectFg &&
                                        bgEraseDist(d.r, d.g, d.b, fr, fgb,
                                                    fb) <= tol)
                                        continue;
                                    const float a = m * f * op;
                                    if (!(a > 0.0f)) continue;
                                    d.a *= (1.0f - a);
                                    local = true;
                                }
                            }
                            if (local) touched.store(true);
                        });
    if (bboxOut) {
        // Geometric circle bbox, written whether or not anything erased
        // (same contract as the device kernel; callers check the return).
        bboxOut[0] = x0;
        bboxOut[1] = y0;
        bboxOut[2] = x1 + 1;
        bboxOut[3] = y1 + 1;
    }
    return touched.load();
}

}  // namespace pittore::compute
