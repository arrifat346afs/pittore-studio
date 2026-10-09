// Same defaults as mask.
#include "engine/vector/svg/filter_rect.h"

#include "engine/vector/svg/grad_attrs.h"

namespace pittore::svg {

FilterRect parseFilterRect(const std::string& x, const std::string& y,
                           const std::string& w, const std::string& h) {
    FilterRect o;
    o.x = x.empty() ? -0.1 : gradNum(x, -0.1);
    o.y = y.empty() ? -0.1 : gradNum(y, -0.1);
    o.w = w.empty() ? 1.2 : gradNum(w, 1.2);
    o.h = h.empty() ? 1.2 : gradNum(h, 1.2);
    return o;
}

}  // namespace pittore::svg
