#pragma once
// Selector match for one node description.
#include <string>

namespace pittore::svg {

bool selectorMatches(const std::string& sel, const std::string& tag,
                     const std::string& id, const std::string& classes);

}  // namespace pittore::svg
