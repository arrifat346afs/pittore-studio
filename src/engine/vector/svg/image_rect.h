#pragma once
// Image box bundle.
#include <string>

namespace pittore::svg {

struct ImageRect {
    double x = 0, y = 0, w = 0, h = 0;
};

ImageRect parseImageRect(const std::string& x, const std::string& y,
                         const std::string& w, const std::string& h, double vw,
                         double vh);

}  // namespace pittore::svg
