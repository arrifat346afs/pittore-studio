#pragma once
// Other filter declarations. Split from engine/filter/filters.h.
#include <vector>

#include "engine/filter/core/filter_types.h"

namespace pittore::filter {

inline std::vector<FilterDef> OtherFilterDefs() {
    return {
        {"high_pass", "High Pass", "Other", {{{"radius", "Radius", 0.1, 250, 3, 0, {}, " px"}}}},
        {"maximum", "Maximum", "Other", {{{"radius", "Radius", 1, 40, 2, 0, {}, " px"}, {"preserve", "Preserve", 0, 1, 1, 1, {"Squareness", "Roundness"}, ""}}}},
        {"minimum", "Minimum", "Other", {{{"radius", "Radius", 1, 40, 2, 0, {}, " px"}, {"preserve", "Preserve", 0, 1, 1, 1, {"Squareness", "Roundness"}, ""}}}},
        {"custom", "Custom Kernel", "Other", {{{"center", "Center Weight", 1, 9, 5, 0, {}, ""}, {"scale", "Scale", 0.5, 10, 1, 0, {}, ""}, {"offset", "Offset", -255, 255, 0, 0, {}, ""}}}},
        {"hsb_hsl", "HSB/HSL", "Other", {{{"mode", "Mode", 0, 3, 0, 1, {"RGB To HSB", "RGB To HSL", "HSB To RGB", "HSL To RGB"}, ""}}}},
        {"offset", "Offset", "Other", {{{"horizontal", "Horizontal", -2000, 2000, 0, 0, {}, " px"}, {"vertical", "Vertical", -2000, 2000, 0, 0, {}, " px"}, {"undefined", "Undefined Areas", 0, 2, 2, 1, {"Set To Transparent", "Repeat Edge Pixels", "Wrap Around"}, ""}}}},
    };
}

}  // namespace pittore::filter
