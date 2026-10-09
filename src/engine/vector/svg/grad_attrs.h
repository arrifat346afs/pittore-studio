#pragma once
// Gradient raw attrs. Fractions kept raw here.
#include <string>

namespace pittore::svg {

struct GradAttrs {
    std::string x1, y1, x2, y2, cx, cy, r, fx, fy, fr;
};

double gradNum(const std::string& s, double fb);

}  // namespace pittore::svg
