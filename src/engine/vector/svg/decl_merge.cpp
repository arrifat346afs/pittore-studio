// Copy then overwrite.
#include "engine/vector/svg/decl_merge.h"

namespace pittore::svg {

std::map<std::string, std::string> mergeDecls(
    const std::map<std::string, std::string>& a,
    const std::map<std::string, std::string>& b) {
    auto out = a;
    for (const auto& [k, v] : b) {
        out[k] = v;
    }
    return out;
}

}  // namespace pittore::svg
