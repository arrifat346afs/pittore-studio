#pragma once
// Raw XML tree. Tolerant input, keeps text for save.
#include <map>
#include <memory>
#include <string>
#include <vector>

namespace pittore::svg {

// One element node.
struct XmlNode {
    std::string tag;
    std::map<std::string, std::string> attrs;
    std::vector<std::shared_ptr<XmlNode>> children;
    std::string text;
    XmlNode* parent = nullptr;
};

// Parse result. Never throws.
struct XmlRead {
    std::shared_ptr<XmlNode> root;
    bool ok = false;
    std::string error;
};

XmlRead readXml(const std::string& xml);

}  // namespace pittore::svg
