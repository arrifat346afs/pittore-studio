#pragma once
// Filter unit flags.
#include <string>

namespace pittore::svg {

bool filterIsUserSpace(const std::string& units);
bool primIsUserSpace(const std::string& units);

}  // namespace pittore::svg
