// Default is object box.
#include "engine/vector/svg/pattern_units.h"

namespace pittore::svg {

bool patternIsUserSpace(const std::string& units) {
    return units == "userSpaceOnUse";
}

}  // namespace pittore::svg
