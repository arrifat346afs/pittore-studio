#include "engine/compute/brushes/tone/tone.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>

#include "engine/core/pixel.h"

namespace pittore::compute {

namespace {

// The photographic Dodge/Burn transfer, per component along the curve the
// Range selects. `e` is the exposure ∈ [0,1]; `dodge` lightens, Burn darkens.
// The curves pivot on the tone they serve, so the edit concentrates there and
// a single pass can never shove a channel past its own limit:
//
//   Highlights  c·(1 ± e/3)                  – proportional, anchored at black
//   Midtones    c^(1/(1+e/2))  /  c^(1/(1−e/2)) – the standard Levels middle
//               (gamma) slider: full-exposure Dodge ≡ gamma 1.5 (c^(2/3)),
//               Burn ≡ gamma 0.5 (c²) — the equivalence measured against the
//               real tool
//   Shadows     c + (e/3)(1−c)  /  (c−e/3)/(1−e/3) – anchored at white
//
// At full exposure a mid-tone dodge lands at ≈0.63 and a mid-tone burn at 0.25
// — neither paints flat white or black.
inline float tone_curve(float c, float e, int range, bool dodge) {
    c = std::clamp(c, 0.0f, 1.0f);
    const float f = e * (1.0f / 3.0f);
    if (range == 2)  // highlights: pivot on black, strongest near white
        return std::clamp(c * (dodge ? (1.0f + f) : (1.0f - f)), 0.0f, 1.0f);
    if (range == 0)  // shadows: pivot on white, strongest near black
        return dodge ? c + f * (1.0f - c)
                     : (c < f ? 0.0f : (c - f) / (1.0f - f));
    // midtones: the Levels gamma slider, interpolated linearly in the gamma
    // value between identity (1.0) and the full-exposure anchors (1.5 dodge,
    // 0.5 burn).
    return dodge ? std::pow(c, 1.0f / (1.0f + 0.5f * e))
                 : std::pow(c, 1.0f / (1.0f - 0.5f * e));
}

// Per-pixel tonal/saturation operator. `coverage` ∈ [0,1] is the effective
// dab/selection coverage and `amount` ∈ [0,1] the operator's exposure (Dodge/
// Burn) or flow (Sponge). Dodge/Burn compute the range's full-exposure result
// and let the coverage blend it in, so a soft dab edge is a soft transition
// rather than a weaker transfer.
inline void apply_tone_to(RGBAf& d, float coverage, float amount, ToneOp op,
                          int range, bool protect_tones, bool vibrance) {
    if (coverage <= 0.0f || amount <= 0.0f || d.a <= 0.0f) return;
    // nothing to tone on a transparent texel
    const float r = d.r, g = d.g, b = d.b;
    const float luma = 0.3f * r + 0.59f * g + 0.11f * b;
    const float cov = std::clamp(coverage, 0.0f, 1.0f);

    if (op == ToneOp::Dodge || op == ToneOp::Burn) {
        const bool dodge = op == ToneOp::Dodge;
        const float e = std::clamp(amount, 0.0f, 1.0f);
        if (protect_tones) {
            // Protect Tones — the modern default: transfer the
            // luminance and rescale RGB by L'/L, so the edit brightens or
            // darkens without shifting hue or saturation, and stalls as a
            // channel nears its limit instead of clipping.
            const float lp = tone_curve(luma, e, range, dodge);
            float nr, ng, nb;
            if (luma > 0.0f) {
                const float sc = lp / luma;
                nr = std::clamp(r * sc, 0.0f, 1.0f);
                ng = std::clamp(g * sc, 0.0f, 1.0f);
                nb = std::clamp(b * sc, 0.0f, 1.0f);
            } else {
                // Pure black has no chroma to preserve; carry the transfer.
                nr = ng = nb = std::clamp(lp, 0.0f, 1.0f);
            }
            d.r = r + (nr - r) * cov;
            d.g = g + (ng - g) * cov;
            d.b = b + (nb - b) * cov;
        } else {
            // Legacy per-channel transfer (protect tones off): each component
            // rides the range's curve, so saturation shifts as luminance
            // changes — Dodge desaturates, Burn saturates.
            const float tr = tone_curve(r, e, range, dodge);
            const float tg = tone_curve(g, e, range, dodge);
            const float tb = tone_curve(b, e, range, dodge);
            d.r = r + (tr - r) * cov;
            d.g = g + (tg - g) * cov;
            d.b = b + (tb - b) * cov;
        }
    } else {
        const float mx = std::max(r, std::max(g, b));
        const float mn = std::min(r, std::min(g, b));
        const float sat = mx > 0.0f ? (mx - mn) / mx : 0.0f;
        float k = amount * cov;
        // Vibrance biases the change toward the least-saturated pixels.
        if (vibrance) k *= (1.0f - sat);
        if (op == ToneOp::Desaturate) {
            // Sponge Desaturate: pull toward the pixel's own luminance.
            d.r = r + (luma - r) * k;
            d.g = g + (luma - g) * k;
            d.b = b + (luma - b) * k;
        } else {
            // Saturate is the same pull with the amount negated, clamped.
            d.r = std::clamp(r + (r - luma) * k, 0.0f, 1.0f);
            d.g = std::clamp(g + (g - luma) * k, 0.0f, 1.0f);
            d.b = std::clamp(b + (b - luma) * k, 0.0f, 1.0f);
        }
    }
}

}  // namespace

// Tonal / saturation operator under a soft circular dab (see paint.h). The dab
// geometry is deliberately identical to paint_dab_host so a Dodge/Burn/Sponge
// stroke has exactly the same footprint as a paint brush of the same size.
void tone_dab_host(RGBAf* dst, std::uint32_t w, std::uint32_t h, float cx,
                   float cy, float radius, float hardness, float amount,
                   ToneOp op, int range, bool protect_tones, bool vibrance,
                   const SelectionMask* selection) {
    if (!dst || radius <= 0.0f || amount <= 0.0f || w == 0 || h == 0) return;

    const float hard = std::clamp(hardness, 0.0f, 1.0f);
    const float amt = std::clamp(amount, 0.0f, 1.0f);

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
            const float t = std::sqrt(dx * dx + dy * dy) * inv_r;
            if (t >= 1.0f) continue;

            float cov = 1.0f;
            if (t > 1.0f - soft_span) cov = (1.0f - t) / soft_span;
            if (selection) cov *= selection->coverage(x, y);
            apply_tone_to(dst[idx], cov, amt, op, range, protect_tones, vibrance);
        }
    }
}

