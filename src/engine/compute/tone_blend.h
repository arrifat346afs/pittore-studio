#pragma once
// Live Tone Blend Group transfer (conventional reharmonization): re-grade
// a group composite G against the already-composited backdrop B beneath it,
// keeping G's detail. CPU reference; GPU backends implement the same math.
//
// Pipeline (research/R102 §8 primary + docs/live-tone-blend-research.md §4):
//   B_L, G_L = low-pass(B), low-pass(G)      (backdrop reference blur)
//   K        = clamp(B_L / (G_L + eps))       (gain field, ±8 stops)
//   T        = G * K                          (tone exchange, detail kept)
//   (L,a,b)  = Oklab(T); (a,b) *= color       (post saturation scale)
//   L        = pivot + (L - pivot) * (1+t)    (post S-curve about backdrop
//                                             mean lightness; t = contrast)
//   rgb      = Oklab⁻¹(L,a,b), hue-safe gamut map
//   Out      = mix(G, rgb, strength); Out.a = G.a
//
// Verified .af semantics baked in: Blend Color is a post saturation
// scale (0 desaturates, tonal blend survives); Blend Contrast is a post
// S-curve (negative flattens); Blend Strength is wet/dry; Low Pass blurs
// the backdrop reference (max = smoothest); Content Type only changes
// rasterization/coverage, never this math.

#include <cstdint>
#include <vector>

#include "engine/core/pixel.h"

namespace pittore::compute {

struct ToneBlendParams {
    float strength = 1.0f;   // 0..1 wet/dry (0 = identity)
    float color = 1.0f;      // 0..1 post saturation scale
    float contrast = 0.0f;   // -1..1 post S-curve about backdrop mean L
    float lowPass = 1.0f;    // 0..1 backdrop reference blur (1 = smoothest)
    int contentType = 0;     // 0 image / 1 vector / 2 text (hint only)
};

// Re-grade group against backdrop into dst (w*h straight RGBAf buffers).
// dst may alias group (per-pixel read-then-write is safe: the low-pass
// fields live in their own buffers), but must not alias backdrop. Where the
// backdrop has no coverage the gain is neutral (K = 1); transparent group
// texels stay transparent. Full-frame op. Returns false (doing nothing)
// when strength <= 0 or the frame is empty.
bool applyToneBlend(const pittore::RGBAf* group,
                    const pittore::RGBAf* backdrop, pittore::RGBAf* dst,
                    std::uint32_t w, std::uint32_t h,
                    const ToneBlendParams& p);

// Region-bounded variant: re-grade only [rx0,rx1) x [ry0,ry1) of `dst`
// (only `rect` is written back). Same contract as
// ComputeBackend::tone_blend_region on the device paths: it re-samples the
// low-pass level cached by the last full-frame applyToneBlend and re-uses
// its global contrast pivot, so the region matches what full frame produced
// there and cannot seam. Full-frame buffers with full-frame stride (w, h) —
// regions are addressed, not repacked.
bool applyToneBlendRegion(const pittore::RGBAf* group,
                          const pittore::RGBAf* backdrop,
                          pittore::RGBAf* dst, std::uint32_t w,
                          std::uint32_t h, std::uint32_t rx0,
                          std::uint32_t ry0, std::uint32_t rx1,
                          std::uint32_t ry1, const ToneBlendParams& p);

}  // namespace pittore::compute
