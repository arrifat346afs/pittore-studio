#pragma once
// Noise filter declarations. Split from engine/filter/filters.h.
#include <vector>

#include "engine/filter/core/filter_types.h"

namespace pittore::filter {

inline std::vector<FilterDef> NoiseFilterDefs() {
    return {
        {"add_noise", "Add Noise", "Noise", {{{"amount", "Amount", 0, 100, 10, 0, {}, " %"}, {"distribution", "Distribution", 0, 1, 0, 1, {"Uniform", "Gaussian"}, ""}, {"monochrome", "Monochromatic", 0, 1, 1, 2, {}, ""}}}},
        {"despeckle", "Despeckle", "Noise", {{{"strength", "Strength", 0, 100, 100, 0, {}, " %"}}}},
        {"dust_scratches", "Dust & Scratches", "Noise", {{{"radius", "Radius", 1, 16, 2, 0, {}, " px"}, {"threshold", "Threshold", 0, 255, 20, 0, {}, ""}}}},
        {"median", "Median", "Noise", {{{"radius", "Radius", 1, 100, 2, 0, {}, " px"}}}},
        {"reduce_noise", "Reduce Noise", "Noise", {{{"strength", "Strength", 0, 10, 5, 0, {}, ""}, {"detail", "Preserve Detail", 0, 100, 60, 0, {}, " %"}, {"colour", "Reduce Color Noise", 0, 100, 50, 0, {}, " %"}, {"sharpen", "Sharpen Details", 0, 100, 25, 0, {}, " %"}, {"jpeg", "Remove JPEG Artifact", 0, 1, 0, 2, {}, ""}}}},
    };
}

}  // namespace pittore::filter
