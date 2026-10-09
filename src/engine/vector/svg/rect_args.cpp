// Bundle parse. ry falls back to rx.
#include "engine/vector/svg/rect_args.h"

#include "engine/vector/svg/length.h"

namespace pittore::svg {

RectArgs parseRectArgs(const std::string& x, const std::string& y,
                       const std::string& w, const std::string& h,
                       const std::string& rx, const std::string& ry, double vw,
                       double vh) {
    RectArgs o;
    o.x = readLength(x, vw, 0);
    o.y = readLength(y, vh, 0);
    o.w = readLength(w, vw, 0);
    o.h = readLength(h, vh, 0);
    o.rx = readLength(rx, vw, 0);
    o.ry = ry.empty() ? o.rx : readLength(ry, vh, 0);
    return o;
}

}  // namespace pittore::svg
