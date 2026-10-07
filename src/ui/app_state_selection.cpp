// Marquee/elliptical/arbitrary-shape selection: setting, masking, combining
// and inverting. Split out of app_state.cpp.

#include "ui/app_state.h"

#include <algorithm>
#include <utility>

namespace pittore::ui {

void AppState::setSelection(const QRectF& rect, bool ellipse) {
    DocumentItem* d = activeDocument();
    if (!d) return;
    d->selection = rect;
    d->selectionIsEllipse = ellipse;
    // A rect/ellipse (marquee) selection replaces any arbitrary-shape mask.
    d->selectionIsMask = false;
    d->selectionMask = QImage();
    emit selectionChanged();
    if (!rect.isEmpty() && context_ != TaskContext::Crop && context_ != TaskContext::Text)
        setTaskContext(TaskContext::Selection);
    else if (rect.isEmpty() && context_ == TaskContext::Selection)
        setTaskContext(TaskContext::None);
}

// Bounding box of a grayscale mask at the standard 50% (127) threshold, in
// pixel coordinates; empty (maxX < 0) when nothing is selected.
QRect maskBbox(const QImage& mask) {
    if (mask.isNull()) return QRect();
    const int w = mask.width(), h = mask.height();
    int minX = w, minY = h, maxX = -1, maxY = -1;
    for (int y = 0; y < h; ++y) {
        const uchar* row = mask.constScanLine(y);  // honours padded stride
        for (int x = 0; x < w; ++x) {
            if (row[x] > 127) {
                if (x < minX) minX = x;
                if (x > maxX) maxX = x;
                if (y < minY) minY = y;
                if (y > maxY) maxY = y;
            }
        }
    }
    return maxX < 0 ? QRect() : QRect(QPoint(minX, minY), QPoint(maxX, maxY));
}

void AppState::setSelectionMask(QImage mask) {
    DocumentItem* d = activeDocument();
    if (!d) return;
    if (mask.isNull() || mask.width() != d->size.width() ||
        mask.height() != d->size.height()) {
        clearSelection();
        return;
    }
    if (mask.format() != QImage::Format_Grayscale8)
        mask = mask.convertToFormat(QImage::Format_Grayscale8);
    const QRect bbox = maskBbox(mask);
    if (bbox.isEmpty()) {
        clearSelection();
        return;
    }
    d->selectionMask = std::move(mask);
    d->selectionIsMask = true;
    d->selectionIsEllipse = false;
    // Pixel (x,y) of the mask is document pixel (x,y): the bbox is the
    // selection rect bbox-consumers rely on.
    d->selection = QRectF(bbox);
    ++d->selectionStamp;
    emit selectionChanged();
    if (context_ != TaskContext::Crop && context_ != TaskContext::Text)
        setTaskContext(TaskContext::Selection);
}

void AppState::clearSelection() { setSelection(QRectF(), false); }

void AppState::replaceSelectionMask(QImage mask, const QString& undoName,
                                    const QString& iconKey) {
    DocumentItem* d = activeDocument();
    if (!d) return;
    d->beginUndoAction();
    setSelectionMask(std::move(mask));
    d->commitUndoAction(undoName, iconKey);
    emit historyChanged();
    emit documentModified(d);
}

bool AppState::selectFromLayerAlpha(int layerIndex) {
    DocumentItem* d = activeDocument();
    if (!d || d->size.isEmpty() || layerIndex < 0 ||
        layerIndex >= d->layers.size())
        return false;
    const LayerItem& l = d->layers[layerIndex];

    // Sources whose alpha makes up the selection: the pixel layer itself, or
    // the visible pixel descendants of a group, bottom→top (the composite
    // order) so Ctrl+click on a group row outlines the whole assembled logo.
    QVector<int> sources;
    if (l.kind == LayerItem::Kind::Pixel) {
        sources.append(layerIndex);
    } else if (l.kind == LayerItem::Kind::Group) {
        sources = groupPixelDescendantIndices(layerIndex);
        if (sources.isEmpty()) return false;
        std::sort(sources.begin(), sources.end(),
                  [](int a, int b) { return a > b; });
    } else {
        return false;   // text/adjustment rows have no alpha to extract
    }

    const int dw = d->size.width(), dh = d->size.height();
    QImage mask(dw, dh, QImage::Format_Grayscale8);
    mask.fill(0);
    if (mask.isNull()) return false;

    // Each source's alpha is placed into document space with the same
    // nearest-neighbour lookup the AI/wand rasterisers use — but treating
    // out-of-footprint pixels as transparent (0), never clamping to the source
    // edge, so the mask is the layer's exact footprint. Iterating only each
    // source's own document footprint keeps the cost O(source pixels), not
    // O(doc × layers). For a single pixel layer this is a 1:1 alpha copy.
    for (int idx : sources) {
        const LayerItem& c = d->layers[idx];
        if (!c.visible || !c.pixels) continue;
        const int pw = static_cast<int>(c.pixels->width());
        const int ph = static_cast<int>(c.pixels->height());
        if (pw < 1 || ph < 1) continue;
        const double sx = std::max(c.scaleX, 1e-6);
        const double sy = std::max(c.scaleY, 1e-6);
        QRect foot = QRectF(c.offset, QSizeF(pw * sx, ph * sy))
                         .toAlignedRect()
                         .intersected(QRect(0, 0, dw, dh));
        if (foot.isEmpty()) continue;
        const pittore::RGBAf* src = c.pixels->data();
        for (int y = foot.top(); y <= foot.bottom(); ++y) {
            uchar* row = mask.scanLine(y);
            for (int x = foot.left(); x <= foot.right(); ++x) {
                const int lx = std::clamp(
                    static_cast<int>(std::lround((x + 0.5 - c.offset.x()) /
                                                 sx - 0.5)),
                    0, pw - 1);
                const int ly = std::clamp(
                    static_cast<int>(std::lround((y + 0.5 - c.offset.y()) /
                                                 sy - 0.5)),
                    0, ph - 1);
                const float a = src[std::size_t(ly) * pw + lx].a;
                // Source-over union for groups; single layers just copy their
                // alpha (first paint over the cleared mask).
                const float cur = row[x] / 255.0f;
                row[x] = static_cast<uchar>(std::clamp(
                    static_cast<int>((a + cur * (1.0f - a)) * 255.0f + 0.5f),
                    0, 255));
            }
        }
    }

    if (maskBbox(mask).isEmpty()) {
        setStatusHint(tr("Select Transparency: layer is fully transparent."));
        return false;
    }
    replaceSelectionMask(std::move(mask), tr("Select Transparency"),
                         QStringLiteral("select"));
    setStatusHint(tr("Selection loaded from \"%1\".")
                      .arg(d->layers[layerIndex].name));
    return true;
}

// The live selection as a document-resolution coverage mask: the stored mask
// when one exists, otherwise the rect/ellipse rasterised. Always returns a
// document-sized Grayscale8 image (all-zero when nothing is selected).
QImage selectionAsMask(const DocumentItem& d) {
    QImage m(d.size, QImage::Format_Grayscale8);
    if (m.isNull()) return m;
    m.fill(0);
    if (d.selection.isEmpty()) return m;
    if (d.selectionIsMask && !d.selectionMask.isNull()) {
        if (d.selectionMask.size() == m.size()) return d.selectionMask;
        return d.selectionMask.scaled(m.size(), Qt::IgnoreAspectRatio,
                                      Qt::SmoothTransformation)
            .convertToFormat(QImage::Format_Grayscale8);
    }
    const QRectF sel = d.selection.normalized();
    const bool ellipse = d.selectionIsEllipse;
    const int w = m.width(), h = m.height();
    const double cx = sel.center().x(), cy = sel.center().y();
    const double rx = std::max(sel.width() * 0.5, 1e-9);
    const double ry = std::max(sel.height() * 0.5, 1e-9);
    for (int y = 0; y < h; ++y) {
        uchar* row = m.scanLine(y);
        for (int x = 0; x < w; ++x) {
            bool inside;
            if (ellipse) {
                const double nx = (x + 0.5 - cx) / rx;
                const double ny = (y + 0.5 - cy) / ry;
                inside = nx * nx + ny * ny <= 1.0;
            } else {
                inside = sel.contains(QPointF(x + 0.5, y + 0.5));
            }
            if (inside) row[x] = 255;
        }
    }
    return m;
}

void AppState::invertSelection() {
    DocumentItem* d = activeDocument();
    if (!d || d->selection.isEmpty() || d->size.isEmpty()) return;
    const QImage base = selectionAsMask(*d);
    if (base.isNull()) return;
    QImage inv(base.size(), QImage::Format_Grayscale8);
    for (int y = 0; y < base.height(); ++y) {
        const uchar* s = base.constScanLine(y);
        uchar* o = inv.scanLine(y);
        for (int x = 0; x < base.width(); ++x) o[x] = uchar(255 - s[x]);
    }
    replaceSelectionMask(std::move(inv), tr("Inverse"),
                         QStringLiteral("marquee-rect"));
}

void AppState::combineSelection(QImage incoming, int mode,
                                const QString& undoName,
                                const QString& iconKey) {
    DocumentItem* d = activeDocument();
    if (!d || d->size.isEmpty()) return;
    if (incoming.isNull() || incoming.size() != d->size) {
        if (mode == 0) replaceSelectionMask(QImage(), undoName, iconKey);
        return;
    }
    if (incoming.format() != QImage::Format_Grayscale8)
        incoming = incoming.convertToFormat(QImage::Format_Grayscale8);
    if (mode == 0) {
        replaceSelectionMask(std::move(incoming), undoName, iconKey);
        return;
    }
    const QImage base = selectionAsMask(*d);
    QImage out(base.size(), QImage::Format_Grayscale8);
    for (int y = 0; y < base.height(); ++y) {
        const uchar* a = base.constScanLine(y);
        const uchar* b = incoming.constScanLine(y);
        uchar* o = out.scanLine(y);
        for (int x = 0; x < base.width(); ++x) {
            const int av = a[x], bv = b[x];
            int v;
            switch (mode) {
                case 1: v = std::max(av, bv); break;          // add / union
                case 2: v = (av * (255 - bv)) / 255; break;   // subtract
                case 3: v = std::min(av, bv); break;          // intersect
                default: v = bv; break;
            }
            o[x] = uchar(std::clamp(v, 0, 255));
        }
    }
    replaceSelectionMask(std::move(out), undoName, iconKey);
}

}  // namespace pittore::ui
