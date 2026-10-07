#pragma once
// Blur filter declarations. Split from engine/filter/filters.h.
#include <vector>

#include "engine/filter/core/filter_types.h"

namespace pittore::filter {

inline std::vector<FilterDef> BlurFilterDefs() {
    return {
        {"average", "Average", "Blur", {{{"strength", "Strength", 0, 100, 100, 0, {}, " %"}}}},
        {"blur", "Blur", "Blur", {{{"strength", "Strength", 0, 100, 100, 0, {}, " %"}}}},
        {"blur_more", "Blur More", "Blur", {{{"strength", "Strength", 0, 100, 100, 0, {}, " %"}}}},
        {"box_blur", "Box Blur", "Blur", {{{"radius", "Radius", 0, 100, 4, 0, {}, " px"}}}},
        {"gaussian_blur", "Gaussian Blur", "Blur", {{{"radius", "Radius", 0, 100, 4, 0, {}, " px"}}}},
        {"lens_blur", "Lens Blur", "Blur", {{{"radius", "Radius", 0, 50, 8, 0, {}, " px"}, {"shape", "Shape", 0, 6, 0, 1, {"Circle", "Triangle", "Square", "Pentagon", "Hexagon", "Heptagon", "Octagon"}, ""}, {"brightness", "Brightness", 0, 100, 0, 0, {}, ""}, {"threshold", "Threshold", 0, 255, 200, 0, {}, ""}, {"noise", "Noise", 0, 50, 0, 0, {}, ""}}}},
        {"motion_blur", "Motion Blur", "Blur", {{{"angle", "Angle", -360, 360, 0, 0, {}, " deg"}, {"distance", "Distance", 1, 500, 20, 0, {}, " px"}}}},
        {"radial_blur", "Radial Blur", "Blur", {{{"amount", "Amount", 1, 100, 10, 0, {}, ""}, {"method", "Method", 0, 1, 0, 1, {"Spin", "Zoom"}, ""}, {"quality", "Quality", 0, 2, 1, 1, {"Draft", "Good", "Best"}, ""}, {"x", "Center X", 0, 100, 50, 0, {}, " %"}, {"y", "Center Y", 0, 100, 50, 0, {}, " %"}}}},
        {"shape_blur", "Shape Blur", "Blur", {{{"radius", "Radius", 1, 60, 10, 0, {}, " px"}, {"shape", "Shape", 0, 5, 0, 1, {"Square", "Diamond", "Hexagon", "Cross", "Ring", "Star"}, ""}}}},
        {"smart_blur", "Smart Blur", "Blur", {{{"radius", "Radius", 0.1, 100, 5, 0, {}, " px"}, {"threshold", "Threshold", 0.1, 100, 25, 0, {}, ""}, {"mode", "Mode", 0, 2, 0, 1, {"Normal", "Edge Only", "Overlay Edge"}, ""}}}},
        {"surface_blur", "Surface Blur", "Blur", {{{"radius", "Radius", 1, 100, 5, 0, {}, " px"}, {"threshold", "Threshold", 2, 255, 15, 0, {}, ""}}}},
    };
}

}  // namespace pittore::filter
