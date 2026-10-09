#pragma once
// Import caps. True when over budget.
#include <cstddef>

namespace pittore::svg {

bool overSizeCap(std::size_t bytes);
bool overTagCap(int tags);
bool overUseCap(int uses);

}  // namespace pittore::svg
