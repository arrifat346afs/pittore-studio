#pragma once
// Path length scale. Maps author's total to real one.
#include <string>

namespace pittore::svg {

double pathLengthScale(const std::string& attr, double realLen);

}  // namespace pittore::svg