// One dab of a TONAL stroke. The reference keeps a per-pixel coverage buffer
// for the whole stroke and, on every dab, re-renders the touched pixels from
// their PRE-stroke value at `coverage × amount`. So overlapping dabs raise the
// coverage instead of compounding the tone — a 50% Dodge stays 50%, where
// applying the operator dab-by-dab would race to white.
bool tone_stroke_dab_host(const RGBAf* pre, RGBAf* dst, float* coverage,
                          std::uint32_t w, std::uint32_t h, float cx, float cy,
                          float radius, float hardness, float amount, ToneOp op,
                          int range, bool protect_tones, bool vibrance,
                          const SelectionMask* selection, int* bbox) {
    if (!pre || !dst || !coverage || radius <= 0.0f || amount <= 0.0f || w == 0 ||
        h == 0)
        return false;

    const float hard = std::clamp(hardness, 0.0f, 1.0f);
    const float amt = std::clamp(amount, 0.0f, 1.0f);

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

    // Re-render the touched pixels from the pre-stroke image. Pixels already
    // at the same coverage are recomputed to the same value, so re-rendering
    // into the overlap is harmless.
    for (int y = by0; y <= by1; ++y) {
        std::size_t idx = static_cast<std::size_t>(y) * w + static_cast<std::size_t>(bx0);
        for (int x = bx0; x <= bx1; ++x, ++idx) {
            const float c = coverage[idx];
            if (c <= 0.0f) continue;
            RGBAf d = pre[idx];
            apply_tone_to(d, c, amt, op, range, protect_tones, vibrance);
            dst[idx] = d;
        }
    }

    if (bbox) {
        bbox[0] = bx0;
        bbox[1] = by0;
        bbox[2] = bx1 + 1;
        bbox[3] = by1 + 1;
    }
    return true;
}

}  // namespace pittore::compute
