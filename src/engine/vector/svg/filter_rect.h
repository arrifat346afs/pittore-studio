#pragma once
// Filter area fractions. Defaults per spec.
#include <string>

namespace pittore::svg {

struct FilterRect {
    double x = -0.1, y = -0.1, w = 1.2, h = 1.2;
};

FilterRect parseFilterRect(const std::string& x, const std::string& y,
                           const std::string& w, const std::string& h);

}  // namespace pittore::svg
