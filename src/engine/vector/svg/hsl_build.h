#pragma once
// HSL to sRGB. h in degrees.
namespace pittore::svg {

void hslToRgb(float h, float s, float l, float out[3]);

}  // namespace pittore::svg
