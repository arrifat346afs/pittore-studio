#pragma once
// Point through matrix.
#include "engine/vector/svg/transform.h"

namespace pittore::svg {

void applyAffine(const Affine& m, double x, double y, double& ox, double& oy);

}  // namespace pittore::svg
