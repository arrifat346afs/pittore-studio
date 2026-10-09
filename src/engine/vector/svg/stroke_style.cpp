// Stroke words to enums.
#include "engine/vector/svg/stroke_style.h"

#include <cstdlib>

#include "engine/vector/svg/dash.h"

namespace pittore::svg {

Cap parseCap(const std::string& s) {
    if (s == "round") {
        return Cap::Round;
    }
    if (s == "square") {
        return Cap::Square;
    }
    return Cap::Butt;
}

Join parseJoin(const std::string& s) {
    if (s == "round") {
        return Join::Round;
    }
    if (s == "bevel") {
        return Join::Bevel;
    }
    return Join::Miter;
}

StrokeStyle makeStroke(double width, const std::string& cap,
                       const std::string& join, const std::string& miter,
                       const std::string& dash, const std::string& offset) {
    StrokeStyle s;
    s.width = width < 0 ? 0 : width;
    s.cap = parseCap(cap);
    s.join = parseJoin(join);
    s.miter = miter.empty() ? 4.0 : std::strtod(miter.c_str(), nullptr);
    s.dash = parseDashArray(dash);
    s.dashOffset = parseDashOffset(offset);
    return s;
}

}  // namespace pittore::svg
