#pragma once
// Marker size bundle.
#include <string>

namespace pittore::svg {

struct MarkerArgs {
    double refX = 0, refY = 0, mw = 3, mh = 3;
    bool strokeWidthUnits = true;
};

MarkerArgs parseMarkerArgs(const std::string& refX, const std::string& refY,
                           const std::string& mw, const std::string& mh,
                           const std::string& units);

}  // namespace pittore::svg
