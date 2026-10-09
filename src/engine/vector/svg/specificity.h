#pragma once
// Selector score. Id wins over class over tag.
#include <string>

namespace pittore::svg {

int specificity(const std::string& selector);

}  // namespace pittore::svg
