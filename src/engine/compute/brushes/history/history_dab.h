#pragma once
// History Brush dab: source-over snapshot texels under the dab mask.
// Restores the layer toward the history source state (default: oldest undo
// snapshot, i.e. the document-open state). Rim geometry reuses bgEraseRim
// (same brush feel everywhere); device kernels share this header.
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>

#include "engine/compute/brushes/erase/bg_erase.h"
#include "engine/compute/brushes/selection_mask/selection_mask.h"
#include "engine/core/parallel.h"
#include "engine/core/pixel.h"

namespace pittore::compute {

// Source-over one snapshot texel by mask alpha. Device-safe (ternary only)
// so kernels share it; identical op order host/device for parity.
inline void historyDabMix(RGBAf& d, const RGBAf& s, float a) {
    const float inv_a = 1.0f - a;
    const float out_a = a + d.a * inv_a;
    if (out_a > 0.0f) {
        d.r = (a * s.r + d.r * d.a * inv_a) / out_a;
        d.g = (a * s.g + d.g * d.a * inv_a) / out_a;
        d.b = (a * s.b + d.b * d.a * inv_a) / out_a;
        d.a = out_a;
    }
}

// src must match dst dimensions exactly (checked by the caller).
// parallel_rows over circle rows (disjoint dst rows, read-only src).
inline bool history_brush_dab_host(RGBAf* dst, const RGBAf* src,
                                   std::uint32_t w, std::uint32_t h, float cx,
                                   float cy, float radius, float hardness,
                                   float opacity, int* bboxOut,
                                   const SelectionMask* selection = nullptr) {
    if (!dst || !src || radius <= 0.0f || opacity <= 0.0f || w == 0 || h == 0)
        return false;
    const float hard = std::clamp(hardness, 0.0f, 1.0f);
    const float op = std::clamp(opacity, 0.0f, 1.0f);
    const int y0 = std::max(0, static_cast<int>(std::floor(cy - radius)));
    const int y1 = std::min(static_cast<int>(h) - 1,
                            static_cast<int>(std::ceil(cy + radius)));
    const int x0 = std::max(0, static_cast<int>(std::floor(cx - radius)));
    const int x1 = std::min(static_cast<int>(w) - 1,
                            static_cast<int>(std::ceil(cx + radius)));
    if (y1 < y0 || x1 < x0) return false;
    const float inv_r = 1.0f / radius;
    bool touched = false;
    // Small bbox: serial loop avoids thread-spawn cost dominating µs dabs.
    // (parallel_rows pays off at 64+ rows; dab bboxes rarely reach that.)
    for (int y = y0; y <= y1; ++y) {
        const float dy = static_cast<float>(y) + 0.5f - cy;
        std::size_t idx = std::size_t(y) * w + std::size_t(x0);
        for (int x = x0; x <= x1; ++x, ++idx) {
            const float dx = static_cast<float>(x) + 0.5f - cx;
            const float t =
                std::sqrt(dx * dx + dy * dy) * inv_r;
            float m = bgEraseRim(t, hard) * op;
            if (!(m > 0.0f)) continue;
            if (selection) {
                m *= selection->coverage(x, y);
                if (!(m > 0.0f)) continue;
            }
            RGBAf before = dst[idx];
            historyDabMix(dst[idx], src[idx], m);
            if (std::memcmp(&before, &dst[idx], sizeof(RGBAf)) != 0)
                touched = true;
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

// Art History dab: source-over a history-source stamp (centre cx+ox,
// cy+oy, radius stampR) under the brush mask (radius). `tolerance` <= 0
// paints everywhere; otherwise pixels whose dst colour differs from the
// source by more than tolerance are skipped. Serial (stamp-bounded).
inline bool arthistory_dab_host(
    RGBAf* dst, const RGBAf* src, std::uint32_t w, std::uint32_t h, float cx,
    float cy, float radius, float hardness, float opacity, float ox,
    float oy, float stampR, float tolerance, int* bboxOut,
    const SelectionMask* selection = nullptr) {
    if (!dst || !src || radius <= 0.0f || opacity <= 0.0f || w == 0 || h == 0)
        return false;
    const float hard = std::clamp(hardness, 0.0f, 1.0f);
    const float op = std::clamp(opacity, 0.0f, 1.0f);
    const float tol = std::clamp(tolerance, 0.0f, 1.0f);
    const float scx = cx + ox, scy = cy + oy;
    const float sr = std::max(stampR, 1.0f);
    const int x0 = std::max(
        0, int(std::floor(std::min(cx - radius, scx - sr))));
    const int y0 = std::max(
        0, int(std::floor(std::min(cy - radius, scy - sr))));
    const int x1 = std::min(
        int(w) - 1, int(std::ceil(std::max(cx + radius, scx + sr))));
    const int y1 = std::min(
        int(h) - 1, int(std::ceil(std::max(cy + radius, scy + sr))));
    if (x1 < x0 || y1 < y0) return false;
    const float inv_r = 1.0f / radius;
    bool touched = false;
    for (int y = y0; y <= y1; ++y) {
        for (int x = x0; x <= x1; ++x) {
            const float dx = float(x) + 0.5f - cx;
            const float dy = float(y) + 0.5f - cy;
            const float t = std::sqrt(dx * dx + dy * dy) * inv_r;
            float m = bgEraseRim(t, hard) * op;
            if (!(m > 0.0f)) continue;
            // Inside the stamp disc?
            const float sdx = float(x) + 0.5f - scx;
            const float sdy = float(y) + 0.5f - scy;
            if (std::sqrt(sdx * sdx + sdy * sdy) > sr) continue;
            if (selection) {
                m *= selection->coverage(x, y);
                if (!(m > 0.0f)) continue;
            }
            std::size_t idx = std::size_t(y) * w + x;
            if (tol > 0.0f) {
                const RGBAf& o = dst[idx];
                const RGBAf& s = src[idx];
                float dd = std::fabs(o.r - s.r);
                dd = std::max(dd, std::fabs(o.g - s.g));
                dd = std::max(dd, std::fabs(o.b - s.b));
                if (dd > tol) continue;
            }
            historyDabMix(dst[idx], src[idx], m);
            touched = true;
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
