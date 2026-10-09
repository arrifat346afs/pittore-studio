#pragma once
// Gradient units flag. True means user space.
#include <string>

namespace pittore::svg {

bool isUserSpaceUnits(const std::string& units);

}  // namespace pittore::svg
