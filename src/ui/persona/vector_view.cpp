#include "ui/persona/vector_view.h"

#include <limits>

#include "engine/vector/vector_art.h"
#include "ui/app_state.h"

namespace pittore::ui {

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
        if (l.indent != 0) continue;  // groups composite as raster subtrees
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
