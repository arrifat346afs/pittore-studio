// Percent aware numbers.
#include "engine/vector/svg/grad_attrs.h"

#include <cstdlib>

namespace pittore::svg {

double gradNum(const std::string& s, double fb) {
    if (s.empty()) {
        return fb;
    }
    char* end = nullptr;
    const double v = std::strtod(s.c_str(), &end);
    if (end == s.c_str()) {
        return fb;
    }
    if (s.find('%') != std::string::npos) {
        return v / 100.0;
    }
    return v;
}

}  // namespace pittore::svg
