// Bounds in world space. Corners through matrix.
#include "engine/vector/svg/bounds.h"

#include <algorithm>

#include "engine/vector/svg/path_bounds.h"
#include "engine/vector/svg/render_item.h"

namespace pittore::svg {
namespace {

void addPt(BBox& b, double x, double y, const Affine& m) {
    const double wx = m.a * x + m.c * y + m.e;
    const double wy = m.b * x + m.d * y + m.f;
    if (b.empty) {
        b.x0 = b.x1 = wx;
        b.y0 = b.y1 = wy;
        b.empty = false;
        return;
    }
    b.x0 = std::min(b.x0, wx);
    b.y0 = std::min(b.y0, wy);
    b.x1 = std::max(b.x1, wx);
    b.y1 = std::max(b.y1, wy);
}

}  // namespace

BBox itemBounds(const RenderItem& it) {
    BBox b;
    const double pad = it.style.stroke.none ? 0 : it.style.strokeWidth * 0.5;
    switch (it.kind) {
        case ItemKind::Rect:
            addPt(b, it.x - pad, it.y - pad, it.world);
            addPt(b, it.x + it.w + pad, it.y + it.h + pad, it.world);
            break;
        case ItemKind::Circle:
            addPt(b, it.cx - it.r - pad, it.cy - it.r - pad, it.world);
            addPt(b, it.cx + it.r + pad, it.cy + it.r + pad, it.world);
            break;
        case ItemKind::Ellipse:
            addPt(b, it.cx - it.rx - pad, it.cy - it.ry - pad, it.world);
            addPt(b, it.cx + it.rx + pad, it.cy + it.ry + pad, it.world);
            break;
        case ItemKind::Line:
            addPt(b, it.x1 - pad, it.y1 - pad, it.world);
            addPt(b, it.x2 + pad, it.y2 + pad, it.world);
            break;
        case ItemKind::Poly:
            for (const auto& p : it.points) {
                addPt(b, p.first - pad, p.second - pad, it.world);
                addPt(b, p.first + pad, p.second + pad, it.world);
            }
            break;
        case ItemKind::Path: {
            const SegBox w = pathWorldBox(it.segs, it.world, pad);
            if (!w.empty) {
                addPt(b, w.x0, w.y0, identity());
                addPt(b, w.x1, w.y1, identity());
            }
            break;
        }
        case ItemKind::Image:
        case ItemKind::Use:
            addPt(b, it.x, it.y, it.world);
            addPt(b, it.x + it.w, it.y + it.h, it.world);
            break;
        case ItemKind::Text:
            addPt(b, it.x, it.y, it.world);
            addPt(b, it.x + it.w, it.y + it.h, it.world);
            break;
        default:
            break;
    }
    return b;
}

BBox unionBox(const BBox& a, const BBox& b) {
    if (a.empty) {
        return b;
    }
    if (b.empty) {
        return a;
    }
    BBox o;
    o.empty = false;
    o.x0 = std::min(a.x0, b.x0);
    o.y0 = std::min(a.y0, b.y0);
    o.x1 = std::max(a.x1, b.x1);
    o.y1 = std::max(a.y1, b.y1);
    return o;
}

}  // namespace pittore::svg
