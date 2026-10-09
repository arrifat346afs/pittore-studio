#pragma once
// Prefix split. Returns local part.
#include <string>

namespace pittore::svg {

std::string localName(const std::string& qname);
std::string prefixOf(const std::string& qname);

}  // namespace pittore::svg
