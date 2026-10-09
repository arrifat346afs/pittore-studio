#pragma once
// Count open tags up to cap.
#include <string>

namespace pittore::svg {

int countTags(const std::string& s, int cap);

}  // namespace pittore::svg
