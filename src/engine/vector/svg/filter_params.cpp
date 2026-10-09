// Param defaults. Never throws.
#include "engine/vector/svg/filter_params.h"

#include <cstdlib>

namespace pittore::svg {

double parseStdDev(const std::string& s, double fb) {
    if (s.empty()) {
        return fb;
    }
    char* end = nullptr;
    const double v = std::strtod(s.c_str(), &end);
    return end == s.c_str() ? fb : v;
}

Flood parseFlood(const std::string& color, const std::string& opacity) {
    Flood f;
    float c[4] = {0, 0, 0, 1};
    if (!color.empty() && parseColor(color, c)) {
        f.r = c[0];
        f.g = c[1];
        f.b = c[2];
        f.a = c[3];
    }
    if (!opacity.empty()) {
        char* end = nullptr;
        const double v = std::strtod(opacity.c_str(), &end);
        if (end != opacity.c_str()) {
            f.a *= (float)v;
        }
    }
    return f;
}

Offset parseOffset(const std::string& dx, const std::string& dy) {
    Offset o;
    o.dx = parseStdDev(dx, 0);
    o.dy = parseStdDev(dy, 0);
    return o;
}

}  // namespace pittore::svg
