#include "ui/canvas_view.h"

#include <QApplication>
#include <QContextMenuEvent>
#include <QFile>
#include <QFileInfo>
#include <QImageReader>
#include <QInputDialog>
#include <QKeyEvent>
#include <QMenu>
#include <QMimeData>
#include <QMouseEvent>
#include <QPainter>
#include <QPixmap>
#include <QPainterPath>
#include <QScrollBar>
#include <QDateTime>
#include <QTimer>
#include <QWheelEvent>
#include <QtMath>

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <unordered_map>
#include <utility>
#include <vector>

#include "ui/ai_models.h"
#include "ui/contextual_task_bar.h"
#include "ui/icons.h"
#include "ui/selection_mask.h"
#include "ui/svg_parts.h"
#include "engine/ai/bg_remove.h"
#include "engine/compute/paint.h"
#include "engine/compute/warp.h"
#include "engine/core/log.h"

#include "ui/canvas/shared/canvas_helpers.h"

namespace pittore::ui {


bool CanvasView::objectSelectMaskAt(const QPoint& docPixel, QImage& maskOut,
                                    float& iouOut, QString& status, bool expand) {
    const AppSettings s = state_->settings();
    const AiModel* model = aiModel(s.bgModel);
    DocumentItem* d = doc();
    LayerItem* layer = state_->activeLayer();
    if (!model || !d || !layer ||
        layer->kind != LayerItem::Kind::Pixel || !layer->pixels) {
        PITTORE_LOG("[ui][ai] object select refused: no pixel layer");
        status = tr("Object Select needs a pixel layer.");
        return false;
    }
    if (aiModelStore().state(model->id) != AiModelState::Present) {
        status = tr("The Object Select model is not installed — import the two "
                    ".onnx files (Preferences ▸ AI).");
        PITTORE_LOG("[ui][ai] object select refused: model '%s' not installed",
                     model->id.toUtf8().constData());
        return false;
    }
    const bool pairModel = !model->decoderFile.isEmpty();
    const int pw = static_cast<int>(layer->pixels->width());
    const int ph = static_cast<int>(layer->pixels->height());

    // Pack RGBA once — needed for segment_rgba8 (single-file models)
    // and for the SAM encoder (pair models). Straight alpha, row-major.
    const std::size_t packedBytes = std::size_t(pw) * ph * 4u;
    std::vector<std::uint8_t> rgba(packedBytes);
    auto* img = layer->pixels.get();
    for (int y = 0; y < ph; ++y) {
        for (int x = 0; x < pw; ++x) {
            const pittore::RGBAf& p = img->at(x, y);
            const auto enc = [](float v) {
                return static_cast<std::uint8_t>(
                    std::clamp(int(v * 255 + 0.5f), 0, 255));
            };
            std::uint8_t* q = &rgba[(std::size_t(y) * pw + x) * 4];
            q[0] = enc(p.r); q[1] = enc(p.g); q[2] = enc(p.b); q[3] = enc(p.a);
        }
    }

    // Encode (pair/SAM models only — decode_point/decode_mask need the
    // embeddings). Cache the full-frame segmentation for single-file
    // person models so hover moves and repeated clicks stay cheap.
    const void* key = layer->pixels.get();
    if (pairModel && (!objectEncodings_ || objectEncodingsKey_ != key ||
                        objectEncodingsModel_ != model->id)) {
        QApplication::setOverrideCursor(Qt::WaitCursor);
        state_->setStatusHint(tr("Analyzing objects…"));
        QApplication::processEvents();
        const auto t0 = std::chrono::steady_clock::now();
        auto enc = std::make_shared<const pittore::ai::SamEncodings>(
            pittore::ai::encode_rgba8(rgba.data(), pw, ph,
                                       aiModelPath(model->id).toStdString(),
                                       model->inputSize));
        const double ms = std::chrono::duration<double, std::milli>(
                              std::chrono::steady_clock::now() - t0)
                              .count();
        QApplication::restoreOverrideCursor();
        if (!enc->ok) {
            status = tr("Object Select failed: %1")
                         .arg(QString::fromStdString(enc->error));
            PITTORE_LOG("[ui][ai] object select encode failed after %.0f ms: %s",
                         ms, enc->error.c_str());
            return false;
        }
        objectEncodings_ = std::move(enc);
        objectEncodingsKey_ = key;
        objectEncodingsModel_ = model->id;
        PITTORE_LOG("[ui][ai] object select encoded %dx%d in %.0f ms", pw, ph, ms);
    }
    if (!pairModel && (!segmentAlphaCacheKey_ || segmentAlphaCacheKey_ != key ||
                         segmentAlphaCacheModel_ != model->id)) {
        QApplication::setOverrideCursor(Qt::WaitCursor);
        state_->setStatusHint(tr("Segmenting with %1…").arg(model->name));
        QApplication::processEvents();
        const auto t0 = std::chrono::steady_clock::now();
        const QString modelFile = aiModelPath(model->id);
        pittore::ai::SegmentResult seg =
            pittore::ai::segment_rgba8(rgba.data(), pw, ph,
                                        modelFile.toStdString(), model->inputSize);
        const double ms = std::chrono::duration<double, std::milli>(
                              std::chrono::steady_clock::now() - t0).count();
        QApplication::restoreOverrideCursor();
        if (!seg.ok) {
            status = tr("Object Select failed: %1")
                         .arg(QString::fromStdString(seg.error));
            PITTORE_LOG("[ui][ai] object select segment failed after %.0f ms: %s",
                         ms, seg.error.c_str());
            return false;
        }
        segmentAlphaCache_ = std::move(seg.alpha);
        segmentAlphaCacheKey_ = key;
        segmentAlphaCacheModel_ = model->id;
        PITTORE_LOG("[ui][ai] object select segment %dx%d in %.0f ms "
                     "(%zu floats)",
                     pw, ph, ms, segmentAlphaCache_.size());
    }

    // The SAM point prompt lives in LAYER pixel space (the encoder saw the
    // layer's own pixels, so decode_point maps the query through enc.width/-
    // height). Map the document pixel through the layer placement first — a
    // pasted or scaled layer would otherwise probe the wrong spot and return a
    // fragment of some other region.
    const int px = std::clamp(
        int(std::lround((docPixel.x() - layer->offset.x()) / layer->scaleX)), 0,
        pw - 1);
    const int py = std::clamp(
        int(std::lround((docPixel.y() - layer->offset.y()) / layer->scaleY)), 0,
        ph - 1);
    const std::size_t totalPx = std::size_t(pw) * std::size_t(ph);

    // pairModel (SAM encoder+decoder): hover preview via a single
    // point decode, expanded on click/settle. single-file person model
    // (e.g. birefnet-portrait): full-frame segmentation in one shot,
    // used on click/settle; hover defers to the commit path because the
    // full-model run is not cheap enough for live preview.
    pittore::ai::SamDecodeResult dec;            // only valid for pairModel.
    const float* src = nullptr;
    float bestIou = 0.0f;
    std::vector<float> hoverAlpha;  // owns the hover preview for pair models.
    std::vector<float> expandedAlpha;  // owns the expand refinement for pair models.
    if (pairModel) {
        dec = pittore::ai::decode_point(*objectEncodings_, px, py,
                                            aiModelDecoderPath(model->id)
                                                .toStdString());
        if (!dec.ok) {
            status = tr("Object Select failed: %1")
                         .arg(QString::fromStdString(dec.error));
            PITTORE_LOG("[ui][ai] object select decode failed: %s",
                         dec.error.c_str());
            return false;
        }
        if (dec.alpha.size() != totalPx) {
            status = tr("Object Select returned a wrong-sized mask.");
            return false;
        }
        bestIou = dec.iou;
        if (!expand) {
            hoverAlpha = dec.alpha;  // persistent copy for the doc channel
            src = hoverAlpha.data();
        }
    }
    if (expand) {
        if (pairModel) {
            // A point prompt now returns the whole object (the encoder is
            // ImageNet-normalised — see encode_rgba8), so refinement is a
            // belt-and-braces second pass: feeding the point decode's alpha
            // back grows/completes the silhouette without ever shrinking it.
            // (The whole-frame all-ones + 0.1-threshold dance this replaced
            // was the source of the frame-hugging background bleed.)
            QApplication::setOverrideCursor(Qt::WaitCursor);
            const pittore::ai::SamDecodeResult ref =
                pittore::ai::refine_mask(*objectEncodings_, px, py,
                                          dec.alpha.data(),
                                          aiModelDecoderPath(model->id)
                                              .toStdString());
            QApplication::restoreOverrideCursor();
            if (!ref.ok || ref.alpha.size() != totalPx) {
                status = tr("Object Select failed: refinement error.");
                return false;
            }
            expandedAlpha = ref.alpha;
            // Match the auto path (segment_rgba8_pair): snap the mask boundary
            // toward source-image edges with the same grow-only guided filter,
            // so hair/clothing slivers the coarse decoder ran inside are
            // recovered here too. Only on commit — ~0.4 s at 4K is too slow for
            // the live hover preview, which stays on the raw point decode.
            pittore::ai::guided_refine_alpha(rgba.data(), pw, ph, expandedAlpha);
            src = expandedAlpha.data();
            bestIou = ref.iou;
        } else {
            // Full-frame person/person model: the model returns the whole
            // subject in one shot. Snap to the click's component later.
            if (segmentAlphaCache_.empty()) {
                status = tr("Object Select failed: no segmentation.");
                return false;
            }
            src = segmentAlphaCache_.data();
            bestIou = 1.0f;  // full-frame subject selected
        }
    }
    if (!pairModel && !expand) {
        // Single-file model live hover: there is no per-point decode to call,
        // so preview the cached full-frame segmentation. The first call runs
        // the model once (with the wait cursor); the cache keeps every later
        // hover cheap. The commit path snaps to the component under the click.
        // Without this, `src` stays null here and the scan below segfaults.
        if (segmentAlphaCache_.empty()) return false;
        src = segmentAlphaCache_.data();
        bestIou = 1.0f;
    }

    // The mask exists (soft layer-space alpha). First decide the "nothing
    // selected" case at the standard >50% threshold.
    bool any = false;
    for (std::size_t i = 0; i < totalPx; ++i)
        if (src[i] > 0.5f) { any = true; break; }
    if (!any) {
        PITTORE_LOG("[ui][ai] object select: empty mask at (%d,%d)", px, py);
        status = tr("No object found at that point.");
        return false;
    }

    // Build a document-resolution selection channel (0..255) from the
    // layer-space mask via the inverse layer transform. Soft alpha
    // survives as anti-aliased selection edges; the ants come from the
    // mask outline.
    QImage smask = pittore::ui::selectionMaskFromLayerAlpha(
        src, pw, ph, layer->offset, layer->scaleX, layer->scaleY, d->size);
    // Keep the connected component the click landed on, so clicking a
    // person selects that person even when the full-frame mask also
    // covers others (or a bigger stray blob). Fall back to the largest
    // component when the click landed on background.
    {
        const QPoint seed(
            std::clamp(docPixel.x(), 0, d->size.width() - 1),
            std::clamp(docPixel.y(), 0, d->size.height() - 1));
        if (smask.constScanLine(seed.y())[seed.x()] > 64) {
            const QImage comp = selectionComponentAt(smask, seed);
            if (!comp.isNull() &&
                !pittore::ui::selectionMaskBbox(comp).isEmpty())
                smask = comp;
        } else {
            smask = pittore::ui::selectionMaskLargestComponent(smask);
        }
    }
    // Low-contrast interior detail (glare on the plastic, an embossed
    // rim line, a printed logo, a shadow band) can dip the per-pixel
    // SAM alpha back under the 50% threshold in patches inside the
    // object, sometimes staying connected to the exterior background
    // through a thin gap at the object's own edge. Bridge gaps up to
    // enclosed, then fill it — this never moves the real outer boundary,
    // only reclassifies background pixels the bridged probe still
    // can't reach. A small fixed closure radius keeps hair/strand margins
    // intact on small images, where a scale-proportional radius would be
    // proportionally huge.
    smask = pittore::ui::selectionMaskFillHoles(smask, 2);
    // The model's hard edge is pixel-noisy; one box pass evens the coverage so
    // the marching ants follow a clean contour.
    smask = pittore::ui::selectionMaskSmooth(smask, 1);
    // Restrict to pixels that carry image information. The AI masks are
    // smooth blobs that spill into fully transparent background (the empty
    // canvas around a cutout like the test cup) — selecting nothingness. The
    // layer's own alpha plane is the ground truth: intersect with the opaque
    // region, and when the click itself landed on an opaque pixel, snap to
    // that pixel's cutout component — the object's true silhouette, every
    // pixel of it. A full photo (no transparency) is unaffected.
    {
        double opaqueFraction = 1.0;
        const QImage opaque = layerOpaqueDocMask(layer, 16, d->size,
                                                 opaqueFraction);
        if (!opaque.isNull() && opaqueFraction < 0.9995) {
            const QPoint seed(std::clamp(docPixel.x(), 0, d->size.width() - 1),
                              std::clamp(docPixel.y(), 0, d->size.height() - 1));
            const QImage snapped = cutoutComponentAt(opaque, seed);
            if (!snapped.isNull() &&
                !pittore::ui::selectionMaskBbox(snapped).isEmpty()) {
                PITTORE_LOG("[ui][ai] object select: snapped to cutout "
                             "component (%.1f%% opaque)",
                             opaqueFraction * 100.0);
                smask = snapped;
            } else {
                for (int y = 0; y < smask.height(); ++y) {
                    uchar* dst = smask.scanLine(y);
                    const uchar* orow = opaque.constScanLine(y);
                    for (int x = 0; x < smask.width(); ++x)
                        if (orow[x] <= 127) dst[x] = 0;
                }
                PITTORE_LOG("[ui][ai] object select: intersected with opaque "
                             "region (%.1f%% opaque)",
                             opaqueFraction * 100.0);
            }
        }
    }
    if (pittore::ui::selectionMaskBbox(smask).isEmpty()) {
        status = tr("No object found at that point.");
        return false;
    }
    maskOut = std::move(smask);
    iouOut = bestIou;
    PITTORE_LOG("[ui][ai] object select: point=(%d,%d) mask=%dx%d iou=%.3f "
                 "expand=%d",
                 px, py, maskOut.width(), maskOut.height(), bestIou, expand ? 1 : 0);
    return true;
}


bool CanvasView::runAiObjectSelect(const QPoint& docPixel) {
    QImage smask;
    float iou = 0.0f;
    QString status;
    if (!objectSelectMaskAt(docPixel, smask, iou, status)) {
        if (!status.isEmpty()) state_->setStatusHint(status);
        return true;
    }
    state_->replaceSelectionMask(smask, tr("Object Select"),
                                 QStringLiteral("object-sel"));
    state_->setStatusHint(tr("Selected the object at this point (confidence "
                             "%1%).")
                              .arg(int(iou * 100.0f)));
    return true;
}


void CanvasView::runAiQuickSelect(const QPointF& from, const QPointF& to,
                                  int mode) {
    if (state_->activeTool() != ToolId::QuickSelection) return;
    DocumentItem* d = doc();
    LayerItem* layer = state_->activeLayer();
    if (!d || !layer || layer->kind != LayerItem::Kind::Pixel || !layer->pixels) {
        state_->setStatusHint(tr("Quick Selection needs a pixel layer."));
        return;
    }
    // Sample the press→release segment; each sample decodes the object under
    // the pointer with the shared SAM path and the masks are unioned, so a drag
    // grows the selection across several objects. A plain click is one sample.
    const double dist = QLineF(from, to).length();
    const int samples = std::clamp(int(dist / 48.0) + 1, 1, 6);
    QImage combined;
    int hits = 0;
    float lastIou = 0.0f;
    QString lastStatus;
    for (int i = 0; i < samples; ++i) {
        const double t = samples == 1 ? 0.0 : double(i) / double(samples - 1);
        const QPoint p = QPointF(from.x() + (to.x() - from.x()) * t,
                                 from.y() + (to.y() - from.y()) * t)
                             .toPoint();
        QImage m;
        float iou = 0.0f;
        QString status;
        if (objectSelectMaskAt(p, m, iou, status, /*expand=*/true)) {
            unionMaskInto(combined, m);
            lastIou = iou;
            ++hits;
        } else if (!status.isEmpty()) {
            lastStatus = status;
        }
    }
    if (combined.isNull()) {
        state_->setStatusHint(lastStatus.isEmpty()
                                  ? tr("Quick Selection found nothing to select.")
                                  : lastStatus);
        return;
    }
    state_->combineSelection(std::move(combined), mode, tr("Quick Selection"),
                             QStringLiteral("quick-sel"));
    state_->setStatusHint(tr("Quick Selection: combined %1 region(s), confidence "
                             "%2%.")
                              .arg(hits)
                              .arg(int(lastIou * 100.0f)));
}


void CanvasView::onHoverSettle() {
    // The pointer has rested on a point we only decoded cheaply: re-run with
    // the multi-point expansion so the preview shows the whole object.
    if (!hoverPreviewActive_ || hoverPreviewExpanded_) return;
    if (state_->activeTool() != ToolId::ObjectSelection) return;
    if (!state_->option(ToolId::ObjectSelection, QStringLiteral("object_finder"))
             .toBool())
        return;
    const QPoint p = hoverPreviewPoint_;
    QImage m;
    float iou = 0.0f;
    QString status;
    state_->setStatusHint(tr("Fine-tuning selection…"));
    if (objectSelectMaskAt(p, m, iou, status, true)) {
        hoverPreviewMask_ = m;
        hoverPreviewExpanded_ = true;
        viewport()->update();
    } else if (!status.isEmpty()) {
        state_->setStatusHint(status);
    }
}

}  // namespace pittore::ui
