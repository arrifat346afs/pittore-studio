#pragma once
// Render filter declarations. Split from engine/filter/filters.h.
#include <vector>

#include "engine/filter/core/filter_types.h"

namespace pittore::filter {

inline std::vector<FilterDef> RenderFilterDefs() {
    return {
        {"clouds", "Clouds", "Render", {{{"seed", "Random Seed", 0, 999, 1, 0, {}, ""}}}},
        {"difference_clouds", "Difference Clouds", "Render", {{{"seed", "Random Seed", 0, 999, 2, 0, {}, ""}}}},
        {"fibers", "Fibers", "Render", {{{"variance", "Variance", 1, 64, 16, 0, {}, ""}, {"strength", "Strength", 1, 64, 8, 0, {}, ""}, {"seed", "Random Seed", 0, 999, 1, 0, {}, ""}}}},
        {"lens_flare", "Lens Flare", "Render", {{{"x", "Center X", 0, 100, 50, 0, {}, " %"}, {"y", "Center Y", 0, 100, 50, 0, {}, " %"}, {"brightness", "Brightness", 10, 300, 100, 0, {}, " %"}, {"lens", "Lens Type", 0, 3, 0, 1, {"50-300mm Zoom", "35mm Prime", "105mm Prime", "Movie Prime"}, ""}}}},
        {"lighting_effects", "Lighting Effects", "Render", {{{"type", "Light Type", 0, 2, 0, 1, {"Spotlight", "Point Light", "Infinite Light"}, ""}, {"x", "Position X", 0, 100, 30, 0, {}, " %"}, {"y", "Position Y", 0, 100, 25, 0, {}, " %"}, {"angle", "Angle", 0, 360, 45, 0, {}, " deg"}, {"intensity", "Intensity", 0, 300, 120, 0, {}, " %"}, {"spread", "Spread", 5, 200, 60, 0, {}, " %"}, {"ambience", "Ambience", 0, 100, 35, 0, {}, " %"}, {"gloss", "Gloss", 0, 100, 30, 0, {}, " %"}, {"height", "Height", 0, 200, 60, 0, {}, " %"}}}},
        {"flame", "Flame", "Render", {{{"count", "Flames", 1, 20, 3, 0, {}, ""}, {"height", "Height", 10, 200, 80, 0, {}, " px"}, {"width", "Width", 10, 200, 40, 0, {}, " px"}, {"angle", "Angle", -180, 180, 0, 0, {}, " deg"}, {"turbulence", "Turbulence", 0, 100, 30, 0, {}, ""}, {"opacity", "Opacity", 0, 100, 100, 0, {}, " %"}, {"seed", "Random Seed", 0, 999, 1, 0, {}, ""}}}},
        {"picture_frame", "Picture Frame", "Render", {{{"style", "Style", 0, 4, 1, 1, {"Plain", "Beveled", "Matted", "Rounded", "Ornate"}, ""}, {"width", "Width", 1, 40, 8, 0, {}, " %"}, {"tone", "Tone", 0, 100, 25, 0, {}, ""}, {"relief", "Relief", 0, 100, 60, 0, {}, ""}}}},
        {"tree", "Tree", "Render", {{{"height", "Height", 20, 100, 70, 0, {}, " %"}, {"thickness", "Trunk Thickness", 1, 100, 30, 0, {}, ""}, {"spread", "Branch Spread", 5, 90, 32, 0, {}, " deg"}, {"leaves", "Leaves", 0, 100, 70, 0, {}, ""}, {"size", "Leaf Size", 1, 100, 40, 0, {}, ""}, {"light", "Light Direction", -100, 100, -50, 0, {}, ""}, {"seed", "Random Seed", 0, 999, 7, 0, {}, ""}}}},
    };
}

}  // namespace pittore::filter
