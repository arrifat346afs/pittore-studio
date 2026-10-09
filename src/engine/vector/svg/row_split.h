#pragma once
// Row ranges with grain. Pure math.
#include <utility>
#include <vector>

namespace pittore::svg {

std::vector<std::pair<int, int>> splitRows(int rows, int threads,
                                           int minPer = 64);

}  // namespace pittore::svg
