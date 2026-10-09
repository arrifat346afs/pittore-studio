// Region as bbox plus fractions.
#include "engine/vector/svg/filter_region.h"

namespace pittore::svg {

FBox resolveFilterRegion(double ox, double oy, double ow, double oh, double fx,
                         double fy, double fw, double fh) {
    FBox b;
    b.x = ox + fx * ow;
    b.y = oy + fy * oh;
    b.w = fw * ow;
    b.h = fh * oh;
    return b;
}

}  // namespace pittore::svg
