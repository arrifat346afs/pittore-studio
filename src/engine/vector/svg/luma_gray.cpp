// Same weights as mask pass.
#include "engine/vector/svg/luma_gray.h"

namespace pittore::svg {

float grayOf(float r, float g, float b) {
    return 0.2126f * r + 0.7152f * g + 0.0722f * b;
}

}  // namespace pittore::svg
