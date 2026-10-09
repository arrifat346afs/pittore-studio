#pragma once
// Gradient transform wrapper.
#include <string>

#include "engine/vector/svg/transform.h"

namespace pittore::svg {

Affine parseGradientTransform(const std::string& s);

}  // namespace pittore::svg
