// Same match set as style pass.
#include "engine/vector/svg/tag_match.h"

#include "engine/vector/svg/class_split.h"

namespace pittore::svg {

bool selectorMatches(const std::string& sel, const std::string& tag,
                     const std::string& id, const std::string& classes) {
    if (sel.empty() || sel == "*") {
        return true;
    }
    if (sel[0] == '#') {
        return id == sel.substr(1);
    }
    const auto list = splitClasses(classes);
    auto has = [&](const std::string& c) {
        for (const auto& k : list) {
            if (k == c) {
                return true;
            }
        }
        return false;
    };
    if (sel[0] == '.') {
        return has(sel.substr(1));
    }
    const size_t dot = sel.find('.');
    if (dot != std::string::npos) {
        return tag == sel.substr(0, dot) && has(sel.substr(dot + 1));
    }
    return tag == sel;
}

}  // namespace pittore::svg
