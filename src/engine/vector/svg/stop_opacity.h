#pragma once
// Stop alpha. Clamped 0..1.
#include <string>

namespace pittore::svg {

float stopAlpha(const std::string& s, float fb = 1.0f);

}  // namespace pittore::svg
