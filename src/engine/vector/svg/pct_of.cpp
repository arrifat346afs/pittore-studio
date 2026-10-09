// Trailing % only.
#include "engine/vector/svg/pct_of.h"

#include <cstdlib>

namespace pittore::svg {

double pctFraction(const std::string& s, double fb) {
    if (s.empty() || s.back() != '%') {
        return fb;
    }
    char* end = nullptr;
    const double v = std::strtod(s.c_str(), &end);
    return end == s.c_str() ? fb : v / 100.0;
}

}  // namespace pittore::svg
