// %g with short output.
#include "engine/vector/svg/attr_write.h"

#include <cstdio>

namespace pittore::svg {

std::string fmtNum(double v) {
    char b[32];
    std::snprintf(b, sizeof b, "%g", v);
    return b;
}

}  // namespace pittore::svg
