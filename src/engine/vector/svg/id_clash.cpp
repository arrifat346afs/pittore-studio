// Suffix ids, rewrite url refs.
#include "engine/vector/svg/id_clash.h"

#include "engine/vector/svg/iri.h"

namespace pittore::svg {

std::map<std::string, std::string> remapIds(
    const std::map<std::string, std::string>& attrs, const std::string& suffix) {
    std::map<std::string, std::string> out = attrs;
    auto it = out.find("id");
    std::string oldId;
    if (it != out.end()) {
        oldId = it->second;
        it->second = oldId + suffix;
    }
    for (auto& [k, v] : out) {
        const std::string t = refTarget(v);
        if (!t.empty() && t == oldId) {
            v = "url(#" + t + suffix + ")";
        } else if (!v.empty() && v[0] == '#' && v.substr(1) == oldId) {
            v = "#" + t + suffix;
        }
    }
    return out;
}

}  // namespace pittore::svg
