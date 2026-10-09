#pragma once
// Old flat parser vs new tree. Shared fixtures only.
#include <string>

namespace pittore::svg {

bool checkFlatParity(const std::string& xml);

}  // namespace pittore::svg
