#pragma once
// Display and visibility flags.
#include <string>

namespace pittore::svg {

bool isDisplayNone(const std::string& display);
bool isVisible(const std::string& visibility);

}  // namespace pittore::svg
