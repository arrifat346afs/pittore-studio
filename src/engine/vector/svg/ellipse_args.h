#pragma once
// Ellipse args bundle.
#include <string>

namespace pittore::svg {

struct EllipseArgs {
    double cx = 0, cy = 0, rx = 0, ry = 0;
};

EllipseArgs parseEllipseArgs(const std::string& cx, const std::string& cy,
                             const std::string& rx, const std::string& ry,
                             double vw, double vh);

}  // namespace pittore::svg
