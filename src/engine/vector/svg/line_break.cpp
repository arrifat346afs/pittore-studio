// Break when over max.
#include "engine/vector/svg/line_break.h"

namespace pittore::svg {

std::vector<int> wrapLines(const std::vector<double>& widths, double maxW) {
    std::vector<int> br;
    double cur = 0;
    for (size_t k = 0; k < widths.size(); ++k) {
        if (cur + widths[k] > maxW && cur > 0) {
            br.push_back((int)k);
            cur = 0;
        }
        cur += widths[k];
    }
    return br;
}

}  // namespace pittore::svg
