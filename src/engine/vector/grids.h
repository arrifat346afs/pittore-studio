#pragma once
// Grids: rectangular, axonometric/isometric, modular.
//
// A grid is data (origin/spacing/angle) plus a point quantizer; the canvas
// overlay and the snap engine share these so what you see is what snaps.
#include <string>
#include <vector>
#include <array>

namespace pittore::vector {

enum class GridKind { Rectangular, Axonometric, Modular };

struct GridSpec {
    GridKind kind = GridKind::Rectangular;
    double ox = 0.0, oy = 0.0;  // origin
    double dx = 10.0, dy = 10.0;  // spacing (rectangular/modular cell)
    double angleDeg = 30.0;        // axonometric angle (or ratio-derived)
    // Modular extras: major every N cells with its own gap.
    int majorEvery = 5;
    double majorGapX = 0.0, majorGapY = 0.0;
    bool visible = true;
    bool snap = true;
};

// Snap (x,y) to the grid; returns the snapped point.
std::pair<double, double> snapToGrid(const GridSpec& grid, double x, double y);

// Lattice lines intersecting rect (x0,y0,x1,y1) for overlay drawing.
// Each entry is a segment; axonometric emits both axes.
std::vector<std::array<double, 4>> gridLines(const GridSpec& grid, double x0, double y0,
                                             double x1, double y1);

// Isometric angle from ratio ("set grid angle by ratio"): run:rise.
GridSpec axonometricFromRatio(double run, double rise, double spacing);

// Parse/emit the SVG <inkscape:grid> attributes the editor round-trips.
GridSpec gridFromAttrs(const std::string& type, double spacingX, double spacingY,
                       double angle);
std::string gridTypeName(const GridSpec& grid);

}  // namespace pittore::vector
