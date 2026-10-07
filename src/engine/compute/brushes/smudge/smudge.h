#pragma once
// Smudge dabs: the dirty-brush model. The stroke carries a small patch of
// paint that travels with the dab: each dab lays the carried patch onto the
// canvas, then reloads the patch from the canvas underneath (pickup), so
// texture drags instead of flattening to a single colour.
#include <cstdint>
#include <vector>

#include "engine/compute/blend.h"
#include "engine/compute/brushes/selection_mask/selection_mask.h"
#include "engine/compute/brushes/stamp/stamp.h"
#include "engine/compute/brushes/mask/mask.h"
#include "engine/compute/brushes/texture/texture.h"
#include "engine/compute/brushes/tip/tip.h"
#include "engine/core/pixel.h"

namespace pittore::compute {

// Smudge stroke shape: Dulling picks up under the dab (shape preserving);
// Smear picks up trailing behind the stroke direction (streaks).
enum class SmudgeMode : int {
    Dulling = 0,
    Smear = 1,
};

// Per-dab smudge controls. All neutral by default.
struct SmudgeCtl {
    SmudgeMode mode = SmudgeMode::Dulling;
    float colorRate = 0.0f;  // 0 pure pickup .. 1 full foreground reload
    RGBAf fg{0, 0, 0, 0};    // foreground ink reloaded by colorRate
    float trailX = 0.0f;     // pickup offset in layer px (smear direction)
    float trailY = 0.0f;
    bool fingerPaint = false;  // first dab starts from fg, not the canvas
};

// Paint carried by the stroke: a square patch centred on the dab that moves
// with it. Owned by the caller, one per stroke; empty = unprimed (the next
// dab primes it from the canvas, or from fg when fingerPaint is set).
struct SmudgeCarry {
    std::vector<RGBAf> color;  // straight RGBA, row-major, side*side
    std::vector<float> height;  // relief patch, same layout (when bound)
    int side = 0;
    bool hasHeight = false;
    void clear() {
        color.clear();
        height.clear();
        side = 0;
        hasHeight = false;
    }
    bool empty() const { return side <= 0 || color.empty(); }
};

// Optional pickup source: a layer-space tile resampled from the composite
// ("sample all layers"). Null = pick up from the dab target itself.
struct SmudgePick {
    const RGBAf* data = nullptr;  // tile texels, row-major
    std::uint32_t w = 0, h = 0;
    float ox = 0.0f, oy = 0.0f;  // tile origin in layer pixels
};

// Auto-tip smudge. `rate` (0..1) is the smear amount, `radiusFrac` scales the
// sampling/blending footprint relative to the dab radius. `blend` constrains
// the smear through a blend mode (Normal = plain smear). `ctl` selects
// dulling/smear, folds foreground into the carried patch per dab, and offsets
// the smear pickup (all neutral = plain dulling). `carry` is the travelling
// patch (primed on first dab). `height` transports relief alongside color
// (null = color only); the carry's height patch loads on first dab when
// bound.
void smudge_tip_dab_host(RGBAf* dst, std::uint32_t w, std::uint32_t h,
                         float cx, float cy, float radius, const AutoTip& tip,
                         float rate, float radiusFrac, SmudgeCarry& carry,
                         const SelectionMask* selection = nullptr,
                         const PatternTex* tex = nullptr,
                         const DabDensity* den = nullptr,
                         const MaskTip* mask = nullptr,
                         int flip = 0,
                         const SmudgePick* pick = nullptr,
                         BlendMode blend = BlendMode::Normal,
                         const SmudgeCtl* ctl = nullptr,
                         float* height = nullptr);

// Stamp smudge: the stamp's alpha channel is the coverage mask.
void smudge_stamp_dab_host(RGBAf* dst, std::uint32_t w, std::uint32_t h,
                           float cx, float cy, float radius,
                           const StampTip& tip, float rate, float radiusFrac,
                           float angleDeg, SmudgeCarry& carry,
                           const SelectionMask* selection = nullptr,
                           const PatternTex* tex = nullptr,
                           const DabDensity* den = nullptr,
                           const MaskTip* mask = nullptr,
                           int flip = 0, int filter = 1,
                           const SmudgePick* pick = nullptr,
                           BlendMode blend = BlendMode::Normal,
                           const SmudgeCtl* ctl = nullptr,
                           float* height = nullptr);

}  // namespace pittore::compute
