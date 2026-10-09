// Conservative path bounds from segments for viewport culling.
#include "engine/vector/svg/path_bounds.h"

namespace pittore::svg {
namespace {

void addPt(SegBox& b, double x, double y) {
    if (b.empty) {
        b.x0 = b.x1 = x;
        b.y0 = b.y1 = y;
        b.empty = false;
        return;
    }
    if (x < b.x0) b.x0 = x;
    if (y < b.y0) b.y0 = y;
    if (x > b.x1) b.x1 = x;
    if (y > b.y1) b.y1 = y;
}

}  // namespace

bool pathSegBox(const std::vector<PathSeg>& segs, SegBox& out) {
    double cx = 0, cy = 0, sx = 0, sy = 0;
    for (const auto& g : segs) {
        switch (g.type) {
            case SegType::Move: {
                const double x = g.rel ? cx + g.v[0] : g.v[0];
                const double y = g.rel ? cy + g.v[1] : g.v[1];
                cx = sx = x;
                cy = sy = y;
                addPt(out, x, y);
                break;
            }
            case SegType::Line:
            case SegType::SmoothQuad: {
                const double x = g.rel ? cx + g.v[0] : g.v[0];
                const double y = g.rel ? cy + g.v[1] : g.v[1];
                cx = x;
                cy = y;
                addPt(out, x, y);
                break;
            }
            case SegType::H:
                cx = g.rel ? cx + g.v[0] : g.v[0];
                addPt(out, cx, cy);
                break;
            case SegType::V:
                cy = g.rel ? cy + g.v[0] : g.v[0];
                addPt(out, cx, cy);
                break;
            case SegType::Cubic:
            case SegType::SmoothCubic: {
                for (int k = 0; k < 6; k += 2) {
                    addPt(out, g.rel ? cx + g.v[k] : g.v[k],
                          g.rel ? cy + g.v[k + 1] : g.v[k + 1]);
                }
                cx = g.rel ? cx + g.v[4] : g.v[4];
                cy = g.rel ? cy + g.v[5] : g.v[5];
                break;
            }
            case SegType::Quad: {
                addPt(out, g.rel ? cx + g.v[0] : g.v[0],
                      g.rel ? cy + g.v[1] : g.v[1]);
                cx = g.rel ? cx + g.v[2] : g.v[2];
                cy = g.rel ? cy + g.v[3] : g.v[3];
                addPt(out, cx, cy);
                break;
            }
            case SegType::Arc: {
                cx = g.rel ? cx + g.v[5] : g.v[5];
                cy = g.rel ? cy + g.v[6] : g.v[6];
                addPt(out, cx, cy);
                addPt(out, cx - g.v[0], cy - g.v[1]);
                addPt(out, cx + g.v[0], cy + g.v[1]);
                break;
            }
            case SegType::Close:
                cx = sx;
                cy = sy;
                break;
        }
    }
    return !out.empty;
}

SegBox pathWorldBox(const std::vector<PathSeg>& segs, const Affine& m,
                    double pad) {
    SegBox local;
    SegBox world;
    if (!pathSegBox(segs, local)) {
        return world;
    }
    const double xs[4] = {local.x0 - pad, local.x1 + pad, local.x0 - pad,
                          local.x1 + pad};
    const double ys[4] = {local.y0 - pad, local.y1 - pad, local.y1 + pad,
                          local.y0 + pad};
    for (int i = 0; i < 4; ++i) {
        addPt(world, m.a * xs[i] + m.c * ys[i] + m.e,
              m.b * xs[i] + m.d * ys[i] + m.f);
    }
    return world;
}

}  // namespace pittore::svg
