// Four-number box.
#include "engine/vector/svg/box.h"

#include "engine/vector/svg/number_list.h"

namespace pittore::svg {

Box parseBox(const std::string& s) {
    Box b;
    const auto v = parseDoubles(s);
    if (v.size() < 4) {
        return b;
    }
    b.x = v[0];
    b.y = v[1];
    b.w = v[2];
    b.h = v[3];
    b.valid = b.w > 0 && b.h > 0;
    return b;
}

}  // namespace pittore::svg
