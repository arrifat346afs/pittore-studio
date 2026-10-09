// Plain numbers.
#include "engine/vector/svg/pattern_attrs.h"

#include <cstdlib>

namespace pittore::svg {

PatternRect parsePatternRect(const std::string& x, const std::string& y,
                             const std::string& w, const std::string& h) {
    PatternRect o;
    o.x = x.empty() ? 0 : std::strtod(x.c_str(), nullptr);
    o.y = y.empty() ? 0 : std::strtod(y.c_str(), nullptr);
    o.w = w.empty() ? 0 : std::strtod(w.c_str(), nullptr);
    o.h = h.empty() ? 0 : std::strtod(h.c_str(), nullptr);
    return o;
}

}  // namespace pittore::svg
