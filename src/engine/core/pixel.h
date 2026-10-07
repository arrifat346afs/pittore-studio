#pragma once
// Working pixel: linear-light RGBA floats, straight alpha.
// Blenders premultiply inside, then divide back out.

namespace pittore {

struct alignas(16) RGBAf {
    float r = 0.0f;
    float g = 0.0f;
    float b = 0.0f;
    float a = 0.0f;

    float& operator[](int i) { return (&r)[i]; }
    float operator[](int i) const { return (&r)[i]; }
};

static_assert(sizeof(RGBAf) == 16, "RGBAf must be exactly four floats (float4-compatible)");

}  // namespace pittore