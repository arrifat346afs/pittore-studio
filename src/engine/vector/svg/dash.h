#pragma once
// Dash list parse. Empty means solid.
#include <string>
#include <vector>

namespace pittore::svg {

std::vector<double> parseDashArray(const std::string& s);
double parseDashOffset(const std::string& s);

}  // namespace pittore::svg
