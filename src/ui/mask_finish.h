#pragma once
// Mask finishing (density + feather): pure helpers shared by the compositor
// upload path (DocumentItem::maskSourceFor), PSD export baking, Apply Mask
// and the unit tests.
//
// The live model keeps the RAW painted mask on LayerItem; density (0..1
// coverage multiplier, the Density slider) and feather (gaussian
// radius in mask-native px, the Feather slider) are finished on demand so
// the backends keep sampling plain R coverage and GPU≡CPU holds by
// construction.
#include <memory>

#include "engine/core/image.h"

namespace pittore::ui {

// Copy of `src` with feather blurred in and density multiplied (r=g=b;
// a stays 1). Identity parameters return an exact copy.
std::shared_ptr<pittore::Image> finishMaskImage(const pittore::Image& src,
                                                float density,
                                                float featherPx);

// Re-finish only [x0,x1) x [y0,y1) (mask-native px) of an existing finished
// image from the current raw mask: blur the halo-extended patch, scale it,
// write back the dirty rect. Bit-identical to a full finishMaskImage within
// the rect (3 box passes spread exactly 3r, covered by the halo). Returns
// false when a full finish is required instead (size mismatch); the rect may
// be empty (true, nothing to do).
bool patchFinishedMaskRegion(const pittore::Image& raw, float density,
                             float featherPx, int x0, int y0, int x1, int y1,
                             pittore::Image& finished);

// Bilinear sample of the R channel, clamped to the edge.
float sampleCoverageBilinear(const pittore::Image& m, double x, double y);

}  // namespace pittore::ui
