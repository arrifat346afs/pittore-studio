#pragma once
// Clip units flag. True means user space.
#include <string>

namespace pittore::svg {

bool clipIsUserSpace(const std::string& units);

}  // namespace pittore::svg
