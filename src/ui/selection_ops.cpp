// Selection <-> mask interchange + Select > Modify ops (defined here, not in
// app_state.cpp, per the new-source-files rule; only declared in app_state.h).
//
// Semantics (editor-matching, documented where we differ):
// - Reveal/Hide Selection paint the live selection into the active layer's
//   mask (white reveals). A missing mask is created; an existing mask is
//   intersected (reveal) or subtracted (hide) rather than replaced.
// - From Transparency converts pixel alpha into mask coverage and makes the
//   pixels opaque.
// - Load Mask as Selection rasterises the *finished* mask (density/feather
//   baked) into the selection channel.
// - Modify ops wrap the selection_mask primitives with undoable commits.
#include <algorithm>
#include <cmath>

#include <QImage>

#include "engine/compute/layer_mask.h"
#include "ui/app_state.h"
#include "ui/mask_finish.h"
#include "ui/selection_mask.h"

namespace pittore::ui {
namespace {

// Bilinear sample of a Grayscale8 image, clamped to the edge.
float sampleGrayBilinear(const QImage& m, double x, double y) {
    const int w = m.width(), h = m.height();
    if (w <= 0 || h <= 0) return 0.0f;
    x = std::clamp(x, 0.0, static_cast<double>(w) - 1.0);
    y = std::clamp(y, 0.0, static_cast<double>(h) - 1.0);
    const int x0 = static_cast<int>(x), y0 = static_cast<int>(y);
    const int x1 = std::min(x0 + 1, w - 1), y1 = std::min(y0 + 1, h - 1);
    const float fx = static_cast<float>(x - x0);
    const float fy = static_cast<float>(y - y0);
    const auto row = [&](int yy) { return m.constScanLine(yy); };
    const float c00 = row(y0)[x0], c10 = row(y0)[x1];
    const float c01 = row(y1)[x0], c11 = row(y1)[x1];
    return ((c00 * (1 - fx) + c10 * fx) * (1 - fy) +
            (c01 * (1 - fx) + c11 * fx) * fy) /
           255.0f;
}

// Mirrors ensureLayerMask in app_state.cpp (Pixel masks are layer-native,
// adjustment masks document-sized; locked layers refused). Returns false
// with a hint when no mask can be made. Fresh masks are filled with `fill`.
bool ensureMaskForOp(AppState* state, DocumentItem& d, LayerItem& l,
                     float fill) {
    if ((l.kind != LayerItem::Kind::Pixel &&
         l.kind != LayerItem::Kind::Adjustment) ||
        l.locked) {
        state->setStatusHint(
            AppState::tr("Masks need an unlocked pixel or adjustment layer."));
        return false;
    }
    std::uint32_t mw, mh;
    if (l.kind == LayerItem::Kind::Pixel) {
        if (!l.pixels) {
            state->setStatusHint(
                AppState::tr("The active layer has no pixels yet."));
            return false;
        }
        mw = l.pixels->width();
        mh = l.pixels->height();
    } else {
        mw = static_cast<std::uint32_t>(std::max(0, d.size.width()));
        mh = static_cast<std::uint32_t>(std::max(0, d.size.height()));
    }
    if (mw == 0 || mh == 0) return false;
    if (!l.mask) {
        auto mask = std::make_shared<pittore::Image>(mw, mh);
        mask->fill(pittore::compute::make_mask_pixel(fill));
        l.mask = std::move(mask);
        l.maskLinked = true;
        ++l.maskStamp;
    }
    if (l.maskLinked) {
        l.maskOffset = l.offset;
        l.maskScaleX = l.scaleX;
        l.maskScaleY = l.scaleY;
    }
    l.hasMask = true;
    l.maskEnabled = true;
    return true;
}

// Document placement of the mask (mirrors maskTransformFor in app_state.cpp:
// linked masks ride the layer transform).
void maskPlacementFor(const LayerItem& l, QPointF& off, double& sx,
                      double& sy) {
    if (l.maskLinked) {
        off = l.offset;
        sx = l.scaleX;
        sy = l.scaleY;
    } else {
        off = l.maskOffset;
        sx = l.maskScaleX;
        sy = l.maskScaleY;
    }
}

// Box-min erosion. NOTE: selection_mask's morphRegion erode half is
// inverted (it emits !hit — the complement of a dilation — instead of an
// all-foreground test), so Contract/Border erode here locally. Their file
// is other authors' active work; flagging separately instead of touching it.
QImage erodeBox(const QImage& mask, int r) {
    if (mask.isNull() || r <= 0) return mask;
    const int w = mask.width(), h = mask.height();
    QImage tmp(w, h, QImage::Format_Grayscale8);
    for (int y = 0; y < h; ++y) {
        const uchar* srow = mask.constScanLine(y);
        uchar* trow = tmp.scanLine(y);
        for (int x = 0; x < w; ++x) {
            const int x0 = std::max(0, x - r), x1 = std::min(w - 1, x + r);
            bool all = true;
            for (int i = x0; i <= x1 && all; ++i)
                if (srow[i] <= 127) all = false;
            trow[x] = all ? 255 : 0;
        }
    }
    QImage out(w, h, QImage::Format_Grayscale8);
    for (int y = 0; y < h; ++y) {
        uchar* orow = out.scanLine(y);
        for (int x = 0; x < w; ++x) {
            const int y0 = std::max(0, y - r), y1 = std::min(h - 1, y + r);
            bool all = true;
            for (int yy = y0; yy <= y1 && all; ++yy)
                if (tmp.constScanLine(yy)[x] <= 127) all = false;
            orow[x] = all ? 255 : 0;
        }
    }
    return out;
}

bool hasLiveSelection(AppState* state, DocumentItem& d, QImage& sel) {
    sel = selectionAsMask(d);
    if (sel.isNull() || selectionMaskBbox(sel).isNull()) {
        state->setStatusHint(
            AppState::tr("There is no selection to use."));
        return false;
    }
    return true;
}

}  // namespace

bool AppState::maskPaintSelection(bool hide) {
    DocumentItem* d = activeDocument();
    LayerItem* layer = activeLayer();
    if (!d || !layer) return false;
    QImage sel;
    if (!hasLiveSelection(this, *d, sel)) return false;
    beginUndoStep();
    layer = activeLayer();
    const bool fresh = !layer->mask;
    if (!ensureMaskForOp(this, *d, *layer, 1.0f)) {
        discardUndoStep();
        return false;
    }
    layer = activeLayer();
    if (!fresh && !copyOnWriteActiveMask()) {
        discardUndoStep();
        return false;
    }
    layer = activeLayer();
    QPointF moff;
    double msx, msy;
    maskPlacementFor(*layer, moff, msx, msy);
    const std::uint32_t mw = layer->mask->width();
    const std::uint32_t mh = layer->mask->height();
    pittore::RGBAf* px = layer->mask->data();
    for (std::uint32_t y = 0; y < mh; ++y) {
        for (std::uint32_t x = 0; x < mw; ++x) {
            const double docX = moff.x() + x * msx;
            const double docY = moff.y() + y * msy;
            const float s = (msx > 0.0 && msy > 0.0)
                                ? sampleGrayBilinear(sel, docX, docY)
                                : 1.0f;
            pittore::RGBAf& p = px[static_cast<std::size_t>(y) * mw + x];
            const float keep = hide ? (1.0f - s) : s;
            p = pittore::compute::make_mask_pixel(
                std::clamp(p.r * keep, 0.0f, 1.0f));
        }
    }
    ++layer->maskStamp;
    layer->thumbnail = QImage();
    commitUndoStep(hide ? tr("Hide Selection in Mask")
                        : tr("Reveal Selection in Mask"),
                   QStringLiteral("mask"));
    d->rebuildComposite();
    emit layersChanged();
    emit documentModified(d);
    return true;
}

bool AppState::maskRevealSelection() {
    return maskPaintSelection(false);
}

bool AppState::maskHideSelection() {
    return maskPaintSelection(true);
}

bool AppState::maskFromTransparency() {
    DocumentItem* d = activeDocument();
    if (!d || !activeLayer() ||
        activeLayer()->kind != LayerItem::Kind::Pixel ||
        !activeLayer()->pixels) {
        setStatusHint(tr("Transparency needs an active pixel layer."));
        return false;
    }
    beginUndoStep();
    if (!copyOnWriteActiveLayer()) {
        discardUndoStep();
        return false;
    }
    LayerItem* layer = activeLayer();
    const std::uint32_t nw = layer->pixels->width();
    const std::uint32_t nh = layer->pixels->height();
    if (nw == 0 || nh == 0) {
        discardUndoStep();
        return false;
    }
    // Fresh mask when none (or a size-mismatched linked one, which the
    // compositor already treats as missing); shared masks detach first.
    if (!layer->mask || layer->mask->width() != nw ||
        layer->mask->height() != nh) {
        auto mask = std::make_shared<pittore::Image>(nw, nh);
        mask->fill(pittore::compute::make_mask_pixel(1.0f));
        layer->mask = std::move(mask);
        layer->maskLinked = true;
    } else if (!copyOnWriteActiveMask()) {
        discardUndoStep();
        return false;
    }
    layer = activeLayer();
    layer->maskOffset = layer->offset;
    layer->maskScaleX = layer->scaleX;
    layer->maskScaleY = layer->scaleY;
    layer->hasMask = true;
    layer->maskEnabled = true;
    pittore::RGBAf* px = layer->pixels->data();
    pittore::RGBAf* mp = layer->mask->data();
    for (std::size_t i = 0, n = static_cast<std::size_t>(nw) * nh; i < n;
         ++i) {
        mp[i] = pittore::compute::make_mask_pixel(
            std::clamp(px[i].a, 0.0f, 1.0f));
        px[i].a = 1.0f;
    }
    ++layer->sourceStamp;
    ++layer->maskStamp;
    layer->thumbnail = QImage();
    commitUndoStep(tr("Mask From Transparency"), QStringLiteral("mask"));
    d->rebuildComposite();
    emit layersChanged();
    emit documentModified(d);
    return true;
}

bool AppState::loadMaskAsSelection() {
    DocumentItem* d = activeDocument();
    LayerItem* layer = activeLayer();
    if (!d || !layer || !layer->hasMask || !layer->mask ||
        d->size.isEmpty()) {
        setStatusHint(tr("The active layer has no mask to load."));
        return false;
    }
    // Finished coverage, so density/feather show up in the ants too.
    std::shared_ptr<pittore::Image> finished;
    const pittore::Image* m = layer->mask.get();
    if (layer->maskFeather > 0.0f || layer->maskDensity != 1.0f) {
        finished =
            finishMaskImage(*m, layer->maskDensity, layer->maskFeather);
        m = finished.get();
    }
    QPointF moff;
    double msx, msy;
    maskPlacementFor(*layer, moff, msx, msy);
    QImage sel(d->size, QImage::Format_Grayscale8);
    if (sel.isNull()) return false;
    sel.fill(0);
    for (int y = 0; y < sel.height(); ++y) {
        uchar* row = sel.scanLine(y);
        for (int x = 0; x < sel.width(); ++x) {
            float c = 1.0f;
            if (msx > 0.0 && msy > 0.0)
                c = sampleCoverageBilinear(
                    *m, (x - moff.x()) / msx, (y - moff.y()) / msy);
            row[x] = static_cast<uchar>(
                std::clamp(static_cast<int>(c * 255.0f + 0.5f), 0, 255));
        }
    }
    if (selectionMaskBbox(sel).isNull()) {
        setStatusHint(tr("The mask is empty — nothing selected."));
        return false;
    }
    replaceSelectionMask(std::move(sel), tr("Load Mask as Selection"),
                         QStringLiteral("select"));
    return true;
}

bool AppState::modifySelectionExpand(int px) {
    DocumentItem* d = activeDocument();
    if (!d) return false;
    QImage sel;
    if (!hasLiveSelection(this, *d, sel)) return false;
    replaceSelectionMask(selectionMaskGrow(sel, px), tr("Expand Selection"),
                         QStringLiteral("select"));
    return true;
}

bool AppState::modifySelectionContract(int px) {
    DocumentItem* d = activeDocument();
    if (!d) return false;
    QImage sel;
    if (!hasLiveSelection(this, *d, sel)) return false;
    replaceSelectionMask(erodeBox(sel, px), tr("Contract Selection"),
                         QStringLiteral("select"));
    return true;
}

bool AppState::modifySelectionFeather(int px) {
    DocumentItem* d = activeDocument();
    if (!d) return false;
    QImage sel;
    if (!hasLiveSelection(this, *d, sel)) return false;
    replaceSelectionMask(selectionMaskFeather(sel, px),
                         tr("Feather Selection"), QStringLiteral("select"));
    return true;
}

bool AppState::modifySelectionSmooth(int passes) {
    DocumentItem* d = activeDocument();
    if (!d) return false;
    QImage sel;
    if (!hasLiveSelection(this, *d, sel)) return false;
    replaceSelectionMask(selectionMaskSmooth(sel, passes),
                         tr("Smooth Selection"), QStringLiteral("select"));
    return true;
}

bool AppState::modifySelectionBorder(int px) {
    DocumentItem* d = activeDocument();
    if (!d) return false;
    QImage sel;
    if (!hasLiveSelection(this, *d, sel)) return false;
    // Edge band: grown minus shrunk.
    const QImage grown = selectionMaskGrow(sel, px);
    const QImage shrunk = erodeBox(sel, px);
    QImage out(sel.size(), QImage::Format_Grayscale8);
    if (out.isNull()) return false;
    for (int y = 0; y < out.height(); ++y) {
        const uchar* g = grown.constScanLine(y);
        const uchar* s = shrunk.constScanLine(y);
        uchar* o = out.scanLine(y);
        for (int x = 0; x < out.width(); ++x)
            o[x] = static_cast<uchar>((g[x] * (255 - s[x]) + 127) / 255);
    }
    replaceSelectionMask(std::move(out), tr("Border Selection"),
                         QStringLiteral("select"));
    return true;
}

}  // namespace pittore::ui
