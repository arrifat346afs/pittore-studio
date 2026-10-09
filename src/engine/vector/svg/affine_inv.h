#pragma once
// Invert matrix. False when singular.
#include "engine/vector/svg/transform.h"

namespace pittore::svg {

bool invertAffine(const Affine& m, Affine& out);

}  // namespace pittore::svg
