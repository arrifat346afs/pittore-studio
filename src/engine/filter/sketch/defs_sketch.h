#pragma once
// Sketch filter declarations. Split from engine/filter/filters.h.
#include <vector>

#include "engine/filter/core/filter_types.h"

namespace pittore::filter {

inline std::vector<FilterDef> SketchFilterDefs() {
    return {
        {"halftone_pattern", "Halftone Pattern", "Sketch", {{{"size", "Size", 1, 50, 6, 0, {}, ""}, {"contrast", "Contrast", 0, 50, 10, 0, {}, ""}, {"pattern", "Pattern", 0, 2, 0, 1, {"Circle", "Dot", "Line"}, ""}}}},
        {"bas_relief", "Bas Relief", "Sketch", {{{"detail", "Detail", 1, 15, 8, 0, {}, ""}, {"smoothness", "Smoothness", 0, 15, 3, 0, {}, ""}, {"light", "Light Direction", 0, 3, 0, 1, {"Bottom", "Top", "Right", "Left"}, ""}}}},
        {"chalk_charcoal", "Chalk & Charcoal", "Sketch", {{{"charcoal", "Charcoal Area", 0, 20, 6, 0, {}, ""}, {"chalk", "Chalk Area", 0, 20, 6, 0, {}, ""}, {"pressure", "Stroke Pressure", 0, 5, 1, 0, {}, ""}}}},
        {"charcoal", "Charcoal", "Sketch", {{{"thickness", "Charcoal Thickness", 1, 10, 3, 0, {}, ""}, {"detail", "Detail", 0, 10, 5, 0, {}, ""}, {"balance", "Light/Dark Balance", 0, 100, 50, 0, {}, ""}}}},
        {"chrome", "Chrome", "Sketch", {{{"detail", "Detail", 0, 10, 4, 0, {}, ""}, {"smoothness", "Smoothness", 0, 10, 4, 0, {}, ""}}}},
        {"conte_crayon", "Conte Crayon", "Sketch", {{{"foreground", "Foreground Level", 1, 5, 2, 0, {}, ""}, {"background", "Background Level", 1, 5, 2, 0, {}, ""}, {"texture", "Texture", 0, 3, 0, 1, {"Canvas", "Sandstone", "Burlap", "Brick"}, ""}, {"scaling", "Scaling", 50, 200, 100, 0, {}, " %"}, {"relief", "Relief", 0, 50, 4, 0, {}, ""}, {"light", "Light Direction", 0, 7, 7, 1, {"Top", "Top Right", "Right", "Bottom Right", "Bottom", "Bottom Left", "Left", "Top Left"}, ""}, {"invert", "Invert", 0, 1, 0, 2, {}, ""}}}},
        {"graphic_pen", "Graphic Pen", "Sketch", {{{"length", "Stroke Length", 1, 15, 7, 0, {}, ""}, {"balance", "Light/Dark Balance", 0, 100, 50, 0, {}, ""}, {"direction", "Stroke Direction", 0, 3, 0, 1, {"Right Diagonal", "Horizontal", "Left Diagonal", "Vertical"}, ""}}}},
        {"note_paper", "Note Paper", "Sketch", {{{"balance", "Image Balance", 0, 50, 25, 0, {}, ""}, {"graininess", "Graininess", 0, 50, 10, 0, {}, ""}, {"relief", "Relief", 0, 50, 8, 0, {}, ""}}}},
        {"photocopy", "Photocopy", "Sketch", {{{"detail", "Detail", 1, 24, 9, 0, {}, ""}, {"darkness", "Darkness", 1, 50, 25, 0, {}, ""}}}},
        {"plaster", "Plaster", "Sketch", {{{"balance", "Image Balance", 0, 50, 25, 0, {}, ""}, {"smoothness", "Smoothness", 0, 50, 5, 0, {}, ""}, {"relief", "Relief", 0, 50, 8, 0, {}, ""}, {"light", "Light Direction", 0, 3, 0, 1, {"Bottom", "Top", "Right", "Left"}, ""}}}},
        {"reticulation", "Reticulation", "Sketch", {{{"density", "Density", 0, 50, 12, 0, {}, ""}, {"foreground", "Foreground Level", 0, 50, 40, 0, {}, ""}, {"background", "Background Level", 0, 50, 10, 0, {}, ""}}}},
        {"stamp", "Stamp", "Sketch", {{{"balance", "Light/Dark Balance", 1, 50, 25, 0, {}, ""}, {"smoothness", "Smoothness", 0, 50, 10, 0, {}, ""}}}},
        {"torn_edges", "Torn Edges", "Sketch", {{{"balance", "Image Balance", 1, 50, 25, 0, {}, ""}, {"smoothness", "Smoothness", 0, 50, 12, 0, {}, ""}, {"contrast", "Contrast", 0, 100, 50, 0, {}, ""}}}},
        {"water_paper", "Water Paper", "Sketch", {{{"fiber", "Fiber Length", 3, 50, 12, 0, {}, ""}, {"brightness", "Brightness", 0, 100, 60, 0, {}, ""}, {"contrast", "Contrast", 0, 100, 70, 0, {}, ""}}}},
    };
}

}  // namespace pittore::filter
