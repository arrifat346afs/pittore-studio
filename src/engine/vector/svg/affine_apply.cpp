// Direct map.
#include "engine/vector/svg/affine_apply.h"

namespace pittore::svg {

void applyAffine(const Affine& m, double x, double y, double& ox, double& oy) {
    ox = m.a * x + m.c * y + m.e;
    oy = m.b * x + m.d * y + m.f;
}

}  // namespace pittore::svg
