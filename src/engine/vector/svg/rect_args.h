#pragma once
// Rect args bundle.
#include <string>

namespace pittore::svg {

struct RectArgs {
    double x = 0, y = 0, w = 0, h = 0, rx = 0, ry = 0;
};

RectArgs parseRectArgs(const std::string& x, const std::string& y,
                       const std::string& w, const std::string& h,
                       const std::string& rx, const std::string& ry, double vw,
                       double vh);

}  // namespace pittore::svg
