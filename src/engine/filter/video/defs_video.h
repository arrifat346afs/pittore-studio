#pragma once
// Video filter declarations. Split from engine/filter/filters.h.
#include <vector>

#include "engine/filter/core/filter_types.h"

namespace pittore::filter {

inline std::vector<FilterDef> VideoFilterDefs() {
    return {
        {"deinterlace", "De-Interlace", "Video", {{{"field", "Field", 0, 1, 0, 1, {"Odd Fields", "Even Fields"}, ""}, {"fill", "Fill", 0, 1, 0, 1, {"Interpolation", "Duplication"}, ""}}}},
        {"ntsc_colors", "NTSC Colors", "Video", {{{"strength", "Strength", 0, 100, 100, 0, {}, " %"}}}},
    };
}

}  // namespace pittore::filter
