// Empty means object box.
#include "engine/vector/svg/filter_units.h"

namespace pittore::svg {

bool filterIsUserSpace(const std::string& units) {
    return units == "userSpaceOnUse";
}

bool primIsUserSpace(const std::string& units) {
    return units == "userSpaceOnUse";
}

}  // namespace pittore::svg
