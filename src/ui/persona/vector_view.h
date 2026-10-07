#pragma once
// View-space vector painting order (ui/persona).
//
// Decides which art layers the canvas may draw straight from geometry at view
// resolution instead of sampling their document-resolution bake: only layers
// whose view rendering is exactly equal to the compositor's output — opaque
// Normal paint, no mask/clip/effects, top-level, above every adjustment.
// Everything else stays on the raster path, so this can never change a pixel
// except replacing a resampled edge with the true curve.
#include <QVector>

namespace pittore::ui {

class DocumentItem;

// Panel-order indices of the art layers drawable in view space, top-first
// (paint order within the caller's bottom-to-top walk). Empty means the
// document paints exactly as before (single flattened blit).
QVector<int> vectorViewOrder(const DocumentItem& d);

}  // namespace pittore::ui
