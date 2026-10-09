// strtod plus suffix rest.
#include "engine/vector/svg/num_unit.h"

#include <cstdlib>

namespace pittore::svg {

NumUnit splitNumUnit(const std::string& s) {
    NumUnit o;
    char* end = nullptr;
    const double v = std::strtod(s.c_str(), &end);
    if (end == s.c_str()) {
        return o;
    }
    o.value = v;
    o.unit = end ? std::string(end) : std::string();
    o.valid = true;
    return o;
}

}  // namespace pittore::svg
