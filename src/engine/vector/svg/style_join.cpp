// Key order is sorted by map.
#include "engine/vector/svg/style_join.h"

namespace pittore::svg {

std::string joinStyle(const std::map<std::string, std::string>& decls) {
    std::string o;
    for (const auto& [k, v] : decls) {
        if (!o.empty()) {
            o += ";";
        }
        o += k + ":" + v;
    }
    return o;
}

}  // namespace pittore::svg
