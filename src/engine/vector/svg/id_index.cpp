// Depth-first index.
#include "engine/vector/svg/id_index.h"

#include <functional>

#include "engine/vector/svg/xml_reader.h"

namespace pittore::svg {

std::unordered_map<std::string, const XmlNode*> indexIds(
    const XmlNode& root) {
    std::unordered_map<std::string, const XmlNode*> m;
    std::function<void(const XmlNode&)> walk = [&](const XmlNode& e) {
        auto it = e.attrs.find("id");
        if (it != e.attrs.end() && !it->second.empty()) {
            m[it->second] = &e;
        }
        for (const auto& c : e.children) {
            walk(*c);
        }
    };
    walk(root);
    return m;
}

}  // namespace pittore::svg
