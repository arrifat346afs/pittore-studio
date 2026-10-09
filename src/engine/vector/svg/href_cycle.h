#pragma once
// Href cycle check. True when a loop exists.
#include <map>
#include <string>
#include <vector>

namespace pittore::svg {

bool hasHrefCycle(const std::map<std::string, std::string>& hrefOf);

}  // namespace pittore::svg
