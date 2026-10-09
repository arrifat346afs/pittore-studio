#pragma once
// Known attrs per tag. Rest round-trips.
#include <map>
#include <string>

namespace pittore::svg {

bool isKnownAttr(const std::string& tag, const std::string& attr);
std::map<std::string, std::string> unknownAttrs(
    const std::string& tag, const std::map<std::string, std::string>& attrs);

}  // namespace pittore::svg
