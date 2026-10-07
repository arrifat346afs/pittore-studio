#pragma once
// Undo-safe vector paint edits (ui/persona).
//
// The AppState::applyVectorPaint declaration lives in ui/app_state.h; every
// line of implementation here, so core files stay untouched. vectorEditableLayer
// is the shared "which layer do the vector panels edit?" rule. bakeArtDense
// builds the zoom-coupled display bake (see below).
#include <QString>

namespace pittore::ui {

class AppState;
struct LayerItem;

int vectorEditableLayer(AppState* state);

// Dense display bake for an art layer at the given view zoom: re-renders the
// node into the styled fields (styled/styledOffset/styledResample/...) so the
// compositor samples vector art supersampled instead of resampling the
// document-resolution pixels. Skips silently unless all of these hold: live
// art, no layer style, no active live filter, zoom bucket above 1. At bucket
// 1 any stale dense bake is dropped (the pixels path is already exact).
// Never composites or signals; callers own that.
void bakeArtDense(LayerItem& l, double zoom);

// Same contract for flattened SVG rows: re-rasterizes the retained member
// geometry (LayerItem::flatArt) into the styled fields at the zoom density,
// so zoomed map rows stay vector-crisp instead of upscaling their
// document-resolution shared raster. Only runs while the pixels are pristine
// (sourceStamp == flatStamp): once paint touches the row the geometry bake
// would revert brushwork, so it stands down and the pixels serve.
void rebakeFlatRow(LayerItem& l, double zoom);

}  // namespace pittore::ui
