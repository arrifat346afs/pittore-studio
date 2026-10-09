// Default is luminance.
#include "engine/vector/svg/mask_units.h"

namespace pittore::svg {

bool maskIsAlpha(const std::string& type) {
    return type == "alpha";
}

}  // namespace pittore::svg
