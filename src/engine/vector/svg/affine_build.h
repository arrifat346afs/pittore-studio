#pragma once
// Affine builders.
#include "engine/vector/svg/transform.h"

namespace pittore::svg {

Affine makeTranslate(double tx, double ty);
Affine makeScale(double sx, double sy);
Affine makeRotate(double deg);

}  // namespace pittore::svg
