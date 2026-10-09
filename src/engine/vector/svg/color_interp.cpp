// Linear words.
#include "engine/vector/svg/color_interp.h"

namespace pittore::svg {

bool isLinearColor(const std::string& interp) {
    return interp == "linearRGB" || interp == "linear-rgb";
}

}  // namespace pittore::svg
