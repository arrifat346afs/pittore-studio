#pragma once
// Destructive point ops for retained art (ui/persona): Corner rounding,
// Contour offset, Knife cuts and Point-Transform scaling/rotation.
//
// All functions take plain ArtNodes in node coordinates and return new
// segment lists (or edit in place); the canvas commits results with
// applyVectorNode. Pure QtCore.
#include <QPointF>

#include <vector>

#include "engine/vector/path.h"

namespace pittore::vector {
struct ArtNode;
}

namespace pittore::ui {

// Round the corner at anchor segment `seg` to `radius` (node units): both
// adjacent spans are trimmed back and joined by a tangent cubic through the
// old anchor. Only line/cubic spans; Close-segment corners are skipped.
// False when there is nothing to round (missing neighbour or radius <= 0).
bool roundNodeCorner(pittore::vector::ArtNode& node, int seg, double radius);

// Offset closed outlines by `radius` (outward when positive): flatten,
// expand through the stroke engine, keep the extreme loop per subpath.
// `join` follows ArtPaint codes (0 miter, 1 round, 2 bevel). Open paths
// refuse (false, nothing mutated).
bool contourNodePath(pittore::vector::ArtNode& node, double radius, int join);

// True when the anchor at `seg` carries live curvature (an adjacent cubic
// span with non-degenerate handles). Degenerate cubics (controls coincident
// with the span endpoints, as built for straight edges) count as corners.
bool isSmoothAnchor(const pittore::vector::ArtNode& node, int seg);

// Insert an anchor on the nearest span within `tol` of `nodePos` (node
// units): lines split exactly, cubics split at the nearest flattened
// fraction. The closing edge (trailing Close) is not splittable. Writes
// the new anchor's segment index to `segOut`. False when nothing is near.
bool insertAnchorPoint(pittore::vector::ArtNode& node, const QPointF& nodePos,
                       double tol, int* segOut);

// Delete the anchor at `seg`, bridging its neighbours with a straight
// span. Refuses the MoveTo start, the duplicate closing anchor, and paths
// that would drop below 2 anchors (open) or 3 distinct corners (closed).
bool deleteAnchorPoint(pittore::vector::ArtNode& node, int seg);

// Cut every span the knife segment crosses: cut spans split (cubics by de
// Casteljau, lines by lerp) and subpaths break there. `closeMode`: 0 never,
// 1 Near (ends within 8), 2 Far (within 32), 3 Always. Returns the cut
// count; 0 leaves the node untouched.
int knifeCutNode(pittore::vector::ArtNode& node, const QPointF& a,
                 const QPointF& b, double tolerance, int closeMode);

// Scale (`factor`) and rotate (`degrees`) every point and control about
// `centre`, in place.
void transformNodePoints(pittore::vector::ArtNode& node, const QPointF& centre,
                         double factor, double degrees);

// Centroid of the anchor endpoints; (0,0) when there are none.
QPointF nodeAnchorCentroid(const pittore::vector::ArtNode& node);

}  // namespace pittore::ui
