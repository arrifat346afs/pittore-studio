// Words plus 100..900.
#include "engine/vector/svg/font_weight.h"

#include <cstdlib>

namespace pittore::svg {

int fontWeightNum(const std::string& s) {
    if (s == "bold") {
        return 700;
    }
    if (s == "normal") {
        return 400;
    }
    char* end = nullptr;
    const long v = std::strtol(s.c_str(), &end, 10);
    if (end == s.c_str() || v < 100 || v > 900) {
        return 400;
    }
    return (int)v;
}

}  // namespace pittore::svg
