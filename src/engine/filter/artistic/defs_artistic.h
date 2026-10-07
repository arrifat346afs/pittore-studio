#pragma once
// Artistic filter declarations. Split from engine/filter/filters.h.
#include <vector>

#include "engine/filter/core/filter_types.h"

namespace pittore::filter {

inline std::vector<FilterDef> ArtisticFilterDefs() {
    return {
        {"film_grain", "Film Grain", "Artistic", {{{"grain", "Grain", 0, 20, 4, 0, {}, ""}, {"highlight", "Highlight Area", 0, 20, 0, 0, {}, ""}, {"intensity", "Intensity", 0, 10, 10, 0, {}, ""}}}},
        {"cutout", "Cutout", "Artistic", {{{"levels", "No. Of Levels", 2, 8, 4, 0, {}, ""}, {"simplicity", "Edge Simplicity", 0, 10, 4, 0, {}, ""}, {"fidelity", "Edge Fidelity", 1, 3, 2, 0, {}, ""}}}},
        {"dry_brush", "Dry Brush", "Artistic", {{{"size", "Brush Size", 0, 10, 2, 0, {}, ""}, {"detail", "Brush Detail", 0, 10, 8, 0, {}, ""}, {"texture", "Texture", 1, 3, 1, 0, {}, ""}}}},
        {"fresco", "Fresco", "Artistic", {{{"size", "Brush Size", 0, 10, 2, 0, {}, ""}, {"detail", "Brush Detail", 0, 10, 8, 0, {}, ""}, {"texture", "Texture", 1, 3, 1, 0, {}, ""}}}},
        {"watercolor", "Watercolor", "Artistic", {{{"detail", "Brush Detail", 1, 14, 9, 0, {}, ""}, {"shadow", "Shadow Intensity", 0, 10, 1, 0, {}, ""}, {"texture", "Texture", 1, 3, 1, 0, {}, ""}}}},
        {"poster_edges", "Poster Edges", "Artistic", {{{"thickness", "Edge Thickness", 0, 10, 2, 0, {}, ""}, {"intensity", "Edge Intensity", 0, 10, 1, 0, {}, ""}, {"levels", "Posterization", 0, 6, 2, 0, {}, ""}}}},
        {"plastic_wrap", "Plastic Wrap", "Artistic", {{{"strength", "Highlight Strength", 0, 20, 15, 0, {}, ""}, {"detail", "Detail", 1, 15, 9, 0, {}, ""}, {"smoothness", "Smoothness", 1, 15, 7, 0, {}, ""}}}},
        {"colored_pencil", "Colored Pencil", "Artistic", {{{"width", "Pencil Width", 1, 24, 6, 0, {}, ""}, {"pressure", "Stroke Pressure", 0, 15, 4, 0, {}, ""}, {"paper", "Paper Brightness", 0, 50, 25, 0, {}, ""}, {"detail", "Stroke Detail", 1, 3, 2, 0, {}, ""}, {"grain", "Paper Grain", 0, 50, 15, 0, {}, ""}, {"saturation", "Saturation", 0, 200, 100, 0, {}, ""}}}},
        {"neon_glow", "Neon Glow", "Artistic", {{{"size", "Glow Size", -24, 24, 4, 0, {}, ""}, {"brightness", "Glow Brightness", 0, 50, 15, 0, {}, ""}}}},
        {"paint_daubs", "Paint Daubs", "Artistic", {{{"size", "Brush Size", 1, 50, 8, 0, {}, ""}, {"sharpness", "Sharpness", 0, 40, 7, 0, {}, ""}}}},
        {"palette_knife", "Palette Knife", "Artistic", {{{"size", "Stroke Size", 1, 50, 25, 0, {}, ""}, {"detail", "Stroke Detail", 1, 3, 3, 0, {}, ""}, {"softness", "Softness", 0, 10, 0, 0, {}, ""}}}},
        {"rough_pastels", "Rough Pastels", "Artistic", {{{"length", "Stroke Length", 0, 40, 6, 0, {}, ""}, {"detail", "Stroke Detail", 1, 20, 4, 0, {}, ""}, {"texture", "Texture", 0, 3, 0, 1, {"Canvas", "Sandstone", "Burlap", "Brick"}, ""}, {"scaling", "Scaling", 50, 200, 100, 0, {}, " %"}, {"relief", "Relief", 0, 50, 20, 0, {}, ""}}}},
        {"smudge_stick", "Smudge Stick", "Artistic", {{{"length", "Stroke Length", 0, 10, 2, 0, {}, ""}, {"highlight", "Highlight Area", 0, 20, 0, 0, {}, ""}, {"intensity", "Threshold", 0, 10, 10, 0, {}, ""}}}},
        {"sponge", "Sponge", "Artistic", {{{"size", "Brush Size", 0, 10, 5, 0, {}, ""}, {"definition", "Definition", 0, 25, 12, 0, {}, ""}, {"smoothness", "Smoothness", 1, 15, 5, 0, {}, ""}}}},
        {"underpainting", "Underpainting", "Artistic", {{{"size", "Brush Size", 0, 40, 6, 0, {}, ""}, {"coverage", "Coverage", 0, 40, 16, 0, {}, ""}, {"texture", "Texture", 0, 3, 0, 1, {"Canvas", "Sandstone", "Burlap", "Brick"}, ""}, {"scaling", "Scaling", 50, 200, 100, 0, {}, " %"}, {"relief", "Relief", 0, 50, 20, 0, {}, ""}}}},
    };
}

}  // namespace pittore::filter
