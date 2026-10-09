// Clamp 0..1.
#include "engine/vector/svg/stop_opacity.h"

#include <cstdlib>

namespace pittore::svg {

float stopAlpha(const std::string& s, float fb) {
    if (s.empty()) {
        return fb;
    }
    char* end = nullptr;
    const double v = std::strtod(s.c_str(), &end);
    if (end == s.c_str()) {
        return fb;
    }
    if (v < 0) {
        return 0;
    }
    if (v > 1) {
        return 1;
    }
    return (float)v;
}

}  // namespace pittore::svg
