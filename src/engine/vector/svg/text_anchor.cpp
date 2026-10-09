// Start is 0.
#include "engine/vector/svg/text_anchor.h"

namespace pittore::svg {

double anchorShift(const std::string& anchor, double w) {
    if (anchor == "middle") {
        return -w / 2;
    }
    if (anchor == "end") {
        return -w;
    }
    return 0;
}

}  // namespace pittore::svg
