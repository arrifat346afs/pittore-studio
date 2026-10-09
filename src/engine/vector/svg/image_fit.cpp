// Meet centers, slice covers, none stretches.
#include "engine/vector/svg/image_fit.h"

#include <algorithm>

namespace pittore::svg {

FitRect fitView(double vw, double vh, double dx, double dy, double dw,
                double dh, bool slice, bool none) {
    FitRect o{dx, dy, dw, dh};
    if (none || vw <= 0 || vh <= 0 || dw <= 0 || dh <= 0) {
        return o;
    }
    const double s = slice ? std::max(dw / vw, dh / vh) : std::min(dw / vw, dh / vh);
    o.w = vw * s;
    o.h = vh * s;
    o.x = dx + (dw - o.w) / 2;
    o.y = dy + (dh - o.h) / 2;
    return o;
}

}  // namespace pittore::svg
