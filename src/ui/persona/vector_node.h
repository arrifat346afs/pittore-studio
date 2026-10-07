#pragma once
// Direct node editing of retained geometry (ui/persona).
//
// ArtNodes store unflattened segments, so the Node tool can move curve points
// without any path-store model: hit-test an endpoint, translate it (cubic
// controls travel with their endpoint), re-raster on commit. Pure QtCore.
#include <QPointF>

#include <vector>

namespace pittore::vector {
struct ArtNode;
}

namespace pittore::ui {

// Every drawable endpoint in segment order (MoveTo/LineTo/CubicTo ends;
// Close carries no point). Shared by hit-testing and the overlay.
std::vector<QPointF> nodeEndpoints(const pittore::vector::ArtNode& node);

// Nearest segment endpoint to `nodePos` within `tol` (node units), or -1.
int nodeEndpointAt(const pittore::vector::ArtNode& node, const QPointF& nodePos,
                   double tol, int* segOut = nullptr);
// Move segment `seg`'s endpoint to `nodePos`, translating any cubic controls
// of that segment by the same delta so curvature travels with the point.
void moveNodePoint(pittore::vector::ArtNode& node, int seg,
                   const QPointF& nodePos);

// Bezier handles. An anchor (endpoint segment `seg`) owns an in-handle in
// its own segment's c2 (when CubicTo) and an out-handle in the NEXT
// segment's c1 (when CubicTo). `side` 0 = in, 1 = out.
enum class NodeHandleSide { In = 0, Out = 1 };

// Every handle tip in the node: (anchor segment, side, position).
struct NodeHandle {
    int anchorSeg = -1;
    NodeHandleSide side = NodeHandleSide::In;
    QPointF pos;
};
std::vector<NodeHandle> nodeHandles(const pittore::vector::ArtNode& node);

// Nearest handle tip within `tol`, or anchorSeg -1. Hit-tests handles only.
NodeHandle nodeHandleAt(const pittore::vector::ArtNode& node,
                        const QPointF& nodePos, double tol);

// Drag a handle tip to `nodePos`. `mirror` keeps the opposite handle
// mirrored through the anchor (smooth point); otherwise only the dragged
// handle moves (corner, or Alt-drag). The anchor itself never moves.
void moveNodeHandle(pittore::vector::ArtNode& node, int anchorSeg,
                    NodeHandleSide side, const QPointF& nodePos, bool mirror);

// True when the anchor's handles are (near-)mirrors, i.e. dragging one
// should move the other to preserve smoothness.
bool nodeHandlesMirrored(const pittore::vector::ArtNode& node, int anchorSeg,
                         double tol);

// Convert the anchor: corner retracts both adjacent spans to lines,
// smooth rebuilds mirrored handles along the neighbour direction.
void convertNodePoint(pittore::vector::ArtNode& node, int anchorSeg,
                      bool smooth);

// Append Close when the path is open and has 2+ anchors. False when
// already closed or degenerate.
bool closeNodePath(pittore::vector::ArtNode& node);

// Break the path at anchor `seg`: it becomes the end of one subpath and
// (duplicated) the start of the next. A closed loop opens at the anchor.
// False when `seg` is not an anchor, or already sits on a break.
bool splitNodePath(pittore::vector::ArtNode& node, int seg);

// Join the selected open end `seg` to the nearest other open end with a
// line, merging both spans into one subpath. False with no other end, or
// when `seg` is not an open end (mid-path anchors cannot join).
bool joinNodePath(pittore::vector::ArtNode& node, int seg);

// Reverse the direction of the subpath holding anchor `seg` (handles ride
// along via control swap). False when `seg` is not an anchor.
bool reverseNodeSubpath(pittore::vector::ArtNode& node, int seg);

}  // namespace pittore::ui
