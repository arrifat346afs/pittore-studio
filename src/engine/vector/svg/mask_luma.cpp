// Rec.709 luma weights.
#include "engine/vector/svg/mask_luma.h"

namespace pittore::svg {

float luminance(float r, float g, float b) {
    return 0.2126f * r + 0.7152f * g + 0.0722f * b;
}

std::uint8_t maskAlpha(float r, float g, float b, float a) {
    const float l = luminance(r, g, b) * a;
    const int v = (int)(l * 255.0f + 0.5f);
    return (std::uint8_t)(v < 0 ? 0 : v > 255 ? 255 : v);
}

}  // namespace pittore::svg
