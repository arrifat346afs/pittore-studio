#pragma once
// Background Eraser dab math, shared by CPU + CUDA/HIP kernels.
// One source of truth: parity tests hold CPU and GPU equal (float tolerance).
// Device-safe scalar core (ternary ops only, no <algorithm>); host loops
// live in erase.cpp.
#include <cstdint>

#include "engine/core/pixel.h"

// nvcc has __forceinline__ built in; hipcc only after HIP headers, so HIP
// uses __device__ inline here. Host gets plain inline.
#if defined(__CUDACC__)
#define PITTORE_BGERASE_DEVICE __device__ __forceinline__
#elif defined(__HIPCC__)
#define PITTORE_BGERASE_DEVICE __device__ inline
#else
#define PITTORE_BGERASE_DEVICE inline
#endif

namespace pittore::compute {

// Largest per-channel RGB difference (alpha excluded: paper white and paint
// can share alpha 1 while differing in colour). Same metric as the flood
// filler's colour_distance, so dab and contiguous modes agree on tolerance.
struct BgEraseFloat4 {
    float x, y, z;
};

PITTORE_BGERASE_DEVICE float bgEraseDist(float pr, float pg,
                                                 float pb, float sr, float sg,
                                                 float sb) {
    float dr = pr > sr ? pr - sr : sr - pr;
    float dg = pg > sg ? pg - sg : sg - pg;
    float db = pb > sb ? pb - sb : sb - pb;
    float m = dr > dg ? dr : dg;
    return m > db ? m : db;
}

// Tolerance ramp: full erase at exact match, feathering to 0 at tol.
// tol <= 0 erases exact matches only.
PITTORE_BGERASE_DEVICE float bgEraseFrac(float dist, float tol) {
    if (tol <= 0.0f) return dist <= 1e-6f ? 1.0f : 0.0f;
    if (dist > tol) return 0.0f;
    return (tol - dist) / (tol + 1e-6f);
}

// Dab rim mask: 1 inside the hard core, linear ramp over the rim.
// Same geometry as paint_dab_host (t = dist/radius, core = hardness).
PITTORE_BGERASE_DEVICE float bgEraseRim(float t, float hardness) {
    if (t >= 1.0f) return 0.0f;
    float hard = hardness < 0.0f ? 0.0f : (hardness > 1.0f ? 1.0f : hardness);
    if (t <= hard) return 1.0f;
    float span = 1.0f - hard;
    if (span < 1e-4f) span = 1e-4f;
    float m = (1.0f - t) / span;
    return m < 0.0f ? 0.0f : (m > 1.0f ? 1.0f : m);
}

}  // namespace pittore::compute
