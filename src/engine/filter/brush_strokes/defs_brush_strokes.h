#pragma once
// Brush Strokes filter declarations. Split from engine/filter/filters.h.
#include <vector>

#include "engine/filter/core/filter_types.h"

namespace pittore::filter {

inline std::vector<FilterDef> BrushStrokesFilterDefs() {
    return {
        {"accented_edges", "Accented Edges", "Brush Strokes", {{{"width", "Edge Width", 1, 14, 2, 0, {}, ""}, {"brightness", "Edge Brightness", 0, 50, 38, 0, {}, ""}, {"smoothness", "Smoothness", 1, 15, 5, 0, {}, ""}}}},
        {"angled_strokes", "Angled Strokes", "Brush Strokes", {{{"balance", "Direction Balance", 0, 100, 50, 0, {}, ""}, {"length", "Stroke Length", 3, 50, 15, 0, {}, ""}, {"sharpness", "Sharpness", 0, 10, 3, 0, {}, ""}}}},
        {"crosshatch", "Crosshatch", "Brush Strokes", {{{"length", "Stroke Length", 3, 50, 9, 0, {}, ""}, {"sharpness", "Sharpness", 0, 20, 6, 0, {}, ""}, {"strength", "Strength", 1, 3, 1, 0, {}, ""}}}},
        {"dark_strokes", "Dark Strokes", "Brush Strokes", {{{"balance", "Balance", 0, 10, 5, 0, {}, ""}, {"black", "Black Intensity", 0, 10, 6, 0, {}, ""}, {"white", "White Intensity", 0, 10, 2, 0, {}, ""}}}},
        {"ink_outlines", "Ink Outlines", "Brush Strokes", {{{"length", "Stroke Length", 1, 50, 4, 0, {}, ""}, {"dark", "Dark Intensity", 0, 50, 20, 0, {}, ""}, {"light", "Light Intensity", 0, 50, 10, 0, {}, ""}}}},
        {"spatter", "Spatter", "Brush Strokes", {{{"radius", "Spray Radius", 0, 25, 10, 0, {}, ""}, {"smoothness", "Smoothness", 1, 15, 5, 0, {}, ""}}}},
        {"sprayed_strokes", "Sprayed Strokes", "Brush Strokes", {{{"length", "Stroke Length", 0, 20, 12, 0, {}, ""}, {"radius", "Spray Radius", 0, 25, 7, 0, {}, ""}, {"direction", "Stroke Direction", 0, 3, 0, 1, {"Right Diagonal", "Horizontal", "Left Diagonal", "Vertical"}, ""}}}},
        {"sumi_e", "Sumi-e", "Brush Strokes", {{{"width", "Stroke Width", 3, 50, 12, 0, {}, ""}, {"pressure", "Stroke Pressure", 0, 15, 3, 0, {}, ""}, {"contrast", "Contrast", 0, 40, 20, 0, {}, ""}}}},
    };
}

}  // namespace pittore::filter
