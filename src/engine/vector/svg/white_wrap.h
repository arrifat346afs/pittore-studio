#pragma once
// Collapse blanks unless preserved.
#include <string>

namespace pittore::svg {

std::string collapseWhite(const std::string& s, bool preserve);

}  // namespace pittore::svg
