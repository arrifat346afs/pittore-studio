#pragma once
// Mask type flag. True means alpha.
#include <string>

namespace pittore::svg {

bool maskIsAlpha(const std::string& type);

}  // namespace pittore::svg
