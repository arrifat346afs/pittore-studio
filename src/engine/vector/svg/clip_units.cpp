// Default is user space.
#include "engine/vector/svg/clip_units.h"

namespace pittore::svg {

bool clipIsUserSpace(const std::string& units) {
    return units.empty() || units == "userSpaceOnUse";
}

}  // namespace pittore::svg
