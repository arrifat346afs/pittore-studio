#pragma once
// Sharpen filter declarations. Split from engine/filter/filters.h.
#include <vector>

#include "engine/filter/core/filter_types.h"

namespace pittore::filter {

inline std::vector<FilterDef> SharpenFilterDefs() {
    return {
        {"shake_reduction", "Shake Reduction", "Sharpen", {{{"amount", "Amount", 0, 500, 60, 0, {}, " %"}}}},
        {"sharpen", "Sharpen", "Sharpen", {{{"strength", "Strength", 0, 100, 100, 0, {}, " %"}}}},
        {"sharpen_edges", "Sharpen Edges", "Sharpen", {{{"strength", "Strength", 0, 100, 100, 0, {}, " %"}}}},
        {"sharpen_more", "Sharpen More", "Sharpen", {{{"strength", "Strength", 0, 100, 100, 0, {}, " %"}}}},
        {"smart_sharpen", "Smart Sharpen", "Sharpen", {{{"amount", "Amount", 1, 500, 100, 0, {}, " %"}, {"radius", "Radius", 0.1, 64, 1.5, 0, {}, " px"}, {"noise", "Reduce Noise", 0, 100, 10, 0, {}, " %"}, {"remove", "Remove", 0, 2, 1, 1, {"Gaussian Blur", "Lens Blur", "Motion Blur"}, ""}, {"angle", "Angle", 0, 360, 0, 0, {}, " deg"}}}},
        {"unsharp_mask", "Unsharp Mask", "Sharpen", {{{"amount", "Amount", 0, 500, 100, 0, {}, " %"}, {"radius", "Radius", 0.1, 50, 2, 0, {}, " px"}, {"threshold", "Threshold", 0, 255, 0, 0, {}, ""}}}},
    };
}

}  // namespace pittore::filter
