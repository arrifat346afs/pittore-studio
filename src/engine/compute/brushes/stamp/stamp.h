#pragma once
// Bitmap stamp tip: a coverage bitmap (optionally with color) imported from
// user tip files (PNG/GBR/GIH/ABR/preset bundles). Four modes:
//   AlphaMask    – coverage tints the foreground color (grayscale tips).
//   ColorImage   – the tip's own RGBA is stamped as-is (color tips).
//   LightnessMap – tip lightness recolors the foreground around a neutral
//                  pivot (dark tip shades FG, bright tip tints it).
//   GradientMap  – tip lightness blends background to foreground.
// Stored coverage is a paint mask: 1 = full paint.
#include <cstdint>
#include <vector>

#include "engine/compute/brushes/selection_mask/selection_mask.h"
#include "engine/compute/brushes/stamp/pattern.h"
#include "engine/compute/brushes/mask/mask.h"
#include "engine/compute/brushes/texture/texture.h"
#include "engine/core/pixel.h"

namespace pittore::compute {

enum class StampMode : int {
    AlphaMask = 0,
    ColorImage = 1,
    LightnessMap = 2,
    GradientMap = 3,
};

// Tip lightness levels for the map modes: pivot that reproduces the
// foreground exactly, plus brightness lift and contrast around it.
struct StampLevels {
    float neutral = 0.5f;
    float brightness = 0.0f;
    float contrast = 1.0f;
};

struct StampTip {
    std::uint32_t w = 0, h = 0;
    std::vector<float> alpha;  // w*h coverage in [0,1], row-first
    // Color planes, present only when color == true (same layout).
    std::vector<float> red, green, blue;
    bool color = false;
    float spacingPct = 15.0f;  // % of dab diameter between dabs

    bool valid() const {
        return w > 0 && h > 0 && w <= 1024 && h <= 1024 &&
               alpha.size() == std::size_t(w) * h &&
               (!color || (red.size() == alpha.size() &&
                           green.size() == alpha.size() &&
                           blue.size() == alpha.size()));
    }
    void sanitize() {
        if (spacingPct < 1.0f || !(spacingPct <= 200.0f)) spacingPct = 15.0f;
    }
};

// Spacing in doc units for a dab of `radius` under this tip.
inline float stamp_spacing(float radius, const StampTip& tip) {
    const float diameter = 2.0f * (radius < 0.0f ? 0.0f : radius);
    const float pct = (tip.spacingPct < 1.0f || tip.spacingPct > 200.0f)
                          ? 15.0f
                          : tip.spacingPct;
    float s = diameter * pct / 100.0f;
    return s < 0.5f ? 0.5f : s;
}

// Paint one stamp dab. The tip's longest side maps to the dab diameter;
// angleDeg rotates the tip. Hardness is intentionally unused for stamps
// (predefined tips carry their own falloff). Map modes recolor per texel:
// `levels` shapes the tip lightness, `bg` is the gradient far end (null
// falls back to `color`, i.e. a foreground-only map). `height` (same dims
// as dst) accumulates relief in lightness mode, scaled by `heightAmt`.
void stamp_dab_host(RGBAf* dst, std::uint32_t w, std::uint32_t h, float cx,
                    float cy, float radius, const StampTip& tip,
                    StampMode mode, float opacity, const RGBAf& color,
                    float angleDeg,
                    const SelectionMask* selection = nullptr,
                    const PatternTex* tex = nullptr,
                    const DabDensity* den = nullptr,
                    const MaskTip* mask = nullptr,
                    int flip = 0, int filter = 1,
                    const StampLevels* levels = nullptr,
                    const RGBAf* bg = nullptr,
                    float* height = nullptr, float heightAmt = 0.0f);

// Eraser twin: stamp coverage multiplies alpha down, RGB untouched.
// Carves relief too when a height plane is given (same mask).
void stamp_erase_dab_host(RGBAf* dst, std::uint32_t w, std::uint32_t h,
                          float cx, float cy, float radius,
                          const StampTip& tip, float opacity, float angleDeg,
                          const SelectionMask* selection = nullptr,
                          const PatternTex* tex = nullptr,
                          const DabDensity* den = nullptr,
                          const MaskTip* mask = nullptr,
                          int flip = 0, int filter = 1,
                          float* height = nullptr);

// Pattern stamp dab: source-over the procedural tile (pattern.h) under the
// dab mask. Tile pixel = ((x - ox) mod 64, (y - oy) mod 64) in layer space;
// `opacity` scales the mask. parallel_rows over circle rows.
bool pattern_stamp_dab_host(RGBAf* dst, std::uint32_t w, std::uint32_t h,
                            float cx, float cy, float radius, float hardness,
                            float opacity, const PatternTile& tile, float ox,
                            float oy, int* bboxOut,
                            const SelectionMask* selection = nullptr);

}  // namespace pittore::compute
