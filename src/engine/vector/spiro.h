#pragma once
// Spiro splines + B-splines: smooth-curve drawing modes.
//
// Full clothoid solving is iterative; the editor needs interactive rates, so
// Spiro runs as Catmull-Rom-to-Bezier with corner/smooth typing (visually
// identical for the pen tool's density), and B-spline runs as uniform cubic
// BSpline-to-Bezier. Both output Cubics the rest of the pipeline already
// edits/flattens.
#include <vector>

#include "engine/vector/path.h"

namespace pittore::vector {

enum class SpiroType { Corner, Smooth, Left, Right };

// One Spiro control point.
struct SpiroPoint {
    double x = 0.0, y = 0.0;
    SpiroType type = SpiroType::Smooth;
};

// Fit an open Spiro/B-spline through `pts`. Returns cubic Segments
// (MoveTo + CubicTo chain). Closed when `closed`.
std::vector<Segment> spiroFit(const std::vector<SpiroPoint>& pts, bool closed = false);
std::vector<Segment> bsplineFit(const std::vector<std::pair<double, double>>& pts,
                                bool closed = false);

// Convenience: smooth a raw freehand polyline into cubics (pencil mode).
std::vector<Segment> smoothFreehand(const std::vector<std::pair<float, float>>& pts,
                                    double smooth = 1.0);

}  // namespace pittore::vector
