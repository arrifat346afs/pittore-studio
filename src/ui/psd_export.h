#pragma once
// Layered PSD export (Phase 4A): the inverse of AppState::openPsdLayers.
//
// buildLayeredPsdDoc converts the live panel model (index 0 = top) into file
// order (bottom -> top) for psdEncodeLayers. AppState::exportLayeredPsd wraps
// it with validation and encoding; the caller writes the bytes to disk.
#include <QString>

#include "engine/io/psd.h"

namespace pittore::ui {

class DocumentItem;

// Panel -> file order, with group opacity/visibility unfolded (the importer
// refolds them), raw native pixels (styles/text don't survive), masks as
// layer-local coverage grids, adjustments with null bounds (the standard
// 0-channel convention). Stand-in
// adjustments (kind 0) are skipped: the encoder drops them anyway, and
// emitting them as blank pixels would corrupt the canvas on reimport.
pittore::io::PsdLayersDoc buildLayeredPsdDoc(DocumentItem& doc);

}  // namespace pittore::ui
