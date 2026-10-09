// Direct map.
#include "engine/vector/svg/line_args.h"

#include "engine/vector/svg/length.h"

namespace pittore::svg {

LineArgs parseLineArgs(const std::string& x1, const std::string& y1,
                       const std::string& x2, const std::string& y2, double vw,
                       double vh) {
    LineArgs o;
    o.x1 = readLength(x1, vw, 0);
    o.y1 = readLength(y1, vh, 0);
    o.x2 = readLength(x2, vw, 0);
    o.y2 = readLength(y2, vh, 0);
    return o;
}

}  // namespace pittore::svg
