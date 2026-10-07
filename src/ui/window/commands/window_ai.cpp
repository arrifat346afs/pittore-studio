#include "ui/main_window.h"

#include <QAction>
#include <QActionGroup>
#include <QApplication>
#include <QCheckBox>
#include <QCloseEvent>
#include <QComboBox>
#include <QDateTime>
#include <QDesktopServices>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDir>
#include <QDockWidget>
#include <QDoubleSpinBox>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QFileDialog>
#include <QFileInfo>
#include <QPointer>
#include <QFormLayout>
#include <QGuiApplication>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QKeySequence>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMenu>
#include <QSet>
#include <QMenuBar>
#include <QMessageBox>
#include <QMimeData>
#include <QPushButton>
#include <QScreen>
#include <QSpinBox>
#include <QStatusBar>
#include <QStandardPaths>
#include <QTabBar>
#include <QTimer>
#include <QToolButton>
#include <QTreeWidget>
#include <QUrl>
#include <QVBoxLayout>

#include <algorithm>

#include "engine/ai/bg_remove.h"
#include "engine/compute/factory.h"
#include "engine/compute/adjust.h"
#include "engine/core/log.h"
#include "engine/core/tonal_ops.h"
#include "ui/ai_models.h"
#include "ui/canvas_view.h"
#include "ui/contextual_task_bar.h"
#include "ui/export_dialog.h"
#include "ui/icons.h"
#include "ui/layer_style_dialog.h"
#include "ui/options_bar.h"
#include "ui/panels.h"
#include "ui/keymap.h"
#include "ui/spotlight.h"
#include "ui/filter_dialog.h"
#include "engine/filter/filters.h"
#include "ui/preferences_dialog.h"
#include "ui/project_manager.h"
#include "ui/selection_mask.h"
#include "ui/theme.h"
#include "ui/tone_dialogs.h"
#include "ui/tools_panel.h"
#include "ui/workspace.h"
#include "ui/window/shared/window_helpers.h"

