// Angle units to degrees.
#include "engine/vector/svg/angle.h"

#include <cstdlib>

namespace pittore::svg {

double parseAngleDeg(const std::string& s) {
    char* end = nullptr;
    const double v = std::strtod(s.c_str(), &end);
    if (end == s.c_str()) {
        return 0;
    }
    std::string u(end);
    while (!u.empty() && u.front() == ' ') {
        u.erase(u.begin());
    }
    if (u.compare(0, 3, "deg") == 0 || u.empty()) {
        return v;
    }
    if (u.compare(0, 4, "grad") == 0) {
        return v * 360.0 / 400.0;
    }
    if (u.compare(0, 3, "rad") == 0) {
        return v * 180.0 / 3.141592653589793;
    }
    if (u.compare(0, 4, "turn") == 0) {
        return v * 360.0;
    }
    return v;
}

}  // namespace pittore::svg
