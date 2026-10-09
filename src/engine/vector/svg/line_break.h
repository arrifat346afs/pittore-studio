#pragma once
// Greedy wrap. Widths to line breaks.
#include <vector>

namespace pittore::svg {

std::vector<int> wrapLines(const std::vector<double>& widths, double maxW);

}  // namespace pittore::svg
