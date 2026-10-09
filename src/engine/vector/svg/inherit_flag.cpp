// Small inherit set.
#include "engine/vector/svg/inherit_flag.h"

namespace pittore::svg {

bool inheritsProp(const std::string& prop) {
    return prop == "fill" || prop == "stroke" || prop == "opacity" ||
           prop == "fill-opacity" || prop == "stroke-opacity" ||
           prop == "stroke-width" || prop == "font-size" ||
           prop == "font-family" || prop == "text-anchor" ||
           prop == "color" || prop == "visibility";
}

}  // namespace pittore::svg
