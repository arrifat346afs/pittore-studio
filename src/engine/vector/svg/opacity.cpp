// Clamp 0..1.
#include "engine/vector/svg/opacity.h"

namespace pittore::svg {

float effectiveOpacity(float parent, float local, float extra) {
    const float v = parent * local * extra;
    return v < 0 ? 0 : v > 1 ? 1 : v;
}

}  // namespace pittore::svg
