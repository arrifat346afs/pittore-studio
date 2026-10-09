#pragma once
// Pattern rect bundle.
#include <string>

namespace pittore::svg {

struct PatternRect {
    double x = 0, y = 0, w = 0, h = 0;
};

PatternRect parsePatternRect(const std::string& x, const std::string& y,
                             const std::string& w, const std::string& h);

}  // namespace pittore::svg
