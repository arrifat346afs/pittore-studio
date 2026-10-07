#pragma once
// Vector Flood Fill + Shape Builder (ui/persona).
//
// Flood Fill renders the composite, flood-fills the clicked region and
// traces it into a real vector outline (fill to visible boundaries).
// Shape Builder combines intersected art layers into one (Add) or removes
// them (Delete) in a single undo step. AppState entry points commit through
// the shared creation tail; pure helpers stay testable. QtCore + QtGui.
#include <QPointF>
#include <QPainterPath>
#include <QRectF>

#include <vector>

#include "engine/vector/boolean.h"
#include "engine/vector/path.h"

namespace pittore::vector {
struct ArtNode;
}

namespace pittore::ui {

class AppState;
struct LayerItem;

// QPainterPath (from the mask tracer) to engine segments. Closed loops
// whose ends meet get Close; curves map exactly to CubicTo.
std::vector<pittore::vector::Segment> painterPathToSegments(
    const QPainterPath& path);

// An art layer's segments mapped to document coordinates (node matrix,
// layer scale, offset) for cross-layer combining.
std::vector<pittore::vector::Segment> artSegmentsInDoc(
    const pittore::vector::ArtNode& node, const LayerItem& layer);

// Doc-space rings from an art layer (flattened; open subpaths auto-close
// under fill semantics). Empty when the layer has no area. Shared by the
// boolean fold and creation-time path ops.
std::vector<pittore::vector::BoolRing> artToRings(
    const pittore::vector::ArtNode& node, const LayerItem& layer);

}  // namespace pittore::ui
