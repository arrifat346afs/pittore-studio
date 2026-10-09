// Thin wrapper.
#include "engine/vector/svg/grad_xform.h"

namespace pittore::svg {

Affine parseGradientTransform(const std::string& s) {
    if (s.empty()) {
        return identity();
    }
    return parseTransform(s);
}

}  // namespace pittore::svg
