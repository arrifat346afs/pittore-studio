#pragma once
// Single circular paint dab, straight-alpha source-over.
// Split from engine/compute/paint.h.
#include <cstdint>

#include "engine/compute/brushes/selection_mask/selection_mask.h"
#include "engine/compute/brushes/mask/mask.h"
#include "engine/compute/brushes/texture/texture.h"
#include "engine/compute/brushes/tip/tip.h"
#include "engine/core/pixel.h"

namespace pittore::compute {

void paint_dab_host(RGBAf* dst, std::uint32_t w, std::uint32_t h, float cx,
                    float cy, float radius, float hardness, float opacity,
                    const RGBAf& color, const SelectionMask* selection = nullptr,
                    const PatternTex* tex = nullptr,
                    const DabDensity* den = nullptr,
                    const MaskTip* mask = nullptr);

// Auto-tip dab: ellipse (ratio + angle) or square silhouette with the same
// hardness/opacity semantics. ratio==1/angle==0 reproduces paint_dab_host.
void paint_tip_dab_host(RGBAf* dst, std::uint32_t w, std::uint32_t h, float cx,
                        float cy, float radius, const AutoTip& tip,
                        float opacity, const RGBAf& color,
                        const SelectionMask* selection = nullptr,
                        const PatternTex* tex = nullptr,
                        const DabDensity* den = nullptr,
                        const MaskTip* mask = nullptr);

}  // namespace pittore::compute
