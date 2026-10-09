#pragma once
// Strip UTF-8 BOM prefix.
#include <string>

namespace pittore::svg {

std::string stripBom(const std::string& s);

}  // namespace pittore::svg
