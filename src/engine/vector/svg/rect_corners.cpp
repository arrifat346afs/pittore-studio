// Clamp to half box.
#include "engine/vector/svg/rect_corners.h"

#include <algorithm>

namespace pittore::svg {

void clampRadii(double w, double h, double& rx, double& ry) {
    if (rx < 0) {
        rx = 0;
    }
    if (ry < 0) {
        ry = 0;
    }
    rx = std::min(rx, w / 2);
    ry = std::min(ry, h / 2);
}

}  // namespace pittore::svg
