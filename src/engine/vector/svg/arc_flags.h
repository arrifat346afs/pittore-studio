#pragma once
// Arc flags to bools.
#include <string>

namespace pittore::svg {

struct ArcFlags {
    bool large = false;
    bool sweep = false;
};

ArcFlags parseArcFlags(const std::string& a, const std::string& b);

}  // namespace pittore::svg
