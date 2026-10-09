#pragma once
// Color space flag. True means linear.
#include <string>

namespace pittore::svg {

bool isLinearColor(const std::string& interp);

}  // namespace pittore::svg
