#pragma once
// Double list scan. Commas and blanks split.
#include <string>
#include <vector>

namespace pittore::svg {

std::vector<double> parseDoubles(const std::string& s);

}  // namespace pittore::svg
