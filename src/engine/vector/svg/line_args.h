#pragma once
// Line args bundle.
#include <string>

namespace pittore::svg {

struct LineArgs {
    double x1 = 0, y1 = 0, x2 = 0, y2 = 0;
};

LineArgs parseLineArgs(const std::string& x1, const std::string& y1,
                       const std::string& x2, const std::string& y2, double vw,
                       double vh);

}  // namespace pittore::svg
