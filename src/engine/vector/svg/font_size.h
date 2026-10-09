#pragma once
// Font size to px. Supports remap via parent size.
#include <string>

namespace pittore::svg {

double fontSizePx(const std::string& s, double parentPx, double fb = 16.0);

}  // namespace pittore::svg
