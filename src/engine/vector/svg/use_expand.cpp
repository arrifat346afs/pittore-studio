// Clone target geom into use nodes. Depth capped.
#include "engine/vector/svg/use_expand.h"

#include <unordered_map>

#include "engine/vector/svg/scene.h"

namespace pittore::svg {
namespace {

void indexIds(SceneNode& n, std::unordered_map<std::string, SceneNode*>& m) {
    if (!n.id.empty()) {
        m[n.id] = &n;
    }
    for (auto& c : n.children) {
        indexIds(*c, m);
    }
}

int expandAt(SceneNode& n, std::unordered_map<std::string, SceneNode*>& m,
             int depth) {
    int done = 0;
    for (auto& c : n.children) {
        done += expandAt(*c, m, depth);
    }
    if (n.kind == NodeKind::Use && depth < 8) {
        auto it = m.find(n.href);
        if (it == m.end() || it->second == &n) {
            return done;
        }
        SceneNode* t = it->second;
        if (t->kind == NodeKind::Use) {
            return done;
        }
        // Copy shape geom, keep use placement offset.
        const double ox = n.x, oy = n.y;
        n.kind = t->kind;
        n.points = t->points;
        n.segs = t->segs;
        n.text = t->text;
        n.x = t->x + ox;
        n.y = t->y + oy;
        n.w = t->w;
        n.h = t->h;
        n.rx = t->rx;
        n.ry = t->ry;
        n.cx = t->cx + ox;
        n.cy = t->cy + oy;
        n.r = t->r;
        n.x1 = t->x1 + ox;
        n.y1 = t->y1 + oy;
        n.x2 = t->x2 + ox;
        n.y2 = t->y2 + oy;
        n.href = "";
        n.children = t->children;
        ++done;
    }
    return done;
}

}  // namespace

int expandUses(SceneNode& root) {
    std::unordered_map<std::string, SceneNode*> m;
    indexIds(root, m);
    return expandAt(root, m, 0);
}

}  // namespace pittore::svg
