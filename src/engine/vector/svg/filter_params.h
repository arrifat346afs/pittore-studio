#pragma once
// Filter primitive params. Defaults per spec.
#include <string>

#include "engine/vector/svg/color_parse.h"

namespace pittore::svg {

double parseStdDev(const std::string& s, double fb = 0.0);

struct Flood {
    float r = 0, g = 0, b = 0, a = 1;
};

Flood parseFlood(const std::string& color, const std::string& opacity);

struct Offset {
    double dx = 0, dy = 0;
};

Offset parseOffset(const std::string& dx, const std::string& dy);

}  // namespace pittore::svg
