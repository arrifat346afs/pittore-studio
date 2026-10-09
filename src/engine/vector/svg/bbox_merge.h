#pragma once
// Merge boxes.
#include <vector>

#include "engine/vector/svg/bounds.h"

namespace pittore::svg {

BBox mergeBoxes(const std::vector<BBox>& boxes);

}  // namespace pittore::svg
