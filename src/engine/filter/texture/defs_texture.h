#pragma once
// Texture filter declarations. Split from engine/filter/filters.h.
#include <vector>

#include "engine/filter/core/filter_types.h"

namespace pittore::filter {

inline std::vector<FilterDef> TextureFilterDefs() {
    return {
        {"craquelure", "Craquelure", "Texture", {{{"spacing", "Crack Spacing", 2, 100, 15, 0, {}, ""}, {"depth", "Crack Depth", 0, 10, 6, 0, {}, ""}, {"brightness", "Crack Brightness", 0, 10, 9, 0, {}, ""}}}},
        {"grain", "Grain", "Texture", {{{"intensity", "Intensity", 0, 100, 40, 0, {}, ""}, {"contrast", "Contrast", 0, 100, 50, 0, {}, ""}, {"kind", "Grain Type", 0, 9, 0, 1, {"Regular", "Soft", "Sprinkles", "Clumped", "Contrasty", "Enlarged", "Stippled", "Horizontal", "Vertical", "Speckle"}, ""}}}},
        {"mosaic_tiles", "Mosaic Tiles", "Texture", {{{"size", "Tile Size", 2, 100, 22, 0, {}, ""}, {"grout", "Grout Width", 1, 15, 3, 0, {}, ""}, {"lighten", "Lighten Grout", 0, 10, 9, 0, {}, ""}}}},
        {"patchwork", "Patchwork", "Texture", {{{"size", "Square Size", 0, 10, 4, 0, {}, ""}, {"relief", "Relief", 0, 25, 8, 0, {}, ""}}}},
        {"stained_glass", "Stained Glass", "Texture", {{{"size", "Cell Size", 2, 50, 12, 0, {}, ""}, {"border", "Border Thickness", 1, 20, 4, 0, {}, ""}, {"light", "Light Intensity", 0, 10, 3, 0, {}, ""}}}},
        {"texturizer", "Texturizer", "Texture", {{{"texture", "Texture", 0, 3, 0, 1, {"Canvas", "Sandstone", "Burlap", "Brick"}, ""}, {"scaling", "Scaling", 50, 200, 100, 0, {}, " %"}, {"relief", "Relief", 0, 50, 4, 0, {}, ""}, {"light", "Light Direction", 0, 7, 7, 1, {"Top", "Top Right", "Right", "Bottom Right", "Bottom", "Bottom Left", "Left", "Top Left"}, ""}, {"invert", "Invert", 0, 1, 0, 2, {}, ""}}}},
    };
}

}  // namespace pittore::filter
