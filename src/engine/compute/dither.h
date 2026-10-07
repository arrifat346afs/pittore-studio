#pragma once
// Ordered Bayer-8 dither for the float -> 8-bit display blits (canvas
// composite, all backends). Smooth gradients (skies, tone blends, vignettes)
// band visibly under plain round-to-nearest; a sub-LSB ordered pattern pushes
// the error to high spatial frequencies the eye averages out.
//
// Deterministic on absolute document coords, so CPU/GPU/HIP agree
// bit-for-bit (GPU parity holds). Exact integers are untouched:
// floor(k + t) == k for integer k and t < 1, so opaque, transparent and
// flat-8-bit sources blit exactly as before.
//
// Pure integer math (verified 64/64 against the reference matrix), so host
// and device agree exactly — no float libm in the threshold path.
#include <cstdint>

namespace pittore::compute {
namespace dither {

#if defined(__HIP_DEVICE_COMPILE__)
#define PITTORE_DITHER_DEVICE __device__ inline
#elif defined(__CUDACC__)
#define PITTORE_DITHER_DEVICE __device__ __forceinline__
#else
#define PITTORE_DITHER_DEVICE inline
#endif

// Classic Bayer-8 threshold, 0..63: three expansions of the 2x2 base
// [[0,2],[3,1]], coarse levels in the high bits.
PITTORE_DITHER_DEVICE std::uint8_t bayer8(unsigned int x, unsigned int y) {
    const unsigned int x0 = x & 1u, x1 = (x >> 1) & 1u, x2 = (x >> 2) & 1u;
    const unsigned int y0 = y & 1u, y1 = (y >> 1) & 1u, y2 = (y >> 2) & 1u;
    const unsigned int c0 = ((x0 ^ y0) << 1) | y0;
    const unsigned int c1 = ((x1 ^ y1) << 1) | y1;
    const unsigned int c2 = ((x2 ^ y2) << 1) | y2;
    return static_cast<std::uint8_t>(c2 + 4u * c1 + 16u * c0);
}

// Dithered 8-bit sample of a clamped 0..1 float at absolute coords.
PITTORE_DITHER_DEVICE std::uint8_t quantize(float v, unsigned int x,
                                             unsigned int y) {
    float c = v < 0.0f ? 0.0f : (v > 1.0f ? 1.0f : v);
    const float t =
        static_cast<float>(bayer8(x, y)) * (1.0f / 64.0f);
    int q = static_cast<int>(c * 255.0f + t);
    if (q < 0) q = 0;
    if (q > 255) q = 255;
    return static_cast<std::uint8_t>(q);
}

#undef PITTORE_DITHER_DEVICE

}  // namespace dither
}  // namespace pittore::compute
