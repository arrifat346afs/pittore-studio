#pragma once
// Filter umbrella — backward-compatible entry point.
// New code may include the focused header directly:
//   engine/filter/core/filter_types.h    FilterParam / FilterDef
//   engine/filter/core/filter_detail.h   shared pixel-math helpers
//   engine/filter/core/filter_params.h   pv..p14v accessors
//   engine/filter/registry/filter_registry.h  allFilterDefs/findFilter/defaultParams
//   engine/filter/<category>/apply_<category>.h  per-category apply functions
//   engine/filter/<category>/defs_<category>.h   per-category declarations
// Split from the 2739-line single-header engine/filter/filters.h.

#include <string>
#include <vector>

#include "engine/core/image.h"
#include "engine/filter/artistic/apply_artistic.h"
#include "engine/filter/blur/apply_blur.h"
#include "engine/filter/blur_gallery/apply_blur_gallery.h"
#include "engine/filter/distort/apply_distort.h"
#include "engine/filter/lens/apply_lens.h"
#include "engine/filter/neural/apply_neural.h"
#include "engine/filter/noise/apply_noise.h"
#include "engine/filter/other/apply_other.h"
#include "engine/filter/pixelate/apply_pixelate.h"
#include "engine/filter/registry/filter_registry.h"
#include "engine/filter/render/apply_render.h"
#include "engine/filter/sharpen/apply_sharpen.h"
#include "engine/filter/sketch/apply_sketch.h"
#include "engine/filter/stylize/apply_stylize.h"
#include "engine/filter/texture/apply_texture.h"
#include "engine/filter/threed/apply_threed.h"
#include "engine/filter/video/apply_video.h"

namespace pittore::filter {

inline void applyFilter(Image &img, const std::string &id, const std::vector<double> &par) {
    const std::uint32_t w = img.width();
    const std::uint32_t h = img.height();
    if (w == 0 || h == 0) return;
    Image scratch(w, h);
    if (applyBlur(img, scratch, id, par)) return;
    if (applyBlurGallery(img, scratch, id, par)) return;
    if (applySharpen(img, scratch, id, par)) return;
    if (applyNoise(img, scratch, id, par)) return;
    if (applyPixelate(img, scratch, id, par)) return;
    if (applyDistort(img, scratch, id, par)) return;
    if (applyLens(img, scratch, id, par)) return;
    if (applyStylize(img, scratch, id, par)) return;
    if (applyArtistic(img, scratch, id, par)) return;
    if (applySketch(img, scratch, id, par)) return;
    if (applyTexture(img, scratch, id, par)) return;
    if (applyRender(img, scratch, id, par)) return;
    if (applyThreeD(img, scratch, id, par)) return;
    if (applyVideo(img, scratch, id, par)) return;
    if (applyNeural(img, scratch, id, par)) return;
    if (applyOther(img, scratch, id, par)) return;
}

}  // namespace pittore::filter
