// Missing refs and empty geom only.
#include "engine/vector/svg/validate.h"

#include <functional>
#include <unordered_set>

#include "engine/vector/svg/scene.h"

namespace pittore::svg {

std::vector<Issue> validateScene(const Scene& scene) {
    std::vector<Issue> out;
    if (!scene.ok || !scene.root) {
        out.push_back(Issue{"", "empty scene"});
        return out;
    }
    std::unordered_set<std::string> ids;
    std::function<void(const SceneNode&)> index = [&](const SceneNode& n) {
        if (!n.id.empty()) {
            ids.insert(n.id);
        }
        for (const auto& c : n.children) {
            index(*c);
        }
    };
    index(*scene.root);
    std::function<void(const SceneNode&)> check = [&](const SceneNode& n) {
        if (n.kind == NodeKind::Use && !n.href.empty() &&
            ids.find(n.href) == ids.end()) {
            out.push_back(Issue{n.id, "missing use target"});
        }
        if (n.kind == NodeKind::Path && n.segs.empty()) {
            out.push_back(Issue{n.id, "empty path"});
        }
        if (n.kind == NodeKind::Rect && (n.w <= 0 || n.h <= 0)) {
            out.push_back(Issue{n.id, "empty rect"});
        }
        for (const auto& c : n.children) {
            check(*c);
        }
    };
    check(*scene.root);
    return out;
}

}  // namespace pittore::svg
