#pragma once
// Live (Smart) filter layers: a non-destructive filter recipe over a pixel
// layer's native pixels, rendered on demand into a cached image and
// composited like the styled raster (see ensureLayerStyle in app_state.cpp).
//
// The AppState ops are only *declared* in app_state.h and defined in
// live_filter.cpp.
#include <memory>

#include "engine/core/image.h"

namespace pittore::ui {

class LayerItem;

// Filtered base for compositing/styling: the cached render when a live
// filter applies, else the native pixels. Null when the layer has no
// pixels. Never mutates; call ensureLayerFilter() first.
const pittore::Image* liveFilterBase(const LayerItem& l);

// (Re)render the cached filtered image when the recipe, params, enabled
// state or native pixels changed; drop it otherwise. Bumps styledRev (the
// effective-source revision the device cache keys on) on every change so
// the compositor re-uploads, and invalidates the styled raster (it renders
// over this base).
void ensureLayerFilter(const LayerItem& l);

}  // namespace pittore::ui
