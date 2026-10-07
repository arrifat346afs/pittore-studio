#pragma once
// 3D filter declarations. Split from engine/filter/filters.h.
#include <vector>

#include "engine/filter/core/filter_types.h"

namespace pittore::filter {

inline std::vector<FilterDef> ThreeDFilterDefs() {
    return {
        {"bump_map", "Generate Bump Map", "3D", {{{"blur", "Blur Detail", 0, 2, 1, 1, {"High", "Medium", "Low"}, ""}, {"contrast", "Contrast", 0, 100, 30, 0, {}, ""}, {"invert", "Invert Height", 0, 1, 0, 2, {}, ""}}}},
        {"normal_map", "Generate Normal Map", "3D", {{{"blur", "Blur Detail", 0, 2, 1, 1, {"High", "Medium", "Low"}, ""}, {"contrast", "Contrast", 0, 100, 30, 0, {}, ""}, {"strength", "Strength", 1, 100, 30, 0, {}, ""}, {"invert", "Invert Height", 0, 1, 0, 2, {}, ""}}}},
    };
}

}  // namespace pittore::filter
