// Empty means no scale.
#include "engine/vector/svg/path_length.h"

#include <cstdlib>

namespace pittore::svg {

double pathLengthScale(const std::string& attr, double realLen) {
    if (attr.empty() || realLen <= 0) {
        return 1.0;
    }
    char* end = nullptr;
    const double v = std::strtod(attr.c_str(), &end);
    if (end == attr.c_str() || v <= 0) {
        return 1.0;
    }
    return v / realLen;
}

}  // namespace pittore::svg
