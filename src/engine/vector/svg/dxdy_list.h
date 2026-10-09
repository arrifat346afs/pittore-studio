#pragma once
// Position lists for text chunks.
#include <string>
#include <vector>

namespace pittore::svg {

std::vector<double> parsePosList(const std::string& s, double ref);

}  // namespace pittore::svg
