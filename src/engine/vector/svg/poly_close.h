#pragma once
// Close ring when open.
#include <utility>
#include <vector>

namespace pittore::svg {

void closeRing(std::vector<std::pair<double, double>>& pts);

}  // namespace pittore::svg
