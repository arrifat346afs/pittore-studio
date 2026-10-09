// Length bundle.
#include "engine/vector/svg/image_rect.h"

#include "engine/vector/svg/length.h"

namespace pittore::svg {

ImageRect parseImageRect(const std::string& x, const std::string& y,
                         const std::string& w, const std::string& h, double vw,
                         double vh) {
    ImageRect o;
    o.x = readLength(x, vw, 0);
    o.y = readLength(y, vh, 0);
    o.w = readLength(w, vw, 0);
    o.h = readLength(h, vh, 0);
    return o;
}

}  // namespace pittore::svg
