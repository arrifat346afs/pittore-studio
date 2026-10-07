#pragma once
// In-progress Bezier path for the Pen / Freehand / Curvature tools.
//
// The canvas owns one PenPath in document coordinates while the gesture runs;
// on finish it flattens to engine Segments (MoveTo/LineTo/CubicTo/Close) and
// commits through AppState::addVectorPathLayer. Nothing here rasterizes, so
// the model stays unit-testable without the widget stack.

#include <QPointF>
#include <QRectF>

#include <cstddef>
#include <vector>

#include "engine/vector/path.h"

namespace pittore::ui {

// Which point-adding behaviour flattens the anchors.
enum class PenMode { Bezier, Freehand, Curvature };

// One anchor with optional Bezier handles, in document coordinates.
struct PenPoint {
    QPointF anchor;
    QPointF inHandle;   // valid when hasIn
    QPointF outHandle;  // valid when hasOut
    bool hasIn = false;
    bool hasOut = false;
};

class PenPath {
public:
    void clear();
    bool isEmpty() const { return points_.empty(); }
    std::size_t size() const { return points_.size(); }
    bool closed() const { return closed_; }
    void setClosed(bool closed) { closed_ = closed; }

    const std::vector<PenPoint>& points() const { return points_; }

    // Plain click (Pen): corner anchor with no handles.
    void addCorner(const QPointF& at);
    // Press-drag (Pen): smooth anchor; the drag vector gives symmetric
    // handles (out = anchor + v, in = anchor - v).
    void addSmooth(const QPointF& at, const QPointF& dragVec);
    // Reshape the last point's handles around its anchor (live drag).
    void setLastHandles(const QPointF& anchor, const QPointF& dragVec);
    // A near-zero drag is a click: strip the last point's handles.
    void demoteLastToCorner(double minHandleDoc);
    // Pencil stream (Freehand): appends only past minSpacing doc px.
    void addFreehand(const QPointF& at, double minSpacingDoc);
    // Collapse runs of (near-)collinear stream points.
    void simplifyFreehand(double tolDoc);

    // First-anchor grab used for click-to-close.
    bool closeHit(const QPointF& docPos, double tolDoc) const;

    // Anchors (and handle tips) bounding box; empty when no points.
    QRectF bounds() const;

    // Flatten to engine segments. Bezier honours per-point handles,
    // Freehand emits the polyline, Curvature fits Catmull-Rom through the
    // anchors. A closed path ends with Close.
    std::vector<pittore::vector::Segment> toSegments(PenMode mode) const;

private:
    std::vector<PenPoint> points_;
    bool closed_ = false;
};

// One dab of a vector brush stroke: centerline position plus full width.
struct BrushDab {
    QPointF pos;
    double width = 8.0;
};

// A brush stroke: streamed centerline with per-dab widths, committed as a
// filled ribbon outline (round caps/joins) through
// AppState::addVectorBrushLayer. Widths come from the bar's Width and
// Controller (Velocity thins fast spans); Pressure falls back to constant
// without tablet input.
class BrushStroke {
public:
    void clear();
    bool isEmpty() const { return dabs_.empty(); }
    std::size_t size() const { return dabs_.size(); }
    void addDab(const QPointF& pos, double width);
    const std::vector<BrushDab>& dabs() const { return dabs_; }

    // Closed ribbon outline (round caps/joins) in the same coordinates.
    // A single dab becomes a disc. The spine is Chaikin-smoothed and the
    // edges are cubic Beziers, so fast curves render smooth instead of
    // faceted; ratio/angle/square select a calligraphic nib whose support
    // sets each edge offset (round at 1.0/round, matching the dab kernel
    // convention). A smoothed spine also rounds sharp reversals into
    // loops, which the winding fill covers instead of slitting.
    std::vector<pittore::vector::Segment> buildRibbon(
        double ratio = 1.0, double angleDeg = 0.0,
        bool square = false) const;

private:
    std::vector<BrushDab> dabs_;
};

// Tablet pressure → dab width: light touches draw hairlines, full pressure
// reaches the bar width. Pure (unit-testable); the canvas feeds it live
// QTabletEvent pressure, falling back to constant/velocity without a tablet.
double brushPressureWidth(double baseWidth, double pressure);

}  // namespace pittore::ui
