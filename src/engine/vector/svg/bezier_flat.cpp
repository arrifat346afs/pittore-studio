// Distance of controls from chord.
#include "engine/vector/svg/bezier_flat.h"

#include <cmath>

namespace pittore::svg {

double cubicFlatness(double x0, double y0, double x1, double y1, double x2,
                     double y2, double x3, double y3) {
    const double ux = 3 * x1 - 2 * x0 - x3;
    const double uy = 3 * y1 - 2 * y0 - y3;
    const double vx = 3 * x2 - 2 * x3 - x0;
    const double vy = 3 * y2 - 2 * y3 - y0;
    const double a = std::max(ux * ux + uy * uy, vx * vx + vy * vy);
    return std::sqrt(a) / 4.0;
}

}  // namespace pittore::svg
