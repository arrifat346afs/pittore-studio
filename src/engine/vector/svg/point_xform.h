#pragma once
// Points through matrix.
#include <utility>
#include <vector>

#include "engine/vector/svg/transform.h"

namespace pittore::svg {

std::vector<std::pair<double, double>> xformPoints(
    const std::vector<std::pair<double, double>>& pts, const Affine& m);

}  // namespace pittore::svg
