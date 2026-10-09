// Mirror across point.
#include "engine/vector/svg/smooth_hint.h"

namespace pittore::svg {

void reflectPoint(double px, double py, double cx, double cy, double& ox,
                  double& oy) {
    ox = 2 * cx - px;
    oy = 2 * cy - py;
}

}  // namespace pittore::svg
