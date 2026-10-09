#pragma once
// View-based double scan. No string copy, no locale, no strtod.
#include <string_view>
#include <vector>

namespace pittore::svg {

// Parse one SVG number at [p, end). Advances p past it.
bool scanNumber(const char*& p, const char* end, double& out);

// Fill out with every number in the view. Stops at the first bad token.
void scanDoubles(std::string_view in, std::vector<double>& out);

}  // namespace pittore::svg
