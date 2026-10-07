#pragma once
// Blur / Sharpen dab core (engine/compute/brushes/blur).
// Clean-room: dab-mask falloff mirrors paint_dab_host (linear rim ramp by
// hardness); blur kernel is a small clamped-edge separable box blur.
// Both backends share this header for math; AppState picks CPU inline or
// the backend box_blur on the bbox for the GPU fastpath.
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>

#include "engine/core/parallel.h"
#include "engine/core/pixel.h"
#include "engine/compute/brushes/selection_mask/selection_mask.h"

namespace pittore::compute {

// Blur kernel radius from dab radius: deterministic, documented, tested.
inline int blurDabKernelRadius(float radius) {
    return std::clamp(int(radius / 16.0f), 1, 4);
}

// Dab mask: 1 inside the hard core, linear ramp over the rim.
// Mirrors paint_dab_host falloff so Blur/Sharpen feel like the same brush.
inline float blurDabMask(float dx, float dy, float radius, float hardness) {
    const float t =
        std::sqrt(dx * dx + dy * dy) / std::max(radius, 1e-6f);
    if (t >= 1.0f) return 0.0f;
    const float hard = std::clamp(hardness, 0.0f, 1.0f);
    const float core = hard;
    if (t <= core) return 1.0f;
    const float span = std::max(1.0f - core, 1e-4f);
    return std::clamp((1.0f - t) / span, 0.0f, 1.0f);
}

// Box-blur a packed bbox temp in place (clamped edges), separable.
// Rows write disjoint dst rows from read-only src: parallel_rows-safe.
inline void blurDabBoxBlur(std::vector<RGBAf>& buf, int bw, int bh, int r) {
    if (r <= 0 || bw <= 0 || bh <= 0) return;
    std::vector<RGBAf> tmp(std::size_t(bw) * bh);
    // H pass.
    core::parallel_rows(std::uint32_t(bh), [&](std::uint32_t y0, std::uint32_t y1) {
        for (std::uint32_t y = y0; y < y1; ++y) {
            for (int x = 0; x < bw; ++x) {
                double sr = 0, sg = 0, sb = 0, sa = 0;
                int n = 0;
                for (int k = -r; k <= r; ++k) {
                    const int sx = std::clamp(x + k, 0, bw - 1);
                    const RGBAf& p = buf[std::size_t(y) * bw + sx];
                    sr += p.r;
                    sg += p.g;
                    sb += p.b;
                    sa += p.a;
                    ++n;
                }
                RGBAf& o = tmp[std::size_t(y) * bw + x];
                o.r = float(sr / n);
                o.g = float(sg / n);
                o.b = float(sb / n);
                o.a = float(sa / n);
            }
        }
    });
    // V pass (reads tmp, writes buf).
    core::parallel_rows(std::uint32_t(bh), [&](std::uint32_t y0, std::uint32_t y1) {
        for (std::uint32_t y = y0; y < y1; ++y) {
            for (int x = 0; x < bw; ++x) {
                double sr = 0, sg = 0, sb = 0, sa = 0;
                int n = 0;
                for (int k = -r; k <= r; ++k) {
                    const int sy = std::clamp(int(y) + k, 0, bh - 1);
                    const RGBAf& p = tmp[std::size_t(sy) * bw + x];
                    sr += p.r;
                    sg += p.g;
                    sb += p.b;
                    sa += p.a;
                    ++n;
                }
                RGBAf& o = buf[std::size_t(y) * bw + x];
                o.r = float(sr / n);
                o.g = float(sg / n);
                o.b = float(sb / n);
                o.a = float(sa / n);
            }
        }
    });
}

// Full dab: blur bbox (with halo) then mix by mask*strength.
// sharpen=false → mix toward blurred (Blur). sharpen=true → unsharp:
// orig + (orig-blurred), gated by protectDetail on flat areas.
// bboxOut = {x0,y0,x1,y1} touched layer rect (exclusive). Returns changed.
inline bool blur_sharpen_dab_host(RGBAf* dst, std::uint32_t w,
                                  std::uint32_t h, float cx, float cy,
                                  float radius, float hardness, float strength,
                                  bool sharpen, bool protectDetail,
                                  int* bboxOut,
                                  const SelectionMask* selection = nullptr) {
    if (!dst || w == 0 || h == 0 || radius <= 0.0f) return false;
    const float st = std::clamp(strength, 0.0f, 1.0f);
    if (!(st > 0.0f)) return false;
    const int kr = blurDabKernelRadius(radius);
    const int halo = kr * 2 + 1;
    const int bx0 = std::max(0, int(std::floor(cx - radius - halo)));
    const int by0 = std::max(0, int(std::floor(cy - radius - halo)));
    const int bx1 =
        std::min(int(w), int(std::ceil(cx + radius + halo)));
    const int by1 =
        std::min(int(h), int(std::ceil(cy + radius + halo)));
    if (bx1 <= bx0 || by1 <= by0) return false;
    const int bw = bx1 - bx0, bh = by1 - by0;
    std::vector<RGBAf> buf(std::size_t(bw) * bh);
    for (int y = 0; y < bh; ++y)
        for (int x = 0; x < bw; ++x)
            buf[std::size_t(y) * bw + x] = dst[std::size_t(by0 + y) * w + bx0 + x];
    blurDabBoxBlur(buf, bw, bh, kr);
    // Mix circle.
    const int mx0 = std::max(bx0, int(std::floor(cx - radius)));
    const int my0 = std::max(by0, int(std::floor(cy - radius)));
    const int mx1 = std::min(bx1, int(std::ceil(cx + radius)));
    const int my1 = std::min(by1, int(std::ceil(cy + radius)));
    bool touched = false;
    for (int y = my0; y < my1; ++y) {
        for (int x = mx0; x < mx1; ++x) {
            const float dx = float(x) + 0.5f - cx;
            const float dy = float(y) + 0.5f - cy;
            float m = blurDabMask(dx, dy, radius, hardness) * st;
            if (!(m > 0.0f)) continue;
            if (selection) {
                m *= selection->coverage(x, y);
                if (!(m > 0.0f)) continue;
            }
            RGBAf& o = dst[std::size_t(y) * w + x];
            const RGBAf& b = buf[std::size_t(y - by0) * bw + (x - bx0)];
            if (sharpen) {
                const float dr = (o.r - b.r) * m;
                const float dg = (o.g - b.g) * m;
                const float db = (o.b - b.b) * m;
                if (protectDetail) {
                    const float lum =
                        0.2126f * std::fabs(dr) + 0.7152f * std::fabs(dg) +
                        0.0722f * std::fabs(db);
                    if (lum < 0.015f) continue;
                }
                o.r = std::clamp(o.r + dr, 0.0f, 1.0f);
                o.g = std::clamp(o.g + dg, 0.0f, 1.0f);
                o.b = std::clamp(o.b + db, 0.0f, 1.0f);
            } else {
                o.r += (b.r - o.r) * m;
                o.g += (b.g - o.g) * m;
                o.b += (b.b - o.b) * m;
                o.a += (b.a - o.a) * m;
            }
            touched = true;
        }
    }
    if (bboxOut && touched) {
        bboxOut[0] = mx0;
        bboxOut[1] = my0;
        bboxOut[2] = mx1;
        bboxOut[3] = my1;
    }
    return touched;
}

}  // namespace pittore::compute