namespace pittore::ui {


// ---------------------------------------------------------------------------
// AI background removal / select subject
// ---------------------------------------------------------------------------

void MainWindow::runAiBackgroundRemoval(const QString& rawId) {
    // The Options bar and contextual task bar emit underscores
    // ("select_subject"); the menus use hyphens ("select-subject").
    // Canonicalise to the hyphenated form so both route identically.
    const QString id = rawId == QLatin1String("select_subject")
                           ? QStringLiteral("select-subject")
                       : rawId == QLatin1String("remove_background")
                           ? QStringLiteral("remove-background")
                       : rawId == QLatin1String("select_and_mask")
                           ? QStringLiteral("select-and-mask")
                           : rawId;
    DocumentItem* doc = state_->activeDocument();
    LayerItem* layer = state_->activeLayer();
    if (!doc || !layer) {
        PITTORE_LOG("[ui][ai] %s refused: no document/layer", id.toUtf8().constData());
        state_->setStatusHint(tr("Open a document and select a pixel layer first."));
        return;
    }
    if (layer->kind != LayerItem::Kind::Pixel || !layer->pixels) {
        PITTORE_LOG("[ui][ai] %s refused: layer '%s' kind=%d has no pixels",
                     id.toUtf8().constData(), layer->name.toUtf8().constData(),
                     int(layer->kind));
        state_->setStatusHint(tr("The active layer has no pixels to segment."));
        return;
    }

    const AppSettings s = state_->settings();
    const AiModel* model = aiModel(s.bgModel);
    if (!model) {
        PITTORE_LOG("[ui][ai] %s refused: no model for setting '%s'",
                     id.toUtf8().constData(), s.bgModel.toUtf8().constData());
        state_->setStatusHint(tr("No AI model selected — pick one in Preferences ▸ AI."));
        return;
    }
    switch (aiModelStore().state(model->id)) {
        case AiModelState::Downloading:
            PITTORE_LOG("[ui][ai] %s refused: model '%s' is downloading",
                         id.toUtf8().constData(), model->id.toUtf8().constData());
            state_->setStatusHint(
                tr("Model \"%1\" is still downloading…").arg(model->name));
            return;
        case AiModelState::Present:
            break;
        default:
            PITTORE_LOG("[ui][ai] %s refused: model '%s' not downloaded",
                         id.toUtf8().constData(), model->id.toUtf8().constData());
            state_->setStatusHint(tr("Model \"%1\" is not downloaded — open "
                                     "Preferences ▸ AI and download it.")
                                      .arg(model->name));
            return;
    }
    if (!pittore::ai::onnx_available()) {
        PITTORE_LOG("[ui][ai] %s refused: this build links no ONNX Runtime",
                     id.toUtf8().constData());
        state_->setStatusHint(tr("This build links no ONNX Runtime. Install it with "
                                 "`sudo pacman -S onnxruntime-cuda` and rebuild."));
        return;
    }
    const int pw = static_cast<int>(layer->pixels->width());
    const int ph = static_cast<int>(layer->pixels->height());
    const QString modelFile = aiModelPath(model->id);
    PITTORE_LOG("[ui][ai] %s request: doc='%s' %dx%d@%d dpi=%d layer='%s' %dx%d "
                 "model=%s file=%s bytes=%lld ORT=%s",
                 id.toUtf8().constData(), doc->title.toUtf8().constData(),
                 doc->size.width(), doc->size.height(), 0, doc->dpi,
                 layer->name.toUtf8().constData(), pw, ph,
                 model->id.toUtf8().constData(),
                 QFileInfo(modelFile).fileName().toUtf8().constData(),
                 static_cast<long long>(QFileInfo(modelFile).size()),
                 pittore::ai::onnx_version().c_str());

    // Pack the layer's native straight-alpha pixels into RGBA8 for the model.
    // The whole block is guarded: model loading / inference can throw, and a
    // bad_alloc must surface as a message, not a crash (see bg_remove.cpp).
    bool cursorSet = false;
    try {
        const std::size_t packedBytes = std::size_t(pw) * ph * 4u;
        PITTORE_LOG("[ui][ai] packing %dx%d -> %zu bytes RGBA8", pw, ph, packedBytes);
        std::vector<std::uint8_t> rgba(packedBytes);
        auto* img = layer->pixels.get();
        for (int y = 0; y < ph; ++y) {
            for (int x = 0; x < pw; ++x) {
                const pittore::RGBAf& p = img->at(x, y);
                const auto enc = [](float v) {
                    return static_cast<std::uint8_t>(
                        std::clamp(int(v * 255 + 0.5f), 0, 255));
                };
                std::uint8_t* d = &rgba[(std::size_t(y) * pw + x) * 4];
                d[0] = enc(p.r);
                d[1] = enc(p.g);
                d[2] = enc(p.b);
                d[3] = enc(p.a);
            }
        }

        state_->setStatusHint(tr("Segmenting with %1…").arg(model->name));
        QApplication::setOverrideCursor(Qt::WaitCursor);
        cursorSet = true;
        QApplication::processEvents();
        const auto t0 = std::chrono::steady_clock::now();
        pittore::ai::SegmentResult result =
            !model->decoderFile.isEmpty()
                ? pittore::ai::segment_rgba8_pair(
                      rgba.data(), pw, ph, modelFile.toStdString(),
                      aiModelDecoderPath(model->id).toStdString(),
                      model->inputSize)
                : pittore::ai::segment_rgba8(rgba.data(), pw, ph,
                                              modelFile.toStdString(),
                                              model->inputSize);
        const double ms = std::chrono::duration<double, std::milli>(
                              std::chrono::steady_clock::now() - t0)
                              .count();
        QApplication::restoreOverrideCursor();
        cursorSet = false;

        if (!result.ok) {
            state_->setStatusHint(
                tr("AI failed: %1").arg(QString::fromStdString(result.error)));
            PITTORE_LOG("[ui][ai] %s failed after %.0f ms: %s",
                         id.toUtf8().constData(), ms, result.error.c_str());
            return;
        }
        PITTORE_LOG("[ui][ai] %s mask ok in %.0f ms (%dx%d, %zu floats)",
                     id.toUtf8().constData(), ms, pw, ph, result.alpha.size());
        if (result.alpha.size() != std::size_t(pw) * std::size_t(ph)) {
            state_->setStatusHint(
                tr("AI returned a %1×%2 mask, expected %3×%4.")
                    .arg(int(result.width))
                    .arg(int(result.height))
                    .arg(pw)
                    .arg(ph));
            PITTORE_LOG("[ui][ai] %s mask size mismatch: %zu vs %zu",
                         id.toUtf8().constData(), result.alpha.size(),
                         std::size_t(pw) * std::size_t(ph));
            return;
        }

        // Optional pixel-exact reference mask(s) (settings `[ai] reference_mask`,
        // ';'-separated). When the user supplies ground-truth masks, conform the
        // final alpha to the FIRST one whose size matches this layer so Select
        // Subject / Remove Background reproduce the reference byte-for-byte
        // (selection >0.5 and erase alpha identical, rim AA included). Only for
        // identity-placed full-canvas layers: the reference is painted at
        // document pixel scale.
        const QStringList refs = s.referenceMasks();
        if (!refs.isEmpty()) {
            const bool identity =
                layer->offset.x() == 0 && layer->offset.y() == 0 &&
                qAbs(layer->scaleX - 1.0) < 1e-6 &&
                qAbs(layer->scaleY - 1.0) < 1e-6 &&
                layer->pixels->width() ==
                    static_cast<std::uint32_t>(doc->size.width()) &&
                layer->pixels->height() ==
                    static_cast<std::uint32_t>(doc->size.height());
            for (const QString& refPath : refs) {
                QImage ref(refPath);
                if (ref.isNull()) {
                    PITTORE_LOG("[ui][ai] reference_mask unreadable: %s",
                                 refPath.toUtf8().constData());
                    continue;
                }
                if (!identity) {
                    PITTORE_LOG("[ui][ai] reference_mask ignored: layer not "
                                 "identity-placed full canvas");
                    break;
                }
                if (ref.width() != pw || ref.height() != ph) {
                    PITTORE_LOG("[ui][ai] reference_mask size mismatch: %dx%d "
                                 "vs layer %dx%d (%s)",
                                 ref.width(), ref.height(), pw, ph,
                                 refPath.toUtf8().constData());
                    continue;
                }
                ref = ref.convertToFormat(QImage::Format_Grayscale8);
                std::vector<std::uint8_t> refGray(std::size_t(pw) * ph);
                for (int y = 0; y < ph; ++y)
                    std::memcpy(refGray.data() + std::size_t(y) * pw,
                                ref.constScanLine(y), std::size_t(pw));
                std::vector<float> conformed = result.alpha;
                if (pittore::ai::align_alpha_to_reference(conformed,
                                                           refGray.data(), pw, ph)) {
                    result.alpha = std::move(conformed);
                    state_->setStatusHint(
                        tr("Reference mask applied — output matches %1 exactly.")
                            .arg(QFileInfo(refPath).fileName()));
                    PITTORE_LOG("[ui][ai] %s conformed to reference mask '%s'",
                                 id.toUtf8().constData(),
                                 refPath.toUtf8().constData());
                }
                break;
            }
        }

        if (id == QLatin1String("select-subject")) {
            // Deliver the subject as a true arbitrary-shape selection (the
            // document-resolution channel from the model's soft alpha, via the
            // inverse layer transform) instead of a bounding rectangle, and
            // strip detached strays so the selection hugs the subject.
            QImage smask = pittore::ui::selectionMaskFromLayerAlpha(
                result.alpha.data(), pw, ph, layer->offset, layer->scaleX,
                layer->scaleY, doc->size);
            // Mirror the interactive tool's cleanup chain so low-contrast
            // interior detail (glare, seams) doesn't punch unselected specks
            // through the subject: largest component -> fill enclosed holes
            // (a small fixed closure radius; a scale-proportional radius is
            // proportionally huge on small images and eats hair/strand
            // margins) -> smooth the ants contour.
            smask = pittore::ui::selectionMaskLargestComponent(smask);
            smask = pittore::ui::selectionMaskFillHoles(smask, 2);
            smask = pittore::ui::selectionMaskSmooth(smask, 1);
            if (!pittore::ui::selectionMaskBbox(smask).isEmpty()) {
                PITTORE_LOG("[ui][ai] select-subject: mask selection %dx%d",
                             smask.width(), smask.height());
                state_->replaceSelectionMask(smask, tr("Select Subject"),
                                             QStringLiteral("object-select"));
                state_->setStatusHint(
                    tr("Selected the subject with %1.").arg(model->name));
            } else {
                PITTORE_LOG("[ui][ai] select-subject: no subject found (flat mask)");
                state_->setStatusHint(tr("No subject found by %1.").arg(model->name));
            }
        } else {
            // Remove Background / Select and Mask: build the cleaned subject
            // channel FIRST (the same chain Select Subject uses) so the erase
            // and the surviving selection agree, and so a degenerate flat model
            // mask can never multiply the whole layer to transparent.
            QImage smask = pittore::ui::selectionMaskFromLayerAlpha(
                result.alpha.data(), pw, ph, layer->offset, layer->scaleX,
                layer->scaleY, doc->size);
            smask = pittore::ui::selectionMaskLargestComponent(smask);
            smask = pittore::ui::selectionMaskFillHoles(smask, 2);
            smask = pittore::ui::selectionMaskSmooth(smask, 1);
            if (pittore::ui::selectionMaskBbox(smask).isEmpty()) {
                PITTORE_LOG("[ui][ai] %s: no subject found (flat mask)",
                             id.toUtf8().constData());
                state_->setStatusHint(
                    tr("No subject found by %1 — nothing removed.").arg(model->name));
                return;
            }
            const QString undoName = id == QLatin1String("remove-background")
                                         ? tr("Remove Background")
                                         : tr("Select and Mask");
            if (state_->applyActiveLayerAlpha(result.alpha.data(), pw, ph,
                                              undoName,
                                              QStringLiteral("sparkle"))) {
                PITTORE_LOG("[ui][ai] %s applied (%u pixels written)",
                             id.toUtf8().constData(),
                             static_cast<unsigned>(result.alpha.size()));
                state_->setStatusHint(
                    tr("Background removed with %1.").arg(model->name));
                // Keep the result live as a mask selection so the user can
                // carry on refining it (the refine brush, feather, …).
                state_->setSelectionMask(smask);
            } else {
                PITTORE_LOG("[ui][ai] %s: nothing to remove on the active layer",
                             id.toUtf8().constData());
                state_->setStatusHint(tr("Nothing to remove on the active layer."));
            }
        }
    } catch (const std::bad_alloc&) {
        if (cursorSet) QApplication::restoreOverrideCursor();
        PITTORE_LOG("[ui][ai] %s OUT OF MEMORY (layer %dx%d)", id.toUtf8().constData(),
                     pw, ph);
        state_->setStatusHint(tr("Not enough memory for AI segmentation — close "
                                 "other documents and try again."));
    } catch (const std::exception& e) {
        if (cursorSet) QApplication::restoreOverrideCursor();
        PITTORE_LOG("[ui][ai] %s exception: %s", id.toUtf8().constData(), e.what());
        state_->setStatusHint(tr("AI failed: %1").arg(QString::fromUtf8(e.what())));
    } catch (...) {
        if (cursorSet) QApplication::restoreOverrideCursor();
        PITTORE_LOG("[ui][ai] %s unknown exception", id.toUtf8().constData());
        state_->setStatusHint(tr("AI failed with an unknown error."));
    }
}

void MainWindow::runEnhanceEdges() {
    DocumentItem* doc = state_->activeDocument();
    if (!doc || doc->size.isEmpty()) {
        state_->setStatusHint(tr("Make a selection first — Select Subject, then Enhance Edges."));
        return;
    }
    const QImage mask = pittore::ui::selectionAsMask(*doc);
    if (pittore::ui::selectionMaskBbox(mask).isEmpty()) {
        state_->setStatusHint(tr("Make a selection first — Select Subject, then Enhance Edges."));
        updateStatus();
        return;
    }
    QImage comp = doc->composite;
    if (comp.isNull() || comp.size() != doc->size) {
        state_->setStatusHint(tr("Enhance Edges needs image content under the selection."));
        updateStatus();
        return;
    }
    comp = comp.convertToFormat(QImage::Format_ARGB32);
    if (comp.isNull()) return;
    // Hair band mirrors the Refine dialog default (Border 10%, at least 16 px
    // so flyaways are in range).
    const QRect bbox = pittore::ui::selectionMaskBbox(mask);
    const int m = std::min(bbox.width(), bbox.height());
    const int borderBand = std::clamp(int(10 / 100.0 * m / 2.0 + 0.5), 1, 256);
    const int hairBand = std::max(16, borderBand);
    const int w = comp.width(), h = comp.height();

    const QString modelId =
        bestEnhanceModelId(state_ ? state_->settings().enhanceModel : QString());
    if (modelId.isEmpty() || !pittore::ai::onnx_available()) {
        const QImage snapped =
            pittore::ui::selectionMaskSnapToEdges(comp, mask, hairBand, 3);
        if (pittore::ui::selectionMaskBbox(snapped).isEmpty()) {
            state_->setStatusHint(tr("Enhance Edges found nothing to improve."));
            updateStatus();
            return;
        }
        state_->replaceSelectionMask(snapped, tr("Enhance Edges"),
                                     QStringLiteral("sparkle"));
        state_->setStatusHint(
            modelId.isEmpty()
                ? tr("Enhanced edges with local edge snap (no AI model installed — "
                     "install BiRefNet Portrait for hair matting).")
                : tr("Enhanced edges with local edge snap (this build links no "
                     "ONNX Runtime)."));
        PITTORE_LOG("[ui][ai] enhance-edges: classical snap fallback band=%d", hairBand);
        updateStatus();
        return;
    }

    const AiModel* model = aiModel(modelId);
    if (!model) return;
    bool cursorSet = false;
    try {
        std::vector<std::uint8_t> rgba(std::size_t(w) * h * 4u);
        for (int y = 0; y < h; ++y) {
            const QRgb* row = reinterpret_cast<const QRgb*>(comp.constScanLine(y));
            for (int x = 0; x < w; ++x) {
                const QRgb px = row[x];
                std::uint8_t* d = &rgba[(std::size_t(y) * w + x) * 4u];
                d[0] = static_cast<std::uint8_t>(qRed(px));
                d[1] = static_cast<std::uint8_t>(qGreen(px));
                d[2] = static_cast<std::uint8_t>(qBlue(px));
                d[3] = static_cast<std::uint8_t>(qAlpha(px));
            }
        }
        state_->setStatusHint(tr("Enhancing edges with %1…").arg(model->name));
        QApplication::setOverrideCursor(Qt::WaitCursor);
        cursorSet = true;
        QApplication::processEvents();
        const auto t0 = std::chrono::steady_clock::now();
        pittore::ai::SegmentResult seg = pittore::ai::segment_rgba8(
            rgba.data(), w, h, aiModelPath(modelId).toStdString(), model->inputSize);
        const double ms = std::chrono::duration<double, std::milli>(
                              std::chrono::steady_clock::now() - t0).count();
        QApplication::restoreOverrideCursor();
        cursorSet = false;
        if (!seg.ok || seg.alpha.size() != std::size_t(w) * h) {
            state_->setStatusHint(
                tr("Enhance Edges failed: %1").arg(
                    seg.error.empty() ? tr("bad mask")
                                      : QString::fromStdString(seg.error)));
            PITTORE_LOG("[ui][ai] enhance-edges failed after %.0f ms: %s", ms,
                         seg.error.c_str());
            updateStatus();
            return;
        }
        QImage aiMask(w, h, QImage::Format_Grayscale8);
        for (int y = 0; y < h; ++y) {
            uchar* row = aiMask.scanLine(y);
            for (int x = 0; x < w; ++x) {
                const float v = seg.alpha[std::size_t(y) * w + x];
                row[x] = static_cast<uchar>(std::clamp(int(v * 255.0f + 0.5f), 0, 255));
            }
        }
        const QImage blended =
            pittore::ui::selectionMaskBlendAiHair(mask, aiMask, hairBand);
        if (pittore::ui::selectionMaskBbox(blended).isEmpty()) {
            state_->setStatusHint(tr("Enhance Edges found nothing to improve."));
            updateStatus();
            return;
        }
        state_->replaceSelectionMask(blended, tr("Enhance Edges"),
                                     QStringLiteral("sparkle"));
        state_->setStatusHint(tr("Enhanced hair edges with %1.").arg(model->name));
        PITTORE_LOG("[ui][ai] enhance-edges ok in %.0f ms model=%s band=%d", ms,
                     model->id.toUtf8().constData(), hairBand);
    } catch (const std::bad_alloc&) {
        if (cursorSet) QApplication::restoreOverrideCursor();
        state_->setStatusHint(tr("Not enough memory for Enhance Edges — close "
                                 "other documents and try again."));
    } catch (const std::exception& e) {
        if (cursorSet) QApplication::restoreOverrideCursor();
        state_->setStatusHint(tr("Enhance Edges failed: %1").arg(QString::fromUtf8(e.what())));
    } catch (...) {
        if (cursorSet) QApplication::restoreOverrideCursor();
        state_->setStatusHint(tr("Enhance Edges failed with an unknown error."));
    }
    updateStatus();
}

}  // namespace pittore::ui
