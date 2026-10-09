// Slots: 0 fill, 1 stroke, 2 markers.
#include "engine/vector/svg/paint_order.h"

namespace pittore::svg {

std::vector<int> paintOrderSlots(const std::string& s) {
    if (s.empty() || s == "normal") {
        return {0, 1, 2};
    }
    std::vector<int> out;
    size_t i = 0;
    while (i < s.size() && out.size() < 3) {
        while (i < s.size() && s[i] == ' ') {
            ++i;
        }
        if (s.compare(i, 4, "fill") == 0) {
            out.push_back(0);
            i += 4;
        } else if (s.compare(i, 6, "stroke") == 0) {
            out.push_back(1);
            i += 6;
        } else if (s.compare(i, 7, "markers") == 0) {
            out.push_back(2);
            i += 7;
        } else {
            ++i;
        }
    }
    if (out.empty()) {
        return {0, 1, 2};
    }
    return out;
}

}  // namespace pittore::svg
