// Mid gray threshold.
#include "engine/vector/svg/contrast_pick.h"

#include "engine/vector/svg/luma_gray.h"

namespace pittore::svg {

bool useWhiteText(float r, float g, float b) {
    return grayOf(r, g, b) < 0.5f;
}

}  // namespace pittore::svg
