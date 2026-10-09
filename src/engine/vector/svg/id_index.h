#pragma once
// Id index over XML tree.
#include <string>
#include <unordered_map>

namespace pittore::svg {

struct XmlNode;

std::unordered_map<std::string, const XmlNode*> indexIds(const XmlNode& root);

}  // namespace pittore::svg
