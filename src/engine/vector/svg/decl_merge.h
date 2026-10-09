#pragma once
// Later decls win. Merges two maps.
#include <map>
#include <string>

namespace pittore::svg {

std::map<std::string, std::string> mergeDecls(
    const std::map<std::string, std::string>& a,
    const std::map<std::string, std::string>& b);

}  // namespace pittore::svg
