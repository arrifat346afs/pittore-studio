#pragma once
// Stylize filter declarations. Split from engine/filter/filters.h.
#include <vector>

#include "engine/filter/core/filter_types.h"

namespace pittore::filter {

inline std::vector<FilterDef> StylizeFilterDefs() {
    return {
        {"diffuse", "Diffuse", "Stylize", {{{"amount", "Amount", 1, 32, 4, 0, {}, " px"}, {"mode", "Mode", 0, 3, 0, 1, {"Normal", "Darken Only", "Lighten Only", "Anisotropic"}, ""}}}},
        {"diffuse_glow", "Diffuse Glow", "Stylize", {{{"graininess", "Graininess", 0, 10, 4, 0, {}, ""}, {"glow", "Glow Amount", 0, 20, 10, 0, {}, ""}, {"clear", "Clear Amount", 0, 20, 10, 0, {}, ""}}}},
        {"emboss", "Emboss", "Stylize", {{{"angle", "Angle", -180, 180, 135, 0, {}, " deg"}, {"height", "Height", 1, 10, 3, 0, {}, " px"}, {"amount", "Amount", 1, 500, 100, 0, {}, " %"}}}},
        {"extrude", "Extrude", "Stylize", {{{"type", "Type", 0, 1, 0, 1, {"Blocks", "Pyramids"}, ""}, {"size", "Size", 2, 64, 12, 0, {}, " px"}, {"depth", "Depth", 1, 200, 30, 0, {}, " px"}, {"basis", "Basis", 0, 1, 0, 1, {"Level", "Random"}, ""}, {"solid", "Solid Front Faces", 0, 1, 0, 2, {}, ""}}}},
        {"find_edges", "Find Edges", "Stylize", {{{"strength", "Strength", 0, 100, 100, 0, {}, " %"}}}},
        {"glowing_edges", "Glowing Edges", "Stylize", {{{"width", "Edge Width", 1, 14, 2, 0, {}, ""}, {"brightness", "Edge Brightness", 0, 20, 6, 0, {}, ""}, {"smoothness", "Smoothness", 1, 15, 5, 0, {}, ""}}}},
        {"oil_paint", "Oil Paint", "Stylize", {{{"radius", "Brush Size", 1, 12, 4, 0, {}, " px"}, {"levels", "Stylization", 2, 64, 20, 0, {}, ""}, {"bristle", "Bristle Detail", 0, 10, 4, 0, {}, ""}, {"shine", "Shine", 0, 10, 2, 0, {}, ""}, {"angle", "Lighting Angle", 0, 360, 45, 0, {}, " deg"}}}},
        {"solarize", "Solarize", "Stylize", {{{"strength", "Strength", 0, 100, 100, 0, {}, " %"}}}},
        {"tiles", "Tiles", "Stylize", {{{"count", "Number Of Tiles", 2, 64, 8, 0, {}, ""}, {"maxoffset", "Maximum Offset", 0, 100, 25, 0, {}, " %"}, {"fill", "Fill Empty Area With", 0, 4, 1, 1, {"Transparent", "Background Color", "Foreground Color", "Inverse Image", "Unaltered Image"}, ""}}}},
        {"trace_contour", "Trace Contour", "Stylize", {{{"level", "Level", 0, 255, 128, 0, {}, ""}, {"edge", "Edge", 0, 1, 0, 1, {"Lower", "Upper"}, ""}}}},
        {"wind", "Wind", "Stylize", {{{"strength", "Strength", 1, 100, 20, 0, {}, " px"}, {"method", "Method", 0, 2, 0, 1, {"Wind", "Blast", "Stagger"}, ""}, {"direction", "Direction", 0, 1, 0, 1, {"From The Left", "From The Right"}, ""}}}},
    };
}

}  // namespace pittore::filter
