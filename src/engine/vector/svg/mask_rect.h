#pragma once
// Mask area fractions.
#include <string>

namespace pittore::svg {

struct MaskRect {
    double x = -0.1, y = -0.1, w = 1.2, h = 1.2;
};

MaskRect parseMaskRect(const std::string& x, const std::string& y,
                       const std::string& w, const std::string& h);

}  // namespace pittore::svg
