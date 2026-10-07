#pragma once
// Oklab perceptual colour space (Ottosson 2020), linear-sRGB composites.
// Hue-constant under (a,b) scaling, which is exactly what the tone-blend
// chroma stage needs. Shared CPU/GPU header (same pattern as blend.h).
// Research: research/R102-live-tone-blend-algorithms.md §5.

#include <cmath>
#include <cstdint>

#include "engine/core/pixel.h"

// nvcc predefines __forceinline__; hipcc exposes it only after including hip
// runtime headers, so HIP kernels use __device__ inline (same semantics for the
// purposes of these pure helpers). Host compilers get plain inline.
#if defined(__CUDACC__)
#define PITTORE_OKLAB_DEVICE __device__ __forceinline__
#elif defined(__HIPCC__)
#define PITTORE_OKLAB_DEVICE __device__ inline
#else
#define PITTORE_OKLAB_DEVICE inline
#endif

namespace pittore::compute {
namespace oklab {

PITTORE_OKLAB_DEVICE float ok_pow(float base, float exp) {
#if defined(__CUDACC__) || defined(__HIPCC__)
    return ::powf(base, exp);
#else
    return std::pow(base, exp);
#endif
}

PITTORE_OKLAB_DEVICE float ok_abs(float v) {
#if defined(__CUDACC__) || defined(__HIPCC__)
    return ::fabsf(v);
#else
    return std::fabs(v);
#endif
}

// Signed cube root (preserves out-of-gamut negatives for the round trip).
// Device path uses the single-precision intrinsic: faster than powf(x, 1/3)
// and more accurate; host keeps libm pow for the CPU reference. The two
// agree to ~1e-7, inside every parity tolerance.
PITTORE_OKLAB_DEVICE float ok_cbrt(float v) {
#if defined(__CUDACC__) || defined(__HIPCC__)
    return ::cbrtf(v);
#else
    const float m = ok_pow(ok_abs(v), 1.0f / 3.0f);
    return std::copysign(m, v);
#endif
}

PITTORE_OKLAB_DEVICE float ok_clamp01(float v) {
    return v < 0.0f ? 0.0f : (v > 1.0f ? 1.0f : v);
}

// Linear-sRGB RGBAf triplets are consumed as-is: the engine's working
// buffers hold display-referred values, and every other colour operator in
// this codebase (blends, adjustments) works in that space too. True scene
// linearisation would change all existing behaviour; out of scope here.
PITTORE_OKLAB_DEVICE void rgb_to_oklab(float r, float g, float b, float& L,
                                        float& a, float& bb) {
    const float l = 0.4122214708f * r + 0.5363325363f * g + 0.0514459929f * b;
    const float m = 0.2119034982f * r + 0.6806995451f * g + 0.1073969566f * b;
    const float s = 0.0883024619f * r + 0.2817188376f * g + 0.6299787005f * b;
    const float l_ = ok_cbrt(l), m_ = ok_cbrt(m), s_ = ok_cbrt(s);
    L = 0.2104542553f * l_ + 0.7936177850f * m_ - 0.0040720468f * s_;
    a = 1.9779984951f * l_ - 2.4285922050f * m_ + 0.4505937099f * s_;
    bb = 0.0259040371f * l_ + 0.7827717662f * m_ - 0.8086757660f * s_;
}

PITTORE_OKLAB_DEVICE void oklab_to_rgb(float L, float a, float bb, float& r,
                                        float& g, float& b) {
    const float l_ = L + 0.3963377774f * a + 0.2158037573f * bb;
    const float m_ = L - 0.1055613458f * a - 0.0638541728f * bb;
    const float s_ = L - 0.0894841775f * a - 1.2914855480f * bb;
    const float l = l_ * l_ * l_, m = m_ * m_ * m_, s = s_ * s_ * s_;
    r = 4.0767416621f * l - 3.3077115913f * m + 0.2309699292f * s;
    g = -1.2684380046f * l + 2.6097574011f * m - 0.3413193965f * s;
    b = -0.0041960863f * l - 0.7034186147f * m + 1.7076147010f * s;
}

// Hue-preserving soft gamut map into [0,1]: overbright scales all channels
// by the peak (exact chromaticity, mild highlight compression); negatives
// clamp (bounded, rare after the blend's own clamps). Hard per-channel
// clipping would rotate hues — the classic failure mode.
PITTORE_OKLAB_DEVICE void gamut_map(float& r, float& g, float& b) {
    const float peak = r > g ? (r > b ? r : b) : (g > b ? g : b);
    if (peak > 1.0f) {
        const float k = 1.0f / peak;
        r *= k;
        g *= k;
        b *= k;
    }
    r = ok_clamp01(r);
    g = ok_clamp01(g);
    b = ok_clamp01(b);
}

}  // namespace oklab
}  // namespace pittore::compute
