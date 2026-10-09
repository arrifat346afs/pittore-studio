#pragma once
// Angle to degrees. Bad input gives 0.
#include <string>

namespace pittore::svg {

double parseAngleDeg(const std::string& s);

}  // namespace pittore::svg
