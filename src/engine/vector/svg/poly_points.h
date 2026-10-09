#pragma once
// Points list parse.
#include <string>
#include <utility>
#include <vector>

namespace pittore::svg {

std::vector<std::pair<double, double>> parsePolyPoints(const std::string& s);

}  // namespace pittore::svg
