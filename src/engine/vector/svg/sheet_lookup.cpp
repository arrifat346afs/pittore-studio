// Later rules win. Match is tag/class/id only.
#include "engine/vector/svg/sheet_lookup.h"

#include "engine/vector/svg/tag_match.h"

namespace pittore::svg {

std::string lookupSheet(const std::vector<CssRule>& sheet,
                        const std::string& tag, const std::string& id,
                        const std::string& classes, const std::string& prop) {
    std::string out;
    for (const auto& r : sheet) {
        if (!selectorMatches(r.selector, tag, id, classes)) {
            continue;
        }
        auto it = r.decls.find(prop);
        if (it != r.decls.end()) {
            out = it->second;
        }
    }
    return out;
}

}  // namespace pittore::svg
