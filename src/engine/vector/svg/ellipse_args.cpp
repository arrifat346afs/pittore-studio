// Direct map.
#include "engine/vector/svg/ellipse_args.h"

#include "engine/vector/svg/length.h"

namespace pittore::svg {

EllipseArgs parseEllipseArgs(const std::string& cx, const std::string& cy,
                             const std::string& rx, const std::string& ry,
                             double vw, double vh) {
    EllipseArgs o;
    o.cx = readLength(cx, vw, 0);
    o.cy = readLength(cy, vh, 0);
    o.rx = readLength(rx, vw, 0);
    o.ry = readLength(ry, vh, 0);
    return o;
}

}  // namespace pittore::svg
