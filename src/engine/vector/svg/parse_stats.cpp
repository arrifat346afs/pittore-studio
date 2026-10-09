// Walk and count.
#include "engine/vector/svg/parse_stats.h"

#include <functional>

#include "engine/vector/svg/xml_reader.h"

namespace pittore::svg {

SvgStats collectStats(const XmlNode& root) {
    SvgStats s;
    std::function<void(const XmlNode&, int)> walk = [&](const XmlNode& e,
                                                        int d) {
        ++s.nodes;
        if (d > s.maxDepth) {
            s.maxDepth = d;
        }
        if (e.tag == "path") {
            ++s.paths;
        }
        if (e.tag == "use") {
            ++s.uses;
        }
        for (const auto& c : e.children) {
            walk(*c, d + 1);
        }
    };
    walk(root, 0);
    return s;
}

}  // namespace pittore::svg
