#pragma once
// Circle args bundle.
#include <string>

namespace pittore::svg {

struct CircleArgs {
    double cx = 0, cy = 0, r = 0;
};

CircleArgs parseCircleArgs(const std::string& cx, const std::string& cy,
                           const std::string& r, double vw, double vh);

}  // namespace pittore::svg
