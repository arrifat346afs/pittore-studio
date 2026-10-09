#pragma once
// Pattern transform wrapper.
#include <string>

#include "engine/vector/svg/transform.h"

namespace pittore::svg {

Affine parsePatternTransform(const std::string& s);

}  // namespace pittore::svg
