#include "engine/compute/brushes/composite/composite.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>

#include "engine/core/parallel.h"
#include "engine/core/pixel.h"

namespace pittore::compute {

void blend_pixel(RGBAf& d, const RGBAf& s, BlendMode mode, int x, int y) {
    blend::pixel(mode, s.r, s.g, s.b, s.a, d.r, d.g, d.b, d.a, x, y,
                 d.r, d.g, d.b, d.a);
}

// Region-limited composite: identical blend math to cpu_composite, applied
// only to the pixels [x0,x1) × [y0,y1). Used by the canvas's incremental
// brush repaint, where only a small capsule changes between dabs.
void composite_region_host(RGBAf* bottom, const RGBAf* top, std::uint32_t w,
                           std::uint32_t x0, std::uint32_t y0,
                           std::uint32_t x1, std::uint32_t y1,
                           BlendMode mode) {
    if (x0 >= x1 || y0 >= y1) return;

    // Rows disjoint, per-pixel sampler pure: bit-identical threaded.
    pittore::core::parallel_rows(y1 - y0, [&](std::uint32_t lo, std::uint32_t hi) {
        for (std::uint32_t y = y0 + lo; y < y0 + hi; ++y) {
            std::size_t idx = static_cast<std::size_t>(y) * w + x0;
            const std::size_t end = static_cast<std::size_t>(y) * w + x1;
            for (int dx = static_cast<int>(x0); idx < end; ++idx, ++dx)
                blend_pixel(bottom[idx], top[idx], mode, dx, static_cast<int>(y));
        }
    });
}

namespace {

// Premultiplied tap accumulator: resampling runs in premultiplied space so a
// partially covered edge pixel fades its alpha without shifting its hue (no
// dark fringe), and unpremultiplies once at the end. Out-of-range taps
// contribute zero, i.e. transparency.
struct PremulAcc {
    double r = 0, g = 0, b = 0, a = 0, wsum = 0;

    void add(const RGBAf* src, std::uint32_t sw, std::uint32_t sh,
             std::int64_t x, std::int64_t y, double w) {
        wsum += w;  // total weight counts even outside taps (transparency),
        if (w == 0.0 || x < 0 || y < 0 ||
            x >= static_cast<std::int64_t>(sw) ||
            y >= static_cast<std::int64_t>(sh))
            return;  // ...so a half-covered pixel gets half alpha, not full.
        const RGBAf& s =
            src[static_cast<std::size_t>(y) * sw + static_cast<std::size_t>(x)];
        r += s.r * s.a * w;
        g += s.g * s.a * w;
        b += s.b * s.a * w;
        a += s.a * w;
    }
    RGBAf done() const {
        if (wsum <= 0.0 || a <= 0.0) return RGBAf{0, 0, 0, 0};
        const float inv = static_cast<float>(1.0 / wsum);
        const float aa = static_cast<float>(a * inv);
        if (aa <= 0.0f) return RGBAf{0, 0, 0, 0};
        return RGBAf{static_cast<float>(r * inv) / aa,
                     static_cast<float>(g * inv) / aa,
                     static_cast<float>(b * inv) / aa, aa};
    }
};

}  // namespace

RGBAf sample_placed_host(const RGBAf* src, std::uint32_t sw,
                         std::uint32_t sh, double docX, double docY, double ox,
                         double oy, double sx, double sy) {
    if (!src || sw == 0 || sh == 0 || sx <= 0.0 || sy <= 0.0)
        return RGBAf{0, 0, 0, 0};
    const double fx = (docX - ox) / sx;
    const double fy = (docY - oy) / sy;

    if (sx >= 1.0 && sy >= 1.0) {
        // Upscale (or 1:1): bilinear over the 4 surrounding texels. Pixel
        // values are point samples at integer coords, so integer coords
        // return the exact texel — translated layers stay bit-identical to a
        // memcpy blit.
        if (fx < -1.0 || fy < -1.0 || fx > sw || fy > sh)
            return RGBAf{0, 0, 0, 0};
        const std::int64_t x0 = static_cast<std::int64_t>(std::floor(fx));
        const std::int64_t y0 = static_cast<std::int64_t>(std::floor(fy));
        const double tx = fx - x0, ty = fy - y0;
        PremulAcc acc;
        acc.add(src, sw, sh, x0, y0, (1 - tx) * (1 - ty));
        acc.add(src, sw, sh, x0 + 1, y0, tx * (1 - ty));
        acc.add(src, sw, sh, x0, y0 + 1, (1 - tx) * ty);
        acc.add(src, sw, sh, x0 + 1, y0 + 1, tx * ty);
        return acc.done();
    }

    // Downscale: box-average the covered source footprint. Taps per axis cover
    // the footprint, capped so a thumbnail-scale layer stays bounded; beyond
    // the cap the taps stride across the whole footprint (even coverage,
    // approximate weight).
    const double spanX = 1.0 / sx;
    const double spanY = 1.0 / sy;
    if (fx + spanX < 0.0 || fy + spanY < 0.0 || fx > sw || fy > sh)
        return RGBAf{0, 0, 0, 0};
    constexpr int kMaxTaps = 24;
    const int nx = std::max(1, std::min(kMaxTaps, static_cast<int>(std::ceil(spanX))));
    const int ny = std::max(1, std::min(kMaxTaps, static_cast<int>(std::ceil(spanY))));
    PremulAcc acc;
    for (int j = 0; j < ny; ++j) {
        const double tapY =
            (ny == 1) ? fy + spanY * 0.5 : fy + spanY * (j + 0.5) / ny;
        const std::int64_t yy = static_cast<std::int64_t>(std::floor(tapY));
        for (int i = 0; i < nx; ++i) {
            const double tapX =
                (nx == 1) ? fx + spanX * 0.5 : fx + spanX * (i + 0.5) / nx;
            acc.add(src, sw, sh, static_cast<std::int64_t>(std::floor(tapX)),
                    yy, 1.0);
        }
    }
    return acc.done();
}

// Fused placed-layer composite over a region (see paint.h). The per-pixel
// reference for ComputeBackend::composite_placed: sample → fold alpha → blend.
// (The batched mask/clip walk lives in layer_mask.cpp; this stays the
// mask-less single-layer reference the placed-parity tests pin.)
void composite_placed_host(RGBAf* bottom, const RGBAf* src, std::uint32_t sw,
                           std::uint32_t sh, double ox, double oy, double sx,
                           double sy, std::uint32_t w, std::uint32_t x0,
                           std::uint32_t y0, std::uint32_t x1, std::uint32_t y1,
                           float alpha_fold, BlendMode mode) {
    if (x0 >= x1 || y0 >= y1) return;
    // Rows disjoint, per-pixel sampler pure: bit-identical threaded.
    pittore::core::parallel_rows(y1 - y0, [&](std::uint32_t lo, std::uint32_t hi) {
        for (std::uint32_t y = y0 + lo; y < y0 + hi; ++y) {
            std::size_t idx = static_cast<std::size_t>(y) * w;
            for (std::uint32_t x = x0; x < x1; ++x) {
                RGBAf s = sample_placed_host(src, sw, sh, static_cast<double>(x),
                                             static_cast<double>(y), ox, oy, sx,
                                             sy);
                s.a *= alpha_fold;
                blend_pixel(bottom[idx + x], s, mode, static_cast<int>(x),
                            static_cast<int>(y));
            }
        }
    });
}
}  // namespace pittore::compute
