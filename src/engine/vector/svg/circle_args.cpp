// Min dimension scales r.
#include "engine/vector/svg/circle_args.h"

#include <algorithm>

#include "engine/vector/svg/length.h"

namespace pittore::svg {

CircleArgs parseCircleArgs(const std::string& cx, const std::string& cy,
                           const std::string& r, double vw, double vh) {
    CircleArgs o;
    o.cx = readLength(cx, vw, 0);
    o.cy = readLength(cy, vh, 0);
    o.r = readLength(r, std::min(vw, vh), 0);
    return o;
}

}  // namespace pittore::svg
