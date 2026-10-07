#pragma once
// Blur Gallery filter declarations. Split from engine/filter/filters.h.
#include <vector>

#include "engine/filter/core/filter_types.h"

namespace pittore::filter {

inline std::vector<FilterDef> BlurGalleryFilterDefs() {
    return {
        {"field_blur", "Field Blur", "Blur Gallery", {{{"blur", "Blur", 0, 200, 15, 0, {}, " px"}, {"position", "Position", 0, 100, 50, 0, {}, " %"}, {"spread", "Spread", 1, 100, 50, 0, {}, " %"}}}},
        {"iris_blur", "Iris Blur", "Blur Gallery", {{{"blur", "Blur", 0, 200, 20, 0, {}, " px"}, {"x", "Center X", 0, 100, 50, 0, {}, " %"}, {"y", "Center Y", 0, 100, 50, 0, {}, " %"}, {"radius", "Radius", 1, 100, 35, 0, {}, " %"}, {"roundness", "Roundness", 0, 100, 100, 0, {}, " %"}, {"feather", "Feather", 0, 100, 50, 0, {}, " %"}, {"shape", "Shape", 0, 6, 0, 1, {"Circle", "Triangle", "Square", "Pentagon", "Hexagon", "Heptagon", "Octagon"}, ""}}}},
        {"tilt_shift", "Tilt-Shift", "Blur Gallery", {{{"blur", "Blur", 0, 200, 24, 0, {}, " px"}, {"position", "Position", 0, 100, 50, 0, {}, " %"}, {"band", "Band", 1, 100, 20, 0, {}, " %"}, {"feather", "Feather", 0, 100, 30, 0, {}, " %"}, {"angle", "Angle", 0, 360, 0, 0, {}, " deg"}}}},
        {"spin_blur", "Spin Blur", "Blur Gallery", {{{"angle", "Angle", 0, 60, 12, 0, {}, " deg"}, {"x", "Center X", 0, 100, 50, 0, {}, " %"}, {"y", "Center Y", 0, 100, 50, 0, {}, " %"}, {"radius", "Radius", 1, 100, 50, 0, {}, " %"}, {"feather", "Feather", 0, 100, 30, 0, {}, " %"}}}},
        {"path_blur", "Path Blur", "Blur Gallery", {{{"speed", "Speed", 0, 100, 50, 0, {}, " %"}, {"angle", "Direction", 0, 360, 0, 0, {}, " deg"}, {"curve", "Curve", -100, 100, 0, 0, {}, " %"}, {"taper", "Taper", 0, 100, 0, 0, {}, " %"}}}},
    };
}

}  // namespace pittore::filter
