// DFS with visiting set.
#include "engine/vector/svg/href_cycle.h"

#include <functional>
#include <unordered_set>

namespace pittore::svg {

bool hasHrefCycle(const std::map<std::string, std::string>& hrefOf) {
    std::unordered_set<std::string> done;
    std::function<bool(const std::string&, std::unordered_set<std::string>&)> go =
        [&](const std::string& id, std::unordered_set<std::string>& path) {
            if (path.count(id)) {
                return true;
            }
            if (done.count(id)) {
                return false;
            }
            auto it = hrefOf.find(id);
            if (it == hrefOf.end() || it->second.empty()) {
                done.insert(id);
                return false;
            }
            path.insert(id);
            const bool hit = go(it->second, path);
            path.erase(id);
            done.insert(id);
            return hit;
        };
    for (const auto& [k, v] : hrefOf) {
        std::unordered_set<std::string> path;
        if (go(k, path)) {
            return true;
        }
    }
    return false;
}

}  // namespace pittore::svg
