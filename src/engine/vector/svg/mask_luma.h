#pragma once
// Mask luminance. One weight set everywhere.
#include <cstdint>

namespace pittore::svg {

float luminance(float r, float g, float b);
std::uint8_t maskAlpha(float r, float g, float b, float a);

}  // namespace pittore::svg
