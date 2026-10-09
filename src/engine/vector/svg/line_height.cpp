// Normal is 1.2x.
#include "engine/vector/svg/line_height.h"

#include <cstdlib>

#include "engine/vector/svg/length.h"

namespace pittore::svg {

double lineHeightPx(const std::string& s, double fontPx) {
    if (s.empty() || s == "normal") {
        return fontPx * 1.2;
    }
    char* end = nullptr;
    const double v = std::strtod(s.c_str(), &end);
    if (end != s.c_str() && (end == s.c_str() + s.size() || *end == 0)) {
        return v * fontPx;
    }
    const Length l = parseLength(s);
    return l.valid ? toPx(l, fontPx, fontPx, fontPx / 2.0) : fontPx * 1.2;
}

}  // namespace pittore::svg
