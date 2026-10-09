#pragma once
// Clip rule flag. True means even-odd.
#include <string>

namespace pittore::svg {

bool isClipEvenOdd(const std::string& rule);

}  // namespace pittore::svg
