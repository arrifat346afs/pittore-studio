#include "ui/persona/vector_view.h"

#include <limits>

#include "engine/vector/vector_art.h"
#include "ui/app_state.h"

namespace pittore::ui {
namespace {

// A grouped leaf may draw live when it reproduces its composite pixels:
// the leaf's own paint must be fully reproducible by the live draw (no
// pattern, markers, clip, mask, mesh or filter, which only the baked
// raster carries), and every ancestor group must be transparent to
// compositing (no opacity, blend, mask, style, filter or clip of its
// own). Either side carrying an effect keeps the raster path, so the
// display never diverges from the composite.
bool groupTransparent(const DocumentItem& d, const QVector<int>& parent,
                      int i) {
    const LayerItem& l = d.layers[i];
    if (l.indent == 0) return true;
    const auto& pt = l.art->paint;
    if (!pt.patternId.empty()) return false;
    if (!pt.markerStart.empty() || !pt.markerMid.empty() ||
        !pt.markerEnd.empty())
        return false;
    if (!pt.clipId.empty() || !pt.maskId.empty()) return false;
    if (pt.hasMesh || pt.hasFilter) return false;
    int steps = 0;
    int p = parent[i];
    while (p >= 0 && p < d.layers.size() &&
           steps <= d.layers.size()) {
        ++steps;
        const LayerItem& g = d.layers[p];
        if (g.kind != LayerItem::Kind::Group) return false;
        if (g.toneBlendGroup) return false;
        if (g.opacity != 100 || g.fill != 100) return false;
        if (g.blendMode != QLatin1String("Normal")) return false;
        if (g.hasMask || !g.style.empty()) return false;
        if (g.hasLiveFilter && g.liveFilterEnabled) return false;
        if (g.clipped) return false;
        p = parent[p];
    }
    return true;
}

}  // namespace

QVector<int> vectorViewOrder(const DocumentItem& d) {
    // An adjustment grades every layer beneath it, so only vectors above the
    // topmost one can bypass the raster path (adjustments below never touch
    // them; adjustments above force the whole stack below to bake).
    int topmostAdjustment = std::numeric_limits<int>::max();
    for (int i = 0; i < d.layers.size(); ++i) {
        if (d.layers[i].kind == LayerItem::Kind::Adjustment &&
            d.effectivelyVisible(i))
            topmostAdjustment = qMin(topmostAdjustment, i);
    }

    QVector<int> out;
    QVector<char> vis;
    QVector<int> parent;
    d.effectiveVisibility(vis, parent);
    for (int i = 0; i < d.layers.size(); ++i) {
        const LayerItem& l = d.layers[i];
        if (!vis[i]) continue;
        if (!l.art || l.art->isEmpty()) continue;
        if (l.kind != LayerItem::Kind::Pixel) continue;
        if (!groupTransparent(d, parent, i)) continue;
        if (i >= topmostAdjustment) continue;
        if (l.blendMode != QLatin1String("Normal")) continue;
        if (l.opacity != 100 || l.fill != 100) continue;
        if (l.hasMask) continue;
        if (l.clipped) continue;
        if (!l.style.empty()) continue;
        if (l.hasLiveFilter && l.liveFilterEnabled) continue;
        if (!l.pixels) continue;  // identity fast path needs the bake below
        out.append(i);
    }
    return out;
}

}  // namespace pittore::ui
