#pragma once
// Eraser dab: alpha multiplied down under the dab mask, RGB untouched.
// Split from engine/compute/paint.h.
#include <cstdint>

#include "engine/compute/brushes/selection_mask/selection_mask.h"
#include "engine/compute/brushes/mask/mask.h"
#include "engine/compute/brushes/texture/texture.h"
#include "engine/compute/brushes/tip/tip.h"
#include "engine/core/pixel.h"

namespace pittore::compute {

void erase_dab_host(RGBAf* dst, std::uint32_t w, std::uint32_t h, float cx,
                    float cy, float radius, float hardness, float opacity,
                    const SelectionMask* selection = nullptr,
                    const PatternTex* tex = nullptr,
                    const DabDensity* den = nullptr,
                    const MaskTip* mask = nullptr);

// Auto-tip twin: same ellipse/square measure as paint_tip_dab_host.
void erase_tip_dab_host(RGBAf* dst, std::uint32_t w, std::uint32_t h, float cx,
                        float cy, float radius, const AutoTip& tip,
                        float opacity,
                        const SelectionMask* selection = nullptr,
                        const PatternTex* tex = nullptr,
                        const DabDensity* den = nullptr,
                        const MaskTip* mask = nullptr);

// Background Eraser dab (discontiguous): erase pixels in the dab circle
// whose RGB is within `tolerance` of `sample` (max-channel metric, ramped).
// `protectFg` skips pixels also matching `fg`. RGB untouched, alpha
// multiplied down by mask * match * opacity. Bbox written to bboxOut.
// parallel_rows over circle rows (disjoint dst rows, read-only sample).
bool background_erase_dab_host(RGBAf* dst, std::uint32_t w, std::uint32_t h,
                               float cx, float cy, float radius,
                               float hardness, float opacity,
                               const RGBAf& sample, float tolerance,
                               bool protectFg, const RGBAf& fg, int* bboxOut,
                               const SelectionMask* selection = nullptr);

}  // namespace pittore::compute
