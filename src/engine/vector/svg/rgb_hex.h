#pragma once
// Linear rgb to hex. Clamped.
#include <string>

namespace pittore::svg {

std::string rgbHex(float r, float g, float b);

}  // namespace pittore::svg
