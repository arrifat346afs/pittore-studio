// Align grid plus meet/slice.
#include "engine/vector/svg/aspect_align.h"

namespace pittore::svg {

Aspect parseAspect(const std::string& s) {
    Aspect o;
    if (s.empty() || s == "none") {
        o.align = 0;
        return o;
    }
    if (s.find("slice") != std::string::npos) {
        o.slice = true;
    }
    if (s.find("xMinYMin") != std::string::npos) {
        o.align = 0 + 1;
    } else if (s.find("xMidYMin") != std::string::npos) {
        o.align = 1 + 1;
    } else if (s.find("xMaxYMin") != std::string::npos) {
        o.align = 2 + 1;
    } else if (s.find("xMinYMid") != std::string::npos) {
        o.align = 3 + 1;
    } else if (s.find("xMaxYMid") != std::string::npos) {
        o.align = 5 + 1;
    } else if (s.find("xMinYMax") != std::string::npos) {
        o.align = 6 + 1;
    } else if (s.find("xMidYMax") != std::string::npos) {
        o.align = 7 + 1;
    } else if (s.find("xMaxYMax") != std::string::npos) {
        o.align = 8 + 1;
    } else {
        o.align = 4 + 1;
    }
    return o;
}

}  // namespace pittore::svg
