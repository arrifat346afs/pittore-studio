// Default is object box.
#include "engine/vector/svg/gradient_units.h"

namespace pittore::svg {

bool isUserSpaceUnits(const std::string& units) {
    return units == "userSpaceOnUse";
}

}  // namespace pittore::svg
