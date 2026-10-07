#pragma once
// Distort filter declarations. Split from engine/filter/filters.h.
#include <vector>

#include "engine/filter/core/filter_types.h"

namespace pittore::filter {

inline std::vector<FilterDef> DistortFilterDefs() {
    return {
        {"displace", "Displace", "Distort", {{{"hscale", "Horizontal Scale", 0, 100, 20, 0, {}, " %"}, {"vscale", "Vertical Scale", 0, 100, 20, 0, {}, " %"}, {"fit", "Fit", 0, 1, 0, 1, {"Stretch To Fit", "Tile"}, ""}, {"undefined", "Undefined Areas", 0, 1, 0, 1, {"Repeat Edge Pixels", "Wrap Around"}, ""}}}},
        {"pinch", "Pinch", "Distort", {{{"amount", "Amount", -100, 100, 50, 0, {}, " %"}, {"mode", "Mode", 0, 2, 0, 1, {"Normal", "Horizontal Only", "Vertical Only"}, ""}}}},
        {"polar", "Polar Coordinates", "Distort", {{{"direction", "Direction", 0, 1, 1, 1, {"Polar To Rectangular", "Rectangular To Polar"}, ""}}}},
        {"ripple", "Ripple", "Distort", {{{"amount", "Amount", -999, 999, 100, 0, {}, ""}, {"size", "Size", 1, 64, 12, 0, {}, " px"}}}},
        {"shear", "Shear", "Distort", {{{"amount", "Amount", -200, 200, 40, 0, {}, " px"}, {"curve", "Curve", 0, 2, 0, 1, {"Bow", "S-Curve", "Ramp"}, ""}, {"undefined", "Undefined Areas", 0, 1, 0, 1, {"Repeat Edge Pixels", "Wrap Around"}, ""}}}},
        {"spherize", "Spherize", "Distort", {{{"amount", "Amount", -100, 100, 50, 0, {}, " %"}, {"mode", "Mode", 0, 2, 0, 1, {"Normal", "Horizontal Only", "Vertical Only"}, ""}}}},
        {"twirl", "Twirl", "Distort", {{{"angle", "Angle", -999, 999, 50, 0, {}, " deg"}}}},
        {"wave", "Wave", "Distort", {{{"generators", "Number Of Generators", 1, 8, 1, 0, {}, ""}, {"wavelength", "Wavelength", 1, 400, 60, 0, {}, " px"}, {"amplitude", "Amplitude", 0, 200, 15, 0, {}, " px"}, {"horizontal", "Horizontal Scale", 0, 100, 100, 0, {}, " %"}, {"vertical", "Vertical Scale", 0, 100, 100, 0, {}, " %"}, {"type", "Type", 0, 2, 0, 1, {"Sine", "Triangle", "Square"}, ""}, {"seed", "Random Seed", 0, 999, 1, 0, {}, ""}}}},
        {"zigzag", "ZigZag", "Distort", {{{"amount", "Amount", -100, 100, 30, 0, {}, ""}, {"ridges", "Ridges", 1, 20, 5, 0, {}, ""}, {"style", "Style", 0, 2, 2, 1, {"Around Center", "Out From Center", "Pond Ripples"}, ""}}}},
        {"glass", "Glass", "Distort", {{{"distortion", "Distortion", 0, 20, 5, 0, {}, ""}, {"smoothness", "Smoothness", 1, 15, 3, 0, {}, ""}, {"texture", "Texture", 0, 5, 0, 1, {"Frosted", "Blocks", "Canvas", "Sandstone", "Burlap", "Brick"}, ""}, {"scaling", "Scaling", 50, 200, 100, 0, {}, " %"}}}},
        {"ocean_ripple", "Ocean Ripple", "Distort", {{{"size", "Ripple Size", 1, 15, 9, 0, {}, ""}, {"magnitude", "Ripple Magnitude", 0, 20, 9, 0, {}, ""}}}},
    };
}

}  // namespace pittore::filter
