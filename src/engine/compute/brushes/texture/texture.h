#pragma once
// Dab paper-grain texture: a tiled grayscale pattern modulates dab
// coverage, so strokes break up like pencil on toothy paper. All fields
// are plain data; sampling is a header-inline helper the dab kernels
// share. Modes 0..2 are the original set (preserved bit-exactly);
// modes 3..6 are original approximations (not copied from any other app):
// overlay smooths the grain, dodge screens it (more opaque), burn gammas
// it (holes), height boosts it (full coverage possible).
#include <algorithm>
#include <cmath>
#include <cstdint>

namespace pittore::compute {

struct PatternTex {
    const float* gray = nullptr;  // w*h luma in [0,1], row-first, tiled
    std::uint32_t w = 0, h = 0;
    float strength = 0.0f;  // 0 = off, 1 = full grain
    float scale = 1.0f;     // pattern px per target px
    float offsetX = 0.0f, offsetY = 0.0f;  // target-space origin (per stroke)
    float neutral = 0.5f;   // levels pivot for brightness/contrast
    float brightness = 0.0f;
    float contrast = 1.0f;
    bool invert = false;
    // Combine mode: 0 multiply (soft), 1 subtract (harsh threshold),
    // 2 darken (minimum), 3 overlay (smoothed grain), 4 dodge (screened,
    // more opaque), 5 burn (gamma, holes), 6 height (boosted, full
    // coverage possible). Cutoff windows the grain first unless the
    // policy is 0 (off); the tip-side policy is approximated by the
    // same grain window.
    int mode = 0;
    // Soft texturing: when true, partial strength fades toward untextured
    // (1.0); when false, it rubs out toward transparent (0.0 baseline).
    // Modes 0..2 ignore it to preserve their original behavior.
    bool soft = false;
    int cutoffPolicy = 0;
    float cutLo = 0.0f, cutHi = 1.0f;

    bool valid() const {
        return gray != nullptr && w > 0 && h > 0 && w <= 1024 && h <= 1024 &&
               strength > 0.0f && scale > 0.0f && std::isfinite(scale);
    }
};

// Coverage multiplier at target-space (x, y).
inline float texture_mult(const PatternTex* t, float x, float y) {
    if (t == nullptr || !t->valid()) return 1.0f;
    const float s = t->scale;
    float u = (x + t->offsetX) / s;
    float v = (y + t->offsetY) / s;
    float fu = u - std::floor(u / float(t->w)) * float(t->w);
    float fv = v - std::floor(v / float(t->h)) * float(t->h);
    std::uint32_t xi = std::uint32_t(fu);
    std::uint32_t yi = std::uint32_t(fv);
    if (xi >= t->w) xi = t->w - 1;
    if (yi >= t->h) yi = t->h - 1;
    float g = t->gray[std::size_t(yi) * t->w + xi];
    if (t->invert) g = 1.0f - g;
    g = (g - t->neutral) * t->contrast + t->neutral + t->brightness;
    g = std::clamp(g, 0.0f, 1.0f);
    if (t->cutoffPolicy != 0) {
        const float span = std::max(t->cutHi - t->cutLo, 1e-4f);
        g = std::clamp((g - t->cutLo) / span, 0.0f, 1.0f);
    }
    const float st = std::clamp(t->strength, 0.0f, 1.0f);
    switch (t->mode) {
        case 1:  // subtract: harsh, threshold-like at partial strength
            return std::clamp(g - (1.0f - st), 0.0f, 1.0f);
        case 2: {  // darken: minimum of tip and grain
            if (st <= 0.0f) return 1.0f;
            return std::clamp(std::min(g / std::max(st, 1e-6f), 1.0f), 0.0f,
                              1.0f);
        }
        case 3:
        case 4:
        case 5:
        case 6: {
            // Full-strength grain shape per mode (all map 0->0, 1->1).
            float h = g;
            switch (t->mode) {
                case 3:  // overlay: smootherstepped grain, soft mid contrast
                    h = g * g * (3.0f - 2.0f * g);
                    break;
                case 4:  // dodge: screened grain, stays more opaque
                    h = 1.0f - (1.0f - g) * (1.0f - g);
                    break;
                case 5:  // burn: gamma grain, holes in the darks
                    h = g * g;
                    break;
                default:  // 6 height: boosted grain, full coverage possible
                    h = std::min(1.0f, g * 1.5f);
                    break;
            }
            if (t->soft)
                return 1.0f - st + st * h;  // fade toward untextured
            return std::clamp(h - (1.0f - st), 0.0f, 1.0f);  // rub out
        }
        default:  // multiply: soft feel
            return 1.0f - st + st * g;
    }
}

// Per-pixel density: sparse noisy coverage instead of dimming. Pixels are
// kept by a stable hash of (x, y, seed) so a reseeded stroke replays
// exactly; density 1 keeps everything (the common case, zero cost beyond
// one comparison when a DabDensity is supplied).
struct DabDensity {
    float density = 1.0f;  // fraction of covered pixels kept
    std::uint32_t seed = 0;
};

inline bool density_keep(const DabDensity* d, int x, int y) {
    if (d == nullptr || d->density >= 1.0f) return true;
    if (d->density <= 0.0f) return false;
    std::uint32_t h = std::uint32_t(x) * 374761393u +
                      std::uint32_t(y) * 668265263u +
                      d->seed * 974634211u;
    h = (h ^ (h >> 13)) * 1274126177u;
    h ^= h >> 16;
    const float r = float(h) / 4294967295.0f;
    return r < d->density;
}

}  // namespace pittore::compute