// Clamped sum.
#include "engine/vector/svg/alpha_mix.h"

namespace pittore::svg {

float overAlpha(float srcA, float dstA) {
    const float v = srcA + dstA * (1 - srcA);
    return v < 0 ? 0 : v > 1 ? 1 : v;
}

}  // namespace pittore::svg
