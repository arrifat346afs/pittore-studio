#pragma once
// Solid paint to rgba.
#include <string>

namespace pittore::svg {

bool solidRgba(const std::string& color, const std::string& opacity,
               float out[4]);

}  // namespace pittore::svg
