#pragma once
// Lens filter declarations. Split from engine/filter/filters.h.
#include <vector>

#include "engine/filter/core/filter_types.h"

namespace pittore::filter {

inline std::vector<FilterDef> LensFilterDefs() {
    return {
        {"lens_correction", "Lens Correction", "Lens", {{{"distortion", "Geometric Distortion", -100, 100, 0, 0, {}, ""}, {"red", "Red Fringe", -50, 50, 0, 0, {}, ""}, {"blue", "Blue Fringe", -50, 50, 0, 0, {}, ""}, {"vignette", "Vignette Amount", -100, 100, 0, 0, {}, ""}, {"midpoint", "Vignette Midpoint", 0, 100, 50, 0, {}, ""}, {"vertical", "Vertical Perspective", -100, 100, 0, 0, {}, ""}, {"horizontal", "Horizontal Perspective", -100, 100, 0, 0, {}, ""}, {"angle", "Angle", -180, 180, 0, 0, {}, " deg"}, {"scale", "Scale", 50, 200, 100, 0, {}, " %"}}}},
        {"adaptive_wide_angle", "Adaptive Wide Angle", "Lens", {{{"projection", "Projection", 0, 2, 0, 1, {"Fisheye", "Perspective", "Full Spherical"}, ""}, {"focal", "Focal Length", 4, 60, 14, 0, {}, " mm"}, {"crop", "Crop Factor", 0.5, 3, 1, 0, {}, " x"}, {"scale", "Scale", 50, 200, 100, 0, {}, " %"}}}},
        {"camera_raw", "Camera Raw", "Lens", {{{"temperature", "Temperature", -100, 100, 0, 0, {}, ""}, {"tint", "Tint", -100, 100, 0, 0, {}, ""}, {"exposure", "Exposure", -5, 5, 0, 0, {}, " EV"}, {"contrast", "Contrast", -100, 100, 0, 0, {}, ""}, {"highlights", "Highlights", -100, 100, 0, 0, {}, ""}, {"shadows", "Shadows", -100, 100, 0, 0, {}, ""}, {"whites", "Whites", -100, 100, 0, 0, {}, ""}, {"blacks", "Blacks", -100, 100, 0, 0, {}, ""}, {"clarity", "Clarity", -100, 100, 0, 0, {}, ""}, {"dehaze", "Dehaze", -100, 100, 0, 0, {}, ""}, {"vibrance", "Vibrance", -100, 100, 0, 0, {}, ""}, {"saturation", "Saturation", -100, 100, 0, 0, {}, ""}, {"sharpening", "Sharpening", 0, 150, 0, 0, {}, ""}, {"noise", "Noise Reduction", 0, 100, 0, 0, {}, ""}, {"vignette", "Vignetting", -100, 100, 0, 0, {}, ""}}}},
    };
}

}  // namespace pittore::filter
