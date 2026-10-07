#pragma once
// Pixelate filter declarations. Split from engine/filter/filters.h.
#include <vector>

#include "engine/filter/core/filter_types.h"

namespace pittore::filter {

inline std::vector<FilterDef> PixelateFilterDefs() {
    return {
        {"color_halftone", "Color Halftone", "Pixelate", {{{"radius", "Max Radius", 2, 64, 8, 0, {}, " px"}, {"c1", "Channel 1", 0, 360, 108, 0, {}, " deg"}, {"c2", "Channel 2", 0, 360, 162, 0, {}, " deg"}, {"c3", "Channel 3", 0, 360, 90, 0, {}, " deg"}, {"c4", "Channel 4", 0, 360, 45, 0, {}, " deg"}}}},
        {"crystallize", "Crystallize", "Pixelate", {{{"size", "Cell Size", 3, 300, 12, 0, {}, " px"}}}},
        {"facet", "Facet", "Pixelate", {{{"strength", "Strength", 0, 100, 100, 0, {}, " %"}}}},
        {"fragment", "Fragment", "Pixelate", {{{"offset", "Offset", 1, 32, 4, 0, {}, " px"}}}},
        {"mezzotint", "Mezzotint", "Pixelate", {{{"type", "Type", 0, 9, 1, 1, {"Fine Dots", "Medium Dots", "Grainy Dots", "Coarse Dots", "Short Lines", "Medium Lines", "Long Lines", "Short Strokes", "Medium Strokes", "Long Strokes"}, ""}, {"grain", "Grain", 1, 16, 2, 0, {}, " px"}}}},
        {"mosaic", "Mosaic", "Pixelate", {{{"size", "Cell Size", 2, 200, 10, 0, {}, " px"}}}},
        {"pointillize", "Pointillize", "Pixelate", {{{"size", "Cell Size", 3, 300, 8, 0, {}, " px"}}}},
    };
}

}  // namespace pittore::filter
