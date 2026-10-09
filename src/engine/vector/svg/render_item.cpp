// Scene to items. Copies geom, drops hidden subtrees.
#include "engine/vector/svg/render_item.h"

#include "engine/vector/svg/scene.h"

namespace pittore::svg {
namespace {

ItemKind mapKind(NodeKind k, bool polyClose) {
    switch (k) {
        case NodeKind::Rect:
            return ItemKind::Rect;
        case NodeKind::Circle:
            return ItemKind::Circle;
        case NodeKind::Ellipse:
            return ItemKind::Ellipse;
        case NodeKind::Line:
            return ItemKind::Line;
        case NodeKind::Polyline:
            return ItemKind::Poly;
        case NodeKind::Polygon:
            return ItemKind::Poly;
        case NodeKind::Path:
            return ItemKind::Path;
        case NodeKind::Use:
            return ItemKind::Use;
        case NodeKind::Image:
            return ItemKind::Image;
        case NodeKind::Text:
            return ItemKind::Text;
        default:
            return ItemKind::Group;
    }
    (void)polyClose;
}

void copyGeom(const SceneNode& n, RenderItem& it) {
    it.x = n.x;
    it.y = n.y;
    it.w = n.w;
    it.h = n.h;
    it.rx = n.rx;
    it.ry = n.ry;
    it.cx = n.cx;
    it.cy = n.cy;
    it.r = n.r;
    it.x1 = n.x1;
    it.y1 = n.y1;
    it.x2 = n.x2;
    it.y2 = n.y2;
    it.points = n.points;
    it.segs = n.segs;
    it.href = n.href;
    it.text = n.text;
    it.closePoly = n.kind == NodeKind::Polygon;
}

void walk(const SceneNode& n, std::vector<RenderItem>& out) {
    if (n.style.displayNone) {
        return;
    }
    RenderItem it;
    it.kind = n.kind == NodeKind::Group ? ItemKind::Group : mapKind(n.kind, false);
    it.id = n.id;
    it.world = n.world;
    it.style = n.style;
    copyGeom(n, it);
    out.push_back(it);
    for (const auto& c : n.children) {
        walk(*c, out);
    }
}

}  // namespace

void appendRenderItems(const SceneNode& n, std::vector<RenderItem>& out) {
    walk(n, out);
}

std::vector<RenderItem> flattenScene(const Scene& scene) {
    std::vector<RenderItem> out;
    if (scene.root) {
        walk(*scene.root, out);
    }
    return out;
}

}  // namespace pittore::svg
