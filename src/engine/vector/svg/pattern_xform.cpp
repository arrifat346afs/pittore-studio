// Thin wrapper.
#include "engine/vector/svg/pattern_xform.h"

namespace pittore::svg {

Affine parsePatternTransform(const std::string& s) {
    if (s.empty()) {
        return identity();
    }
    return parseTransform(s);
}

}  // namespace pittore::svg
