#pragma once
// Numeric transforms, align/distribute, arrange.
//
// Numeric transform dialogs, align/distribute, arrange (grid/circle/polar) and
// rows/columns. Operates on bbox lists so canvas, export and scripting share
// one implementation.
#include <string>
#include <vector>

namespace pittore::vector {

struct Bbox {
    double x0 = 0, y0 = 0, x1 = 0, y1 = 0;
    double cx() const { return (x0 + x1) / 2; }
    double cy() const { return (y0 + y1) / 2; }
    double w() const { return x1 - x0; }
    double h() const { return y1 - y0; }
};

enum class AlignEdge {
    Left, HCenter, Right, Top, VCenter, Bottom,
};
enum class DistributeKind {
    HGap, VGap, HCenter, VCenter, HEdge, VEdge,
};

// Move every box so the chosen edge/center meets `to`. Returns translations.
std::vector<std::pair<double, double>> alignBoxes(std::vector<Bbox> boxes, AlignEdge edge,
                                                  double to);
// Even-gaps distribution along one axis. Returns translations.
std::vector<std::pair<double, double>> distributeBoxes(std::vector<Bbox> boxes,
                                                       DistributeKind kind,
                                                       double gap = 0.0);

// Numeric transform spec (Transform dialog): translate/scale/rotate/skew/matrix
// with "apply to each separately" support.
struct TransformSpec {
    double dx = 0, dy = 0;
    double sx = 1, sy = 1;
    double rotateDeg = 0;
    double skewXDeg = 0, skewYDeg = 0;
    double matrix[6] = {1, 0, 0, 1, 0, 0};  // used when useMatrix=true
    bool useMatrix = false;
    bool relative = true;  // scale/rotate about each box center vs origin
};
// Compose the spec into an SVG matrix (a b c d e f).
void transformSpecToMatrix(const TransformSpec& spec, double m[6]);

// Arrange: place `n` copies on grid/circle/polar/honeycomb lattices.
enum class ArrangeKind { Grid, Circle, Polar, Honeycomb, Spiral };
struct ArrangeSpec {
    ArrangeKind kind = ArrangeKind::Grid;
    int cols = 4, rows = 4;
    double dx = 20, dy = 20;
    double radius = 100;
    double startDeg = 0, endDeg = 360;
};
std::vector<std::pair<double, double>> arrangePositions(int n, const ArrangeSpec& spec);

}  // namespace pittore::vector
