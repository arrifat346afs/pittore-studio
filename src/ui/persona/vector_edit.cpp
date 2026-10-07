#include "ui/persona/vector_edit.h"

#include "ui/app_state.h"
#include "ui/persona/vector_raster.h"
#include "ui/persona/vector_profile.h"
#include "ui/project_manager.h"
#include "ui/svg_parts.h"

#include <QPainterPath>
#include <QTransform>
#include <QImage>
#include <QPainter>

#include <algorithm>
#include <cmath>

#include "engine/compute/layer_mask.h"

namespace pittore::ui {

int vectorEditableLayer(AppState* state) {
    DocumentItem* d = state ? state->activeDocument() : nullptr;
    if (!d) return -1;
    for (int index : state->selectedLayerIndices()) {
        if (index >= 0 && index < d->layers.size() && d->layers[index].art &&
            !d->layers[index].art->isEmpty())
            return index;
    }
    return -1;
}

void bakeArtDense(LayerItem& l, double zoom) {
    // FX and live filters own the styled fields when active; never fight them.
    if (!l.art || l.art->isEmpty() || !l.pixels) return;
    if (!l.style.empty()) return;
    if (l.hasLiveFilter && l.liveFilterEnabled) return;
    double kf = std::clamp(std::ceil(zoom - 1e-9), 1.0, 16.0);
    // Cap the dense image: footprint*k long edge stays within 2048 px. The
    // old 4096 cap let one zoomed circle bake a 4000×4000 RGBAf styled image
    // (256MB host + 259MB re-upload per wheel tick). The view draws vectors
    // from geometry at screen resolution anyway; styled only feeds the
    // doc-scale composite.
    const double* m0 = l.art->matrix;
    const QTransform placed =
        QTransform(m0[0], m0[1], m0[2], m0[3], m0[4], m0[5]) *
        QTransform().scale(l.scaleX, l.scaleY) *
        QTransform().translate(l.offset.x(), l.offset.y());
    const QPainterPath outline = artNodePath(*l.art);
    if (outline.isEmpty()) return;
    const QRectF footprint = placed.mapRect(outline.boundingRect());
    const double longEdge = std::max(footprint.width(), footprint.height());
    double k = kf;
    if (longEdge > 0.0) k = std::max(1.0, std::min(kf, std::floor(2048.0 / longEdge)));
    if (k <= 1.0) {
        // Document resolution suffices: drop any stale dense bake so the
        // pixels path (already exact) serves, freeing the memory.
        if (l.styled && l.styledResample > 1.0) {
            l.styled.reset();
            ++l.styledRev;
        }
        l.styledValid = false;
        return;
    }
    if (l.styledValid && l.styled && l.styledStamp == l.sourceStamp &&
        l.styledBaseOffset == l.offset &&
        l.styledBaseScaleX == l.scaleX && l.styledBaseScaleY == l.scaleY &&
        l.styledResample == k)
        return;  // already baked at this density
    const double outScale =
        std::max({std::hypot(placed.m11(), placed.m12()),
                  std::hypot(placed.m21(), placed.m22()), 1e-9});
    const double margin = l.art->paint.hasStroke
                              ? maxProfileWidth(l.art->paint) * outScale / 2.0 + 1.0
                              : 1.0;
    const QRect ir = footprint.adjusted(-margin, -margin, margin, margin)
                         .toAlignedRect();
    if (ir.width() <= 0 || ir.height() <= 0) return;
    pittore::vector::ArtNode dense = *l.art;
    const QTransform framed =
        placed * QTransform().translate(-ir.x(), -ir.y()) * QTransform().scale(k, k);
    dense.matrix[0] = framed.m11();
    dense.matrix[1] = framed.m12();
    dense.matrix[2] = framed.m21();
    dense.matrix[3] = framed.m22();
    dense.matrix[4] = framed.dx();
    dense.matrix[5] = framed.dy();
    QImage img;
    QPointF origin;
    if (!rasterizeArtNode(dense, &img, &origin)) return;
    auto px = straightRgba64ToImage(img);
    if (!px) return;
    l.styled = std::move(px);
    l.styledOffset = QPointF(ir.x(), ir.y()) + QPointF(origin.x() / k, origin.y() / k);
    l.styledResample = k;
    l.styledStamp = l.sourceStamp;
    l.styledBaseOffset = l.offset;
    l.styledBaseScaleX = l.scaleX;
    l.styledBaseScaleY = l.scaleY;
    l.styledValid = true;
    ++l.styledRev;
}

bool AppState::refreshVectorArt() {
    DocumentItem* d = activeDocument();
    if (!d) return false;
    // Moved set: selected rows, with group headers expanded to the pixel
    // descendants the Move tool actually translated. Post-gesture only (no
    // snapshot held here), so replacement is always safe.
    QVector<int> targets;
    for (int index : selectedLayerIndices()) {
        if (index < 0 || index >= d->layers.size()) continue;
        if (d->layers[index].kind == LayerItem::Kind::Group) {
            for (int sub : groupPixelDescendantIndices(index))
                if (!targets.contains(sub)) targets.append(sub);
        } else if (!targets.contains(index)) {
            targets.append(index);
        }
    }
    bool done = false;
    for (int index : targets) {
        // Read-only inputs first (const access; never hold LayerItem& here).
        pittore::vector::ArtNode nodeCopy;
        QPointF curOff;
        double curSx = 1.0, curSy = 1.0;
        {
            const LayerItem& src = d->layers[index];
            // Flattened rows carry member geometry instead of a single node:
            // refresh the zoom-density bake for the new placement.
            if ((!src.art || src.art->isEmpty()) && !src.flatArt.empty() &&
                src.pixels) {
                LayerItem& l = d->layers[index];
                const double before = l.styledResample;
                const bool had = l.styledValid && l.styled;
                rebakeFlatRow(l, d->zoom);
                const bool now = l.styledValid && l.styled &&
                                 l.styledResample > 1.0;
                if (now != had || (now && l.styledResample != before))
                    done = true;
                continue;
            }
            // Sealed rows carry the whole subtree: same refresh, exact painter.
            if ((!src.art || src.art->isEmpty()) && src.sharedNode &&
                src.sharedRes && src.pixels) {
                LayerItem& l = d->layers[index];
                const double before = l.styledResample;
                const bool had = l.styledValid && l.styled;
                rebakeSharedRow(l, d->zoom);
                const bool now = l.styledValid && l.styled &&
                                 l.styledResample > 1.0;
                if (now != had || (now && l.styledResample != before))
                    done = true;
                continue;
            }
            if (!src.art || src.art->isEmpty() || !src.pixels) continue;
            nodeCopy = *src.art;
            curOff = src.offset;
            curSx = src.scaleX;
            curSy = src.scaleY;
        }
        // Node→doc through the LIVE placement. Qt transforms compose
        // left-first: (M * S * T).map applies matrix, then scale, then
        // offset. Then a trim pre-pass mirroring the importer.
        const double* m = nodeCopy.matrix;
        const QTransform placed =
            QTransform(m[0], m[1], m[2], m[3], m[4], m[5]) *
            QTransform().scale(curSx, curSy) *
            QTransform().translate(curOff.x(), curOff.y());
        const QPainterPath outline = artNodePath(nodeCopy);
        if (outline.isEmpty()) continue;
        const double outScale =
            std::max({std::hypot(placed.m11(), placed.m12()),
                      std::hypot(placed.m21(), placed.m22()), 1e-9});
        const double margin = nodeCopy.paint.hasStroke
                                  ? nodeCopy.paint.strokeWidth * outScale / 2.0 +
                                        1.0
                                  : 1.0;
        const QRect trim = placed.mapRect(outline.boundingRect())
                               .adjusted(-margin, -margin, margin, margin)
                               .toAlignedRect();
        if (trim.width() <= 0 || trim.height() <= 0 || trim.width() > 16384 ||
            trim.height() > 16384)
            continue;
        const QTransform framed =
            placed * QTransform().translate(-trim.x(), -trim.y());
        nodeCopy.matrix[0] = framed.m11();
        nodeCopy.matrix[1] = framed.m12();
        nodeCopy.matrix[2] = framed.m21();
        nodeCopy.matrix[3] = framed.m22();
        nodeCopy.matrix[4] = framed.dx();
        nodeCopy.matrix[5] = framed.dy();

        auto staged =
            std::make_shared<pittore::vector::ArtNode>(std::move(nodeCopy));
        QImage img;
        QPointF trimCheck;
        if (!rasterizeArtNode(*staged, &img, &trimCheck)) continue;
        auto pixels = straightRgba64ToImage(img);
        if (!pixels) continue;

        LayerItem& l = d->layers[index];  // fresh access: detach before write
        l.art = std::move(staged);
        l.pixels = std::move(pixels);
        l.offset = QPointF(trim.x(), trim.y());
        l.scaleX = 1.0;
        l.scaleY = 1.0;
        ++l.sourceStamp;
        l.thumbnail = QImage();
        l.styledValid = false;
        // Linked mask follows the new grid (mirror of
        // syncLinkedMaskPlacement: resample on size change, re-anchor).
        if (l.mask && l.maskLinked) {
            if (l.mask->width() != l.pixels->width() ||
                l.mask->height() != l.pixels->height()) {
                const std::uint32_t nw = l.pixels->width();
                const std::uint32_t nh = l.pixels->height();
                const std::uint32_t ow = l.mask->width();
                const std::uint32_t oh = l.mask->height();
                auto next = std::make_shared<pittore::Image>(nw, nh);
                for (std::uint32_t y = 0; y < nh; ++y) {
                    const std::uint32_t sy = std::min(
                        oh - 1, static_cast<std::uint32_t>(
                                    (static_cast<std::uint64_t>(y) * oh) / nh));
                    for (std::uint32_t x = 0; x < nw; ++x) {
                        const std::uint32_t sx = std::min(
                            ow - 1, static_cast<std::uint32_t>(
                                        (static_cast<std::uint64_t>(x) * ow) /
                                        nw));
                        const float c =
                            std::clamp(l.mask->at(sx, sy).r, 0.0f, 1.0f);
                        next->at(x, y) = pittore::compute::make_mask_pixel(c);
                    }
                }
                l.mask = std::move(next);
                ++l.maskStamp;
            }
            l.maskOffset = l.offset;
            l.maskScaleX = 1.0;
            l.maskScaleY = 1.0;
        }
        bakeArtDense(l, d->zoom);
        done = true;
    }
    if (!done) return false;
    d->dirty = true;
    d->rebuildComposite();
    emit layersChanged();  // thumbnails were invalidated above
    emit documentModified(d);
    return true;
}

void rebakeFlatRow(LayerItem& l, double zoom) {
    // FX and live filters own the styled fields when active; never fight them.
    if (l.flatArt.empty() || !l.pixels) return;
    if (!l.style.empty()) return;
    if (l.hasLiveFilter && l.liveFilterEnabled) return;
    auto dropDense = [&l] {
        if (l.styled && l.styledResample > 1.0) {
            l.styled.reset();
            ++l.styledRev;
        }
        l.styledValid = false;
    };
    // Painted rows keep their pixels: re-baking from geometry would revert
    // brushwork, so the dense bake only ever covers pristine rows.
    if (l.sourceStamp != l.flatStamp) {
        dropDense();
        return;
    }
    const double kf = std::clamp(std::ceil(zoom - 1e-9), 1.0, 16.0);
    if (kf <= 1.0) {
        // Document resolution suffices: no geometry walk needed, just drop
        // any stale dense bake so the pixels path serves.
        dropDense();
        return;
    }
    const QTransform place =
        QTransform().scale(l.scaleX, l.scaleY) *
        QTransform().translate(l.offset.x(), l.offset.y());
    // Union footprint in document space through the live placement.
    QRectF box;
    bool hasBox = false;
    double outScale = 1e-9;
    double peakSw = 0.0;
    for (const auto& a : l.flatArt) {
        if (!a || a->isEmpty()) continue;
        const double* m = a->matrix;
        const QTransform placed =
            QTransform(m[0], m[1], m[2], m[3], m[4], m[5]) * place;
        const QPainterPath outline = artNodePath(*a);
        if (outline.isEmpty()) continue;
        const double s =
            std::max({std::hypot(placed.m11(), placed.m12()),
                      std::hypot(placed.m21(), placed.m22()), 1e-9});
        outScale = std::max(outScale, s);
        if (a->paint.hasStroke)
            peakSw = std::max(peakSw, maxProfileWidth(a->paint));
        const QRectF b = placed.mapRect(outline.boundingRect());
        box = hasBox ? box.united(b) : b;
        hasBox = true;
    }
    if (!hasBox) return;
    const double margin = peakSw > 0.0 ? peakSw * outScale / 2.0 + 1.0 : 1.0;
    const QRect ir = box.adjusted(-margin, -margin, margin, margin)
                         .toAlignedRect();
    if (ir.width() <= 0 || ir.height() <= 0 || ir.width() > 16384 ||
        ir.height() > 16384)
        return;
    // Cap the dense image: footprint*k long edge stays within 2048 px, the
    // same bound as the single-art bake.
    const double longEdge =
        std::max<double>(ir.width(), ir.height());
    double k = kf;
    if (longEdge > 0.0)
        k = std::max(1.0, std::min(kf, std::floor(2048.0 / longEdge)));
    if (k <= 1.0) {
        dropDense();
        return;
    }
    if (l.styledValid && l.styled && l.styledStamp == l.sourceStamp &&
        l.styledBaseOffset == l.offset &&
        l.styledBaseScaleX == l.scaleX && l.styledBaseScaleY == l.scaleY &&
        l.styledResample == k)
        return;  // already baked at this density
    const int w = std::max(1, static_cast<int>(std::ceil(ir.width() * k)));
    const int h = std::max(1, static_cast<int>(std::ceil(ir.height() * k)));
    if (w > 16384 || h > 16384) return;
    QImage out(QSize(w, h), QImage::Format_ARGB32_Premultiplied);
    out.fill(Qt::transparent);
    QPainter rp(&out);
    for (const auto& a : l.flatArt) {
        if (!a || a->isEmpty()) continue;
        pittore::vector::ArtNode member = *a;
        const double* m = a->matrix;
        const QTransform framed =
            QTransform(m[0], m[1], m[2], m[3], m[4], m[5]) * place *
            QTransform().translate(-ir.x(), -ir.y()) * QTransform().scale(k, k);
        member.matrix[0] = framed.m11();
        member.matrix[1] = framed.m12();
        member.matrix[2] = framed.m21();
        member.matrix[3] = framed.m22();
        member.matrix[4] = framed.dx();
        member.matrix[5] = framed.dy();
        QImage img;
        QPointF origin;
        if (!rasterizeArtNode(member, &img, &origin)) continue;
        rp.drawImage(origin, img);
    }
    rp.end();
    auto px = straightRgba64ToImage(out);
    if (!px) return;
    l.styled = std::move(px);
    l.styledOffset = QPointF(ir.x(), ir.y());
    l.styledResample = k;
    l.styledStamp = l.sourceStamp;
    l.styledBaseOffset = l.offset;
    l.styledBaseScaleX = l.scaleX;
    l.styledBaseScaleY = l.scaleY;
    l.styledValid = true;
    ++l.styledRev;
}

void rebakeSharedRow(LayerItem& l, double zoom, const QRectF& visible) {
    // FX and live filters own the styled fields when active; never fight them.
    if (!l.sharedNode || !l.sharedRes || !l.pixels) return;
    if (!l.style.empty()) return;
    if (l.hasLiveFilter && l.liveFilterEnabled) return;
    auto dropDense = [&l] {
        if (l.styled && l.styledResample > 1.0) {
            l.styled.reset();
            ++l.styledRev;
        }
        l.styledValid = false;
        l.styledView = QRectF();
    };
    // Same pristine-pixels pin as the flattened-row rebake: paint owns the
    // pixels once edited, so geometry must never revert brushwork.
    if (l.sourceStamp != l.flatStamp) {
        dropDense();
        return;
    }
    const QTransform place =
        QTransform().scale(l.scaleX, l.scaleY) *
        QTransform().translate(l.offset.x(), l.offset.y());
    QImage img;
    QPointF origin;
    double k = 1.0;
    QRectF baked;
    if (!sharedRowBake(*l.sharedNode, *l.sharedRes, place, zoom, visible, &img,
                       &origin, &k, &baked) ||
        img.isNull()) {
        dropDense();
        return;
    }
    if (l.styledValid && l.styled && l.styledStamp == l.sourceStamp &&
        l.styledBaseOffset == l.offset &&
        l.styledBaseScaleX == l.scaleX && l.styledBaseScaleY == l.scaleY &&
        l.styledResample == k && l.styledView == baked)
        return;  // already baked at this density for this view
    auto px = straightRgba64ToImage(img);
    if (!px) {
        dropDense();
        return;
    }
    l.styled = std::move(px);
    l.styledOffset = origin;
    l.styledResample = k;
    l.styledView = baked;
    l.styledStamp = l.sourceStamp;
    l.styledBaseOffset = l.offset;
    l.styledBaseScaleX = l.scaleX;
    l.styledBaseScaleY = l.scaleY;
    l.styledValid = true;
    ++l.styledRev;
}

bool AppState::rezoomVectorArt() {
    DocumentItem* d = activeDocument();
    if (!d) return false;
    bool done = false;
    for (int i = 0; i < d->layers.size(); ++i) {
        LayerItem& l = d->layers[i];  // bake only reads + writes styled fields
        const double before = l.styledResample;
        const bool had = l.styledValid && l.styled;
        bakeArtDense(l, d->zoom);
        rebakeFlatRow(l, d->zoom);
        rebakeSharedRow(l, d->zoom);
        const bool now =
            l.styledValid && l.styled && l.styledResample > 1.0;
        if (now != had || (now && l.styledResample != before)) done = true;
    }
    if (!done) return false;
    d->dirty = true;
    d->rebuildComposite();
    return true;
}

bool AppState::applyVectorPaint(int layerIndex,
                                const pittore::vector::ArtPaint& paint,
                                double opacity, const QString& undoName) {
    DocumentItem* d = activeDocument();
    if (!d || layerIndex < 0 || layerIndex >= d->layers.size()) return false;
    const LayerItem& src = d->layers[layerIndex];
    if (!src.art || src.art->isEmpty() || !src.pixels) {
        setStatusHint(tr("Select a vector shape layer to edit its paint."));
        return false;
    }
    auto node = std::make_shared<pittore::vector::ArtNode>(*src.art);
    node->paint = paint;
    if (opacity >= 0.0) node->opacity = qBound(0.01, opacity, 1.0);
    return applyVectorNode(layerIndex, *node, undoName);
}

bool AppState::resetStrokeProfile() {
    const int index = vectorEditableLayer(this);
    DocumentItem* d = activeDocument();
    const LayerItem* l =
        (d && index >= 0 && index < d->layers.size()) ? &d->layers[index]
                                                      : nullptr;
    if (!l || !l->art || l->art->isEmpty() || !l->art->paint.hasStroke) {
        setStatusHint(tr("Select a stroked vector shape first."));
        return false;
    }
    if (!l->art->paint.hasProfile || l->art->paint.profile.empty()) {
        setStatusHint(tr("The stroke is already uniform."));
        return false;
    }
    auto paint = l->art->paint;
    paint.hasProfile = false;
    paint.profile.clear();
    return applyVectorPaint(index, paint, -1.0, tr("Reset Profile"));
}

bool AppState::applyVectorNode(int layerIndex,
                               const pittore::vector::ArtNode& node,
                               const QString& undoName) {
    DocumentItem* d = activeDocument();
    if (!d || layerIndex < 0 || layerIndex >= d->layers.size()) return false;
    {
        // Const access only: read the source geometry without holding anything.
        const LayerItem& src = d->layers[layerIndex];
        // Retained geometry only: photos and paint have no ArtNode to re-shade,
        // and group rows have no pixels of their own.
        if (!src.art || src.art->isEmpty() || !src.pixels) {
            setStatusHint(tr("Select a vector shape layer to edit."));
            return false;
        }
    }
    // Stage the replacement fully BEFORE the snapshot (never hold a LayerItem&
    // across beginUndoAction: see the note at the fresh access below).
    auto staged = std::make_shared<pittore::vector::ArtNode>(node);
    QImage img;
    QPointF sourceOrigin;
    if (!rasterizeArtNode(*staged, &img, &sourceOrigin)) {
        setStatusHint(tr("Could not re-render the vector shape."));
        return false;
    }
    auto pixels = straightRgba64ToImage(img);
    if (!pixels) {
        setStatusHint(tr("Could not re-render the vector shape."));
        return false;
    }
    const QRectF oldBounds = layerBounds(*d, d->layers[layerIndex]);

    d->beginUndoAction();
    // Fresh non-const access AFTER the snapshot: QVector detaches here, so the
    // writes below land in live-only storage. Holding a LayerItem& across
    // beginUndoAction would write through the snapshot's shared store and
    // corrupt undo (Qt implicit-sharing trap — no detachment on stale refs).
    LayerItem& l = d->layers[layerIndex];
    l.art = std::move(staged);
    l.pixels = std::move(pixels);
    // The new image is trimmed to a new source-space origin; shift the
    // document offset so the artwork does not move (source frame is fixed, so
    // this holds for moved/scaled layers too).
    l.offset = QPointF(l.offset.x() + l.scaleX * sourceOrigin.x(),
                       l.offset.y() + l.scaleY * sourceOrigin.y());
    ++l.sourceStamp;
    l.thumbnail = QImage();
    l.styledValid = false;
    bakeArtDense(l, d->zoom);
    const QRectF newBounds = layerBounds(*d, l);
    const QRect region = (oldBounds | newBounds)
                             .toAlignedRect()
                             .intersected(QRectF(QPointF(0, 0), QSizeF(d->size)).toRect());
    d->dirty = true;
    if (region.isEmpty())
        d->rebuildComposite();
    else
        d->recompositeRegion(region);
    d->commitUndoAction(undoName.isEmpty() ? tr("Vector Paint") : undoName,
                        QStringLiteral("stroke"));
    emit historyChanged();
    emit layersChanged();
    emit documentModified(d);
    return true;
}

}  // namespace pittore::ui
