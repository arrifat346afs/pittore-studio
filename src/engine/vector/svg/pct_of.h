#pragma once
// Percent string to fraction.
#include <string>

namespace pittore::svg {

double pctFraction(const std::string& s, double fb = 0.0);

}  // namespace pittore::svg
