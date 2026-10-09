// Degree elevation.
#include "engine/vector/svg/quad_split.h"

namespace pittore::svg {

void quadToCubic(double x0, double y0, double qx, double qy, double x1,
                 double y1, double& c1x, double& c1y, double& c2x,
                 double& c2y) {
    c1x = x0 / 3 + qx * 2 / 3;
    c1y = y0 / 3 + qy * 2 / 3;
    c2x = x1 / 3 + qx * 2 / 3;
    c2y = y1 / 3 + qy * 2 / 3;
}

}  // namespace pittore::svg
