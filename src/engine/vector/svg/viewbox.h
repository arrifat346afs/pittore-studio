#pragma once
// Viewport resolve. viewBox wins over width/height.
#include <string>

namespace pittore::svg {

struct Viewport {
    double w = 0, h = 0;
};

bool parseViewBox(const std::string& s, double& w, double& h);
Viewport resolveViewport(const std::string& vb, const std::string& wStr,
                         const std::string& hStr);

}  // namespace pittore::svg
