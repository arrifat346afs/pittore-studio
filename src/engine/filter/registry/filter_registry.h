#pragma once
// Filter registry: aggregates the per-category declaration tables.
// Split from engine/filter/filters.h.
#include <string>
#include <vector>

#include "engine/filter/core/filter_params.h"
#include "engine/filter/core/filter_types.h"
#include "engine/filter/blur/defs_blur.h"
#include "engine/filter/blur_gallery/defs_blur_gallery.h"
#include "engine/filter/sharpen/defs_sharpen.h"
#include "engine/filter/other/defs_other.h"
#include "engine/filter/noise/defs_noise.h"
#include "engine/filter/artistic/defs_artistic.h"
#include "engine/filter/pixelate/defs_pixelate.h"
#include "engine/filter/sketch/defs_sketch.h"
#include "engine/filter/distort/defs_distort.h"
#include "engine/filter/lens/defs_lens.h"
#include "engine/filter/stylize/defs_stylize.h"
#include "engine/filter/brush_strokes/defs_brush_strokes.h"
#include "engine/filter/texture/defs_texture.h"
#include "engine/filter/render/defs_render.h"
#include "engine/filter/threed/defs_threed.h"
#include "engine/filter/video/defs_video.h"
#include "engine/filter/neural/defs_neural.h"

namespace pittore::filter {

inline std::vector<FilterDef> allFilterDefs() {
    std::vector<FilterDef> out;
        auto blur = BlurFilterDefs();
        auto blur_gallery = BlurGalleryFilterDefs();
        auto sharpen = SharpenFilterDefs();
        auto other = OtherFilterDefs();
        auto noise = NoiseFilterDefs();
        auto artistic = ArtisticFilterDefs();
        auto pixelate = PixelateFilterDefs();
        auto sketch = SketchFilterDefs();
        auto distort = DistortFilterDefs();
        auto lens = LensFilterDefs();
        auto stylize = StylizeFilterDefs();
        auto brush_strokes = BrushStrokesFilterDefs();
        auto texture = TextureFilterDefs();
        auto render = RenderFilterDefs();
        auto threed = ThreeDFilterDefs();
        auto video = VideoFilterDefs();
        auto neural = NeuralFilterDefs();
        out.insert(out.end(), blur.begin(), blur.end());
        out.insert(out.end(), blur_gallery.begin(), blur_gallery.end());
        out.insert(out.end(), sharpen.begin(), sharpen.end());
        out.insert(out.end(), other.begin(), other.end());
        out.insert(out.end(), noise.begin(), noise.end());
        out.insert(out.end(), artistic.begin(), artistic.end());
        out.insert(out.end(), pixelate.begin(), pixelate.end());
        out.insert(out.end(), sketch.begin(), sketch.end());
        out.insert(out.end(), distort.begin(), distort.end());
        out.insert(out.end(), lens.begin(), lens.end());
        out.insert(out.end(), stylize.begin(), stylize.end());
        out.insert(out.end(), brush_strokes.begin(), brush_strokes.end());
        out.insert(out.end(), texture.begin(), texture.end());
        out.insert(out.end(), render.begin(), render.end());
        out.insert(out.end(), threed.begin(), threed.end());
        out.insert(out.end(), video.begin(), video.end());
        out.insert(out.end(), neural.begin(), neural.end());
    return out;
}

inline const FilterDef *findFilter(const std::string &id) {
    static const std::vector<FilterDef> defs = allFilterDefs();
    for (const auto &d : defs) {
        if (id == d.id) return &d;
    }
    return nullptr;
}

inline std::vector<double> defaultParams(const std::string &id) {
    std::vector<double> out;
    if (const FilterDef *d = findFilter(id)) {
        for (const auto &p : d->params) out.push_back(p.def);
    }
    return out;
}

}  // namespace pittore::filter
