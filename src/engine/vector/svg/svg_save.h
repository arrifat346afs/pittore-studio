#pragma once
// XML tree to text. Stable order, keeps unknowns.
#include <string>

namespace pittore::svg {

struct XmlNode;

std::string saveXml(const XmlNode& root);

}  // namespace pittore::svg
