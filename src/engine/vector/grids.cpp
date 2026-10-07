// Grid quantizers.
#include "engine/vector/grids.h"

#include <cmath>

namespace pittore::vector {

std::pair<double, double> snapToGrid(const GridSpec& grid, double x, double y) {
    if (grid.kind == GridKind::Axonometric) {
        double a = grid.angleDeg * 3.14159265358979 / 180.0;
        // Project onto the two axes, round, project back.
        double ca = std::cos(a), sa = std::sin(a);
        double u = (x - grid.ox) * ca + (y - grid.oy) * sa;
        double v = -(x - grid.ox) * sa + (y - grid.oy) * ca;
        // v axis is compressed for the classic 2:1 isometric look.
        u = std::round(u / grid.dx) * grid.dx;
        v = std::round(v / grid.dy) * grid.dy;
        return {grid.ox + u * ca - v * sa, grid.oy + u * sa + v * ca};
    }
    double sx = grid.dx > 0 ? grid.dx : 1.0, sy = grid.dy > 0 ? grid.dy : 1.0;
    return {grid.ox + std::round((x - grid.ox) / sx) * sx,
            grid.oy + std::round((y - grid.oy) / sy) * sy};
}

std::vector<std::array<double, 4>> gridLines(const GridSpec& grid, double x0, double y0,
                                             double x1, double y1) {
    std::vector<std::array<double, 4>> out;
    if (grid.kind == GridKind::Axonometric) {
        double a = grid.angleDeg * 3.14159265358979 / 180.0;
        double step = grid.dx > 0 ? grid.dx : 10.0;
        for (double k = -4096; k < 4096; k += step) {
            double bx = grid.ox + k * std::cos(a), by = grid.oy + k * std::sin(a);
            out.push_back({bx - 4096 * std::cos(a + 1.5707), by - 4096 * std::sin(a + 1.5707),
                           bx + 4096 * std::cos(a + 1.5707), by + 4096 * std::sin(a + 1.5707)});
            if (out.size() > 400) break;
        }
        (void)x0;
        (void)y0;
        (void)x1;
        (void)y1;
        return out;
    }
    double sx = grid.dx > 0 ? grid.dx : 10.0, sy = grid.dy > 0 ? grid.dy : 10.0;
    for (double gx = grid.ox + std::ceil((x0 - grid.ox) / sx) * sx; gx <= x1; gx += sx)
        out.push_back({gx, y0, gx, y1});
    for (double gy = grid.oy + std::ceil((y0 - grid.oy) / sy) * sy; gy <= y1; gy += sy)
        out.push_back({x0, gy, x1, gy});
    return out;
}

GridSpec axonometricFromRatio(double run, double rise, double spacing) {
    GridSpec g;
    g.kind = GridKind::Axonometric;
    g.dx = spacing > 0 ? spacing : 10.0;
    g.dy = g.dx;
    g.angleDeg = std::atan2(rise, run) * 180.0 / 3.14159265358979;
    return g;
}

GridSpec gridFromAttrs(const std::string& type, double spacingX, double spacingY,
                       double angle) {
    GridSpec g;
    if (type == "axonometric")
        g.kind = GridKind::Axonometric;
    else if (type == "modular")
        g.kind = GridKind::Modular;
    g.dx = spacingX;
    g.dy = spacingY;
    g.angleDeg = angle;
    return g;
}

std::string gridTypeName(const GridSpec& grid) {
    if (grid.kind == GridKind::Axonometric) return "axonometric";
    if (grid.kind == GridKind::Modular) return "modular";
    return "rectangular";
}

}  // namespace pittore::vector
