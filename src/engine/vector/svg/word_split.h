#pragma once
// Words split on blanks. Keeps order.
#include <string>
#include <vector>

namespace pittore::svg {

std::vector<std::string> splitWords(const std::string& s);

}  // namespace pittore::svg
