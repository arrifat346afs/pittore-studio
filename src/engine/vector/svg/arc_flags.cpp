// Non-zero means set.
#include "engine/vector/svg/arc_flags.h"

namespace pittore::svg {

ArcFlags parseArcFlags(const std::string& a, const std::string& b) {
    ArcFlags f;
    f.large = !a.empty() && a != "0";
    f.sweep = !b.empty() && b != "0";
    return f;
}

}  // namespace pittore::svg
