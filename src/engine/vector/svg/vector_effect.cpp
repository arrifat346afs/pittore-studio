// Exact token only.
#include "engine/vector/svg/vector_effect.h"

namespace pittore::svg {

bool isNonScalingStroke(const std::string& effect) {
    return effect == "non-scaling-stroke";
}

}  // namespace pittore::svg
