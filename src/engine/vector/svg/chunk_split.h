#pragma once
// Range split math for the pool.
#include <utility>
#include <vector>

namespace pittore::svg {

std::vector<std::pair<int, int>> splitRanges(int count, int parts);
bool needsThreads(int pxCount);

}  // namespace pittore::svg
