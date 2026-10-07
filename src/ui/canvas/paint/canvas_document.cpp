#include "ui/canvas_view.h"

#include "ui/proof_preview.h"

#include <QApplication>
#include <QContextMenuEvent>
#include <QFile>
#include <QFileInfo>
#include <QImageReader>
#include <QInputDialog>
#include <QKeyEvent>
#include <QMap>
#include <QMenu>
#include <QMimeData>
#include <QMouseEvent>
#include <QPainter>
#include <QPixmap>
#include <QPainterPath>
#include <QScrollBar>
#include <QSet>
#include <QDateTime>
#include <QTimer>
#include <QWheelEvent>
#include <QtMath>

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <unordered_map>
#include <utility>
#include <vector>

#include "ui/ai_models.h"
#include "ui/contextual_task_bar.h"
#include "ui/icons.h"
#include "ui/persona/vector_raster.h"
#include "ui/persona/vector_profile.h"
#include "ui/persona/vector_view.h"
#include "ui/selection_mask.h"
#include "ui/svg_parts.h"
#include "engine/ai/bg_remove.h"
#include "engine/compute/paint.h"
#include "engine/compute/warp.h"
#include "engine/core/log.h"

#include "ui/canvas/shared/canvas_helpers.h"

namespace pittore::ui {
namespace {

// RAII for one segmented paint: park visibility, restore it afterwards, then
// rebuild once so staging buffers and every composite reader see the whole
// document again (mirrors SoloGuard in window_helpers, but restores without
// assuming which layers were soloed). The rebuild is skipped when no pixel
// run painted (all-vector documents draw straight from geometry): the
// composite is zoom-independent, so rebuilding it per zoom frame was pure
// overhead behind the stutter.
struct SegmentGuard {
    DocumentItem* d;
    QVector<char> visible;
    bool painted = false;
    explicit SegmentGuard(DocumentItem* doc) : d(doc) {
        visible.reserve(d->layers.size());
        for (const LayerItem& l : d->layers) visible.push_back(l.visible);
    }
    ~SegmentGuard() {
        for (int i = 0; i < d->layers.size() && i < visible.size(); ++i)
            d->layers[i].visible = static_cast<bool>(visible[i]);
        if (painted) d->rebuildComposite();
    }
};

// Blit the live composite exactly like the legacy path (full or damaged
// sub-rect), so segmented and legacy paints sample identically.
void blitComposite(QPainter& p, const QTransform& t, DocumentItem* d,
                   const QRectF& docRect, const QRect& viewDirty) {
    if (viewDirty.isEmpty()) {
        p.drawImage(docRect, d->composite);
        return;
    }
    // Sample only the damaged source stretch so a region edit paints the
    // sub-rect, not a full 2000×2000 texture transform. Snap the
    // inverse-mapped rect to whole document pixels: at fractional zoom
    // an unaligned sub-rect lets the smoothing grid wobble the painted
    // pixels between flushes (the "pixels moving" brush artifact).
    const QRectF docDirtyF =
        t.inverted().mapRect(QRectF(viewDirty)).intersected(docRect);
    const QRect docDirty =
        docDirtyF.isEmpty()
            ? QRect()
            : docDirtyF.toAlignedRect().intersected(d->composite.rect());
    if (!docDirty.isEmpty())
        p.drawImage(QRectF(docDirty), d->composite, docDirty);
}

// Draw one art layer straight from geometry at view resolution. The painter
// already carries the doc→view transform; prepend the node→doc placement
// (established order: matrix, then scale, then offset).
void paintVectorLayer(QPainter& p, const LayerItem& l) {
    const pittore::vector::ArtNode& node = *l.art;
    const double* m = node.matrix;
    const QTransform nodeToDoc =
        QTransform(m[0], m[1], m[2], m[3], m[4], m[5]) *
        QTransform().scale(l.scaleX, l.scaleY) *
        QTransform().translate(l.offset.x(), l.offset.y());
    QPainterPath path = artNodePath(node);
    if (!node.evenOdd) path.setFillRule(Qt::WindingFill);
    p.save();
    p.setTransform(nodeToDoc * p.transform(), false);
    p.setOpacity(std::clamp(node.opacity, 0.0, 1.0));
    const pittore::vector::ArtPaint& paint = node.paint;
    if (paint.hasFill) {
        p.setPen(Qt::NoPen);
        p.setBrush(artFillBrush(node));
        p.drawPath(path);
    }
    if (paint.hasStroke) {
        // Variable-width strokes expand to filled outlines, like the
        // rasterizer; uniform strokes keep the fast pen path.
        const QPainterPath expanded = expandStrokePath(node);
        if (!expanded.isEmpty()) {
            p.setPen(Qt::NoPen);
            p.setBrush(QColor(paint.stroke[0], paint.stroke[1],
                              paint.stroke[2], paint.stroke[3]));
            p.drawPath(expanded);
        } else {
            QPen pen(QColor(paint.stroke[0], paint.stroke[1], paint.stroke[2],
                            paint.stroke[3]),
                     std::max(0.01, paint.strokeWidth));
            pen.setCosmetic(false);
            pen.setCapStyle(artCapStyle(paint.cap));
            pen.setJoinStyle(artJoinStyle(paint.join));
            applyDashToPen(pen, paint);
            p.setPen(pen);
            p.setBrush(Qt::NoBrush);
            p.drawPath(path);
        }
    }
    p.restore();
}

}  // namespace

// Segmented paint: pixel runs composite solo (in order), simple vector runs
// draw from geometry at view resolution. Returns false when no layer
// qualifies, and the caller falls back to the single flattened blit — photo
// documents paint exactly one composite, as before.
bool CanvasView::paintSegmented(QPainter& p, const QTransform& t,
                                const QRectF& docRect, const QRect& viewDirty) {
    DocumentItem* d = doc();
    if (!d) return false;
    const QVector<int> drawable = vectorViewOrder(*d);
    if (drawable.isEmpty()) return false;
    QSet<int> drawableSet(drawable.begin(), drawable.end());

    // One linear visibility sweep shared by the gate, the walk and the
    // draws below. Per-index effectivelyVisible() scans back O(n) rows per
    // call, turning every sweep here quadratic on multi-thousand-layer docs.
    QVector<char> effVis;
    QVector<int> effParent;
    d->effectiveVisibility(effVis, effParent);

    // Fast path: the whole document blends Normal with no adjustments, clips,
    // masks, tone groups or layer styles, every drawable sits above every
    // pixel layer, and every drawable is fully opaque. Then source-over
    // associativity makes per-run solo composites redundant: the edit-time
    // full composite already equals them, and opaque direct draws over their
    // own raster change nothing. Zero rebuilds, zero visibility churn —
    // steady frames are one blit plus the direct draws (Brazil: ~5ms instead
    // of ~450ms). Anything exotic falls through to the solo path below.
    {
        bool soloNeeded = false;
        int maxDraw = -1, minPix = d->layers.size();
        for (int k = 0; k < d->layers.size() && !soloNeeded; ++k) {
            const LayerItem& l = d->layers[k];
            if (!effVis[k]) continue;
            if (l.kind == LayerItem::Kind::Group) {
                if (l.toneBlendGroup) soloNeeded = true;
                continue;
            }
            if (l.blendMode != QLatin1String("Normal") ||
                l.kind == LayerItem::Kind::Adjustment || l.clipped || l.hasMask ||
                l.toneBlendGroup || !l.style.empty()) {
                soloNeeded = true;
                break;
            }
            if (drawableSet.contains(k)) {
                maxDraw = std::max(maxDraw, k);
                if (l.art && !l.art->isEmpty()) {
                    const auto& pt = l.art->paint;
                    if (l.art->opacity < 1.0) soloNeeded = true;
                    if (pt.hasFill && pt.fill[3] != 255) soloNeeded = true;
                    if (pt.hasGradient) {
                        for (const auto& s : pt.gradient.stops) {
                            if (s.rgba[3] != 255) soloNeeded = true;
                        }
                    }
                    if (!pt.patternId.empty()) soloNeeded = true;
                    if (pt.hasStroke && pt.stroke[3] != 255) soloNeeded = true;
                    if (!pt.markerStart.empty() || !pt.markerMid.empty() ||
                        !pt.markerEnd.empty())
                        soloNeeded = true;
                    if (!pt.clipId.empty() || !pt.maskId.empty()) soloNeeded = true;
                    if (pt.hasMesh || pt.hasFilter) soloNeeded = true;
                }
            } else if (l.pixels) {
                minPix = std::min(minPix, k);
            }
        }
        if (!soloNeeded && maxDraw >= 0 && maxDraw < minPix) {
            blitComposite(p, t, d, docRect, viewDirty);
            for (int k = d->layers.size() - 1; k >= 0; --k) {
                if (drawableSet.contains(k) && effVis[k])
                    paintVectorLayer(p, d->layers[k]);
            }
            return true;
        }
    }

    SegmentGuard guard(d);
    // Paint order is bottom-to-top: panel index size-1 down to 0.
    int i = d->layers.size() - 1;
    auto paintRun = [&](int from, int to) {
        guard.painted = true;
        // Composite layers [from, to] (panel indices, from <= to) solo, then
        // restore visibility AT ONCE: later walk steps evaluate visibility
        // flags, and must see user state, not a previous run's mask.
        for (int k = 0; k < d->layers.size(); ++k)
            d->layers[k].visible =
                guard.visible[k] && k >= from && k <= to;
        d->rebuildComposite();
        blitComposite(p, t, d, docRect, viewDirty);
        for (int k = 0; k < d->layers.size() && k < guard.visible.size(); ++k)
            d->layers[k].visible = static_cast<bool>(guard.visible[k]);
    };
    while (i >= 0) {
        if (drawableSet.contains(i) && effVis[i]) {
            paintVectorLayer(p, d->layers[i]);
            --i;
            continue;
        }
        // Pixel run: extend while layers are neither drawable nor drawable...
        int runTop = i;
        while (i >= 0 &&
               (!drawableSet.contains(i) || !effVis[i]))
            --i;
        // ...but skip fully-hidden stretches without a (wasted) rebuild.
        bool anyVisible = false;
        for (int k = runTop; k > i; --k) {
            if (guard.visible[k] && effVis[k]) {
                anyVisible = true;
                break;
            }
        }
        if (anyVisible) paintRun(i + 1, runTop);
    }
    return true;
}


// ---------------------------------------------------------------------------
// Painting
// ---------------------------------------------------------------------------
void CanvasView::paintEvent(QPaintEvent* e) {
    const auto t0 = std::chrono::steady_clock::now();
    QPainter p(viewport());
    p.setRenderHint(QPainter::Antialiasing, true);
    // Smooth in both directions: zoomed-out minification AND zoomed-in
    // magnification. Nearest on zoom-in turns every edge (including vector
    // art, even supersampled) into hard stair-steps; other editors
    // all smooth-sample the view.
    p.setRenderHint(QPainter::SmoothPixmapTransform, true);

    p.fillRect(viewport()->rect(), canvasSurround(state_->surroundIndex(), state_->theme()));

    if (!doc()) {
        p.setPen(colorsFor(state_->theme()).textDim);
        p.drawText(viewport()->rect(), Qt::AlignCenter,
                   tr("No document open.\nFile ▸ New (Ctrl+N) or File ▸ Open (Ctrl+O)"));
        return;
    }

    paintDocument(p, e->rect());
    // The brush cursor stays up whether or not Extras are visible (a hidden
    // cursor would blind the brush); selection edges and the other helpers
    // follow the Extras toggle, so Ctrl+H gives a clean view of the work.
    paintBrushCursor(p);
    if (extras_) paintOverlay(p);

    const double ms = std::chrono::duration<double, std::milli>(
                          std::chrono::steady_clock::now() - t0)
                          .count();
    // Incremental-repaint diagnostics: how long a canvas paint takes, and over
    // what region. Lets us see the real (async) cost behind a layer eye toggle.
    // `proof` tags whether the soft-proof path ran, so a slow frame under a
    // proof profile lines up with the [render][proof] build lines.
    ::pittore::core::log::log_info(
        "[render][paint] dirty=%dx%d at (%d,%d) viewport=%dx%d zoom=%.2f "
        "proof=%d ms=%.2f",
        e->rect().width(), e->rect().height(), e->rect().x(), e->rect().y(),
        viewport()->width(), viewport()->height(), zoom(),
        (state_->proofEnabled() || state_->proofGamut()) ? 1 : 0, ms);

    // True click→paint latency: a region edit (eye toggle, move/scale frame,
    // stroke flush) stamps its doc rect + monotonic time; the first canvas
    // paint that covers it reports the age. This captures everything the
    // handler timers can't: event-loop queuing, paint scheduling, the region
    // update being coalesced or delayed behind other work.
    const qint64 editAgeUs = state_->takeRegionEditAgeUs();
    if (editAgeUs >= 0) {
        const QRectF vr = documentTransform().mapRect(state_->lastRegionEditRect());
        if (vr.intersects(QRectF(e->rect()))) {
            ::pittore::core::log::log_info(
                "[render][region-vis] click_to_paint=%.3f ms dirty=%dx%d at (%d,%d)",
                editAgeUs / 1000.0, e->rect().width(), e->rect().height(),
                e->rect().x(), e->rect().y());
        }
    }
}


void CanvasView::paintDocument(QPainter& p) {
    paintDocument(p, QRect());
}


bool CanvasView::proofActive() const {
    if (!state_->proofEnabled() && !state_->proofGamut()) return false;
    syncProofConfig();
    return proofManager_ != nullptr;
}

void CanvasView::syncProofConfig() const {
    const AppSettings& s = state_->settings();
    const bool gamut = state_->proofGamut();
    // Compared field by field: the old formatted fingerprint cost four
    // string builds per paint just to discover nothing had moved. The flag
    // also covers a refused profile, so a missing/broken .icc is one attempt
    // (and one hint), not a file probe on every repaint.
    if (proofAttempted_ && s.proofProfile == proofProfile_ &&
        s.proofIntent == proofIntent_ && s.proofBpc == proofBpc_ &&
        gamut == proofGamut_)
        return;
    const auto t0 = std::chrono::steady_clock::now();
    proofAttempted_ = true;
    proofProfile_ = s.proofProfile;
    proofIntent_ = s.proofIntent;
    proofBpc_ = s.proofBpc;
    proofGamut_ = gamut;
    // The setup moved: whatever the cache holds was proofed for the old one.
    proofCache_ = {};
    proofCacheRect_ = {};
    proofManager_ = std::make_unique<pittore::color::ProofManager>();
    const std::string err =
        proofManager_->setProofProfile(s.proofProfile.toStdString());
    const double ms = std::chrono::duration<double, std::milli>(
                          std::chrono::steady_clock::now() - t0)
                          .count();
    if (!err.empty()) {
        proofManager_.reset();
        state_->setStatusHint(tr("Soft proof unavailable: %1")
                                  .arg(QString::fromStdString(err)));
        ::pittore::core::log::log_warning(
            "[render][proof] setup failed profile='%s' ms=%.2f: %s",
            s.proofProfile.toUtf8().constData(), ms, err.c_str());
        return;
    }
    proofManager_->setIntent(
        static_cast<pittore::color::ProofIntent>(qBound(0, s.proofIntent, 3)),
        static_cast<pittore::color::ProofIntent>(qBound(0, s.proofIntent, 3)));
    proofManager_->setBlackPointCompensation(s.proofBpc);
    proofManager_->setGamutCheck(gamut);
    // Which setup is live, for the [render][proof] trail below.
    ::pittore::core::log::log_info(
        "[render][proof] setup profile='%s' intent=%d bpc=%d gamut=%d ms=%.2f",
        s.proofProfile.toUtf8().constData(), s.proofIntent,
        s.proofBpc ? 1 : 0, gamut ? 1 : 0, ms);
}

QImage CanvasView::proofedComposite(const QRect& docRect, QRect* srcRect) const {
    DocumentItem* d = doc();
    if (!d || d->composite.isNull() || !proofManager_) return {};
    const QRect full = d->composite.rect();
    const QRect want = docRect.isEmpty() ? full : docRect.intersected(full);
    if (want.isEmpty()) return {};
    if (srcRect) *srcRect = want;

    // Cache hit: same document, same composite, same document size, and the
    // cached proof covers the request. This is the zoom/pan/idle path — a
    // blit instead of a float conversion + CMS transform over every visible
    // pixel.
    const bool hit = !proofCache_.isNull() && proofCacheDoc_ == d &&
                     proofCacheRev_ == d->revision &&
                     proofCacheDocSize_ == d->composite.size() &&
                     proofCacheRect_.contains(want);
    if (hit) {
        if (srcRect)
            *srcRect = QRect(want.topLeft() - proofCacheRect_.topLeft(),
                             want.size());
        return proofCache_;
    }

    // Cache miss. A request that covers most of the document proofs the
    // whole document once, so every later zoom frame (and every smaller
    // request) is a hit; a small request proofs just itself, which keeps an
    // edit under proof as cheap as it was. The 40 MP guard keeps the extra
    // document-sized copy bounded — beyond it the old per-paint proof simply
    // continues, logged like everything else.
    const std::uint64_t wantArea = static_cast<std::uint64_t>(want.width()) *
                                   static_cast<std::uint64_t>(want.height());
    const std::uint64_t docArea = static_cast<std::uint64_t>(full.width()) *
                                  static_cast<std::uint64_t>(full.height());
    const std::uint64_t kCacheCap = 40ull * 1000ull * 1000ull;
    const bool cacheable = docArea <= kCacheCap;
    const bool buildWhole = want == full || (cacheable && wantArea * 2 >= docArea);
    const QRect build = buildWhole ? full : want;

    const auto t0 = std::chrono::steady_clock::now();
    const QImage proofed =
        proofPreviewImage(build == full ? d->composite : d->composite.copy(build),
                          *proofManager_);
    const double ms = std::chrono::duration<double, std::milli>(
                          std::chrono::steady_clock::now() - t0)
                          .count();
    if (proofed.isNull()) return {};  // CMS refused: caller falls back
    bool kept = false;
    if (cacheable) {
        proofCache_ = proofed;
        proofCacheDoc_ = d;
        proofCacheRect_ = build;
        proofCacheDocSize_ = d->composite.size();
        proofCacheRev_ = d->revision;
        kept = true;
    }
    // The "why" behind a slow proofed frame: builds are the expensive ones
    // (cache hits cost a blit and are not worth a line each).
    ::pittore::core::log::log_info(
        "[render][proof] build rect=%dx%d+%d+%d doc=%dx%d zoom=%.2f "
        "revision=%llu kept=%d ms=%.2f",
        build.x(), build.y(), build.width(), build.height(), full.width(),
        full.height(), zoom(), static_cast<unsigned long long>(d->revision),
        kept ? 1 : 0, ms);
    if (srcRect && build != want)
        *srcRect = QRect(want.topLeft() - build.topLeft(), want.size());
    return proofed;
}


void CanvasView::paintDocument(QPainter& p, const QRect& viewDirty) {
    DocumentItem* d = doc();
    const QTransform t = documentTransform();
    const QRectF docRect(0, 0, d->size.width(), d->size.height());

    p.save();
    p.setTransform(t, false);

    // Drop shadow separating the canvas from the pasteboard.
    p.setPen(Qt::NoPen);
    p.setBrush(QColor(0, 0, 0, 90));
    p.drawRect(docRect.translated(2.0 / d->zoom, 2.0 / d->zoom));

    // Transparency checkerboard, drawn in view space so the squares stay a
    // constant size on screen regardless of zoom. Bounded to the damaged
    // region so a 60×60 layer-eye toggle repaints ~60 cells, not the whole
    // checkerboard of a zoomed-out document.
    p.resetTransform();
    QPainterPath clip;
    clip.addPolygon(t.map(QPolygonF(docRect)));
    if (!viewDirty.isEmpty()) {
        QPainterPath dirty;
        dirty.addRect(QRectF(viewDirty));
        clip = clip.intersected(dirty);
    }
    p.setClipPath(clip);
    const QRectF bounds = clip.boundingRect();
    // Checkerboard as one pattern fill (the old per-cell loop cost ~13k
    // fillRect calls on a zoomed-out 2000×2000 document). Tiles are cached by
    // size + inks (Settings > Canvas > Transparency); the cache holds a
    // handful of entries, so dragging a color wheel never reallocates per
    // repaint. The default 8px cells align to the device pixel grid
    // (multiples of 8), which is exactly how the old loop placed them.
    const AppSettings& prefs = state_->settings();
    const int cell = qBound(4, prefs.transparencyCellPx, 64);
    const QString tileKey = QStringLiteral("%1/%2/%3")
                                .arg(cell)
                                .arg(prefs.transparencyLight.name(QColor::HexArgb),
                                     prefs.transparencyDark.name(QColor::HexArgb));
    static QMap<QString, QPixmap> checkerCache;
    auto it = checkerCache.find(tileKey);
    if (it == checkerCache.end()) {
        if (checkerCache.size() >= 8) checkerCache.clear();
        QPixmap pm(2 * cell, 2 * cell);
        pm.fill(Qt::transparent);
        QPainter qp(&pm);
        qp.fillRect(0, 0, cell, cell, prefs.transparencyLight);
        qp.fillRect(cell, cell, cell, cell, prefs.transparencyLight);
        qp.fillRect(cell, 0, cell, cell, prefs.transparencyDark);
        qp.fillRect(0, cell, cell, cell, prefs.transparencyDark);
        it = checkerCache.insert(tileKey, pm);
    }
    p.fillRect(bounds, QBrush(*it));

    p.setTransform(t, false);
    // >>> Engine seam: this is the one call the Vulkan presentation path
    // replaces with a swapchain blit of the composited tile grid (R56/R91).
    // Vector art with simple paint draws straight from geometry at view
    // resolution (paintSegmented); everything else keeps the flattened blit.
    // Soft proof bypasses the segmented path so every pixel is proofed.
    const bool proof = proofActive();
    QRect docDirty;
    if (!viewDirty.isEmpty()) {
        // Sample only the damaged source stretch so a region edit paints
        // the sub-rect, not a full 2000×2000 texture transform. Snap the
        // inverse-mapped rect to whole document pixels: at fractional zoom
        // an unaligned sub-rect lets the smoothing grid wobble the painted
        // pixels between flushes (the "pixels moving" brush artifact).
        const QRectF docDirtyF =
            t.inverted().mapRect(QRectF(viewDirty)).intersected(docRect);
        docDirty = docDirtyF.isEmpty()
                       ? QRect()
                       : docDirtyF.toAlignedRect().intersected(d->composite.rect());
    }
    if (!d->composite.isNull() && !(!proof && paintSegmented(p, t, docRect, viewDirty))) {
        if (proof) {
            // Full repaint proofs (and draws) the whole document; a damaged
            // rect proofs just that stretch. Both sample through the cache,
            // so a zoom that only moves the viewport is a blit.
            if (viewDirty.isEmpty()) {
                QRect src;
                const QImage proofed = proofedComposite(QRect(), &src);
                p.drawImage(docRect, proofed.isNull() ? d->composite : proofed);
            } else if (!docDirty.isEmpty()) {
                QRect src = docDirty;
                const QImage proofed = proofedComposite(docDirty, &src);
                if (!proofed.isNull())
                    p.drawImage(QRectF(docDirty), proofed, src);
                else
                    p.drawImage(QRectF(docDirty), d->composite, docDirty);
            }
        } else if (viewDirty.isEmpty()) {
            p.drawImage(docRect, d->composite);
        } else if (!docDirty.isEmpty()) {
            p.drawImage(QRectF(docDirty), d->composite, docDirty);
        }
    }
    p.setClipping(false);

    // Quick Mask tints the unselected area red at 50%, as editors conventionally do.
    if (state_->quickMask()) {
        p.setBrush(QColor(255, 0, 0, 128));
        p.setPen(Qt::NoPen);
        if (d->selection.isEmpty()) {
            p.drawRect(docRect);
        } else if (d->selectionIsMask && !d->selectionMask.isNull()) {
            // Tint from the channel itself: red where coverage is 0, so the
            // tint hugs the object's true boundary.
            const QImage& m = d->selectionMask;
            QImage tint(m.size(), QImage::Format_ARGB32);
            tint.fill(Qt::transparent);
            for (int yy = 0; yy < m.height(); ++yy) {
                const uchar* srow = m.constScanLine(yy);
                QRgb* trow = reinterpret_cast<QRgb*>(tint.scanLine(yy));
                for (int xx = 0; xx < m.width(); ++xx)
                    trow[xx] = qRgba(255, 0, 0, (255 - srow[xx]) * 128 / 255);
            }
            p.drawImage(docRect, tint);
        } else {
            QPainterPath outside;
            outside.addRect(docRect);
            QPainterPath inside;
            if (d->selectionIsEllipse) inside.addEllipse(d->selection);
            else inside.addRect(d->selection);
            p.drawPath(outside.subtracted(inside));
        }
    }
    p.restore();

    // Canvas border.
    p.setPen(QPen(QColor(0, 0, 0, 160), 1));
    p.setBrush(Qt::NoBrush);
    p.drawPolygon(t.map(QPolygonF(docRect)));
}

}  // namespace pittore::ui
