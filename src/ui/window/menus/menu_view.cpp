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
#include <QSignalBlocker>
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


void MainWindow::buildViewMenu() {
    QMenu* view = menuBar()->addMenu(tr("&View"));
    QMenu* proof = view->addMenu(tr("Proof Setup"));
    for (const QString& name : {tr("Working CMYK"), tr("Legacy Macintosh RGB"),
                                tr("Internet Standard RGB"), tr("Monitor RGB")})
        makeAction(proof, name);
    makeAction(proof, tr("Custom…"), QString(), [this] { proofSetupDialog(); });
    QAction* proofColorsA =
    makeCheckableAction(view, tr("Proof Colors"), QStringLiteral("Ctrl+Y"),
                        state_->proofEnabled(), [this](bool on) {
                            if (on && state_->settings().proofProfile.isEmpty()) {
                                state_->setStatusHint(
                                    tr("Choose a proof profile first (View > Proof "
                                       "Setup > Custom…)."));
                                if (auto* self = qobject_cast<QAction*>(sender())) {
                                    const QSignalBlocker block(self);
                                    self->setChecked(false);
                                }
                                return;
                            }
                            state_->setProofEnabled(on);
                        });
    viewMenuRefreshers_.push_back([proofColorsA, this] {
        proofColorsA->setChecked(state_->proofEnabled());
    });
    QAction* gamutA =
    makeCheckableAction(view, tr("Gamut Warning"), QStringLiteral("Ctrl+Shift+Y"),
                        state_->proofGamut(), [this](bool on) {
                            if (on && state_->settings().proofProfile.isEmpty()) {
                                state_->setStatusHint(
                                    tr("Choose a proof profile first (View > Proof "
                                       "Setup > Custom…)."));
                                if (auto* self = qobject_cast<QAction*>(sender())) {
                                    const QSignalBlocker block(self);
                                    self->setChecked(false);
                                }
                                return;
                            }
                            state_->setProofGamut(on);
                        });
    viewMenuRefreshers_.push_back([gamutA, this] {
        gamutA->setChecked(state_->proofGamut());
    });
    view->addSeparator();
    makeAction(view, tr("Zoom In"), QStringLiteral("Ctrl++"), [this] { canvas_->zoomIn(); });
    makeAction(view, tr("Zoom Out"), QStringLiteral("Ctrl+-"), [this] { canvas_->zoomOut(); });
    makeAction(view, tr("Fit on Screen"), QStringLiteral("Ctrl+0"),
               [this] { canvas_->zoomToFit(); });
    makeAction(view, tr("Fit Layer on Screen"), QString(),
               [this] { canvas_->zoomToActiveLayer(); });
    makeAction(view, tr("Fit to Width"), QString(), [this] { canvas_->zoomToWidth(); });
    makeAction(view, tr("100%"), QStringLiteral("Ctrl+1"),
               [this] { canvas_->zoomActualPixels(); });
    makeAction(view, tr("200%"), QString(), [this] { canvas_->setZoom(2.0); });
    makeAction(view, tr("Print Size"), QString(), [this] { canvas_->zoomPrintSize(); });
    view->addSeparator();
    makeAction(view, tr("Rotate View 90° CW"), QString(),
               [this] { canvas_->setRotation(90); });
    makeAction(view, tr("Reset Rotation"), QString(), [this] { canvas_->resetRotation(); });
    QMenu* screen = view->addMenu(tr("Screen Mode"));
    makeAction(screen, tr("Standard Screen Mode"), QString(),
               [this] { state_->setScreenMode(ScreenMode::Standard); });
    makeAction(screen, tr("Full Screen Mode With Menu Bar"), QString(),
               [this] { state_->setScreenMode(ScreenMode::FullScreenWithMenuBar); });
    makeAction(screen, tr("Full Screen Mode"), QString(),
               [this] { state_->setScreenMode(ScreenMode::FullScreen); });
    view->addSeparator();
    QAction* extrasA = makeCheckableAction(view, tr("Extras"), QStringLiteral("Ctrl+H"), canvas_->extrasVisible(),
                        [this](bool on) {
                            canvas_->setExtrasVisible(on);
                            state_->setShowExtras(on);
                        });
    viewMenuRefreshers_.push_back([extrasA, this] {
        extrasA->setChecked(canvas_->extrasVisible());
    });
    QAction* tabletA =
    makeCheckableAction(view, tr("Tablet Mode"), QString(),
                        state_->tabletMode(),
                        [this](bool on) { state_->setTabletMode(on); });
    viewMenuRefreshers_.push_back([tabletA, this] {
        tabletA->setChecked(state_->tabletMode());
    });
    QMenu* cursorMenu = view->addMenu(tr("Brush Cursor"));
    // OS cursor shape (radio): the ring paints regardless; this only swaps
    // the system pointer paired with it.
    QList<QAction*> cursorActs;
    const QStringList cursorNames = {tr("Outline Only"), tr("Arrow"),
                                     tr("Crosshair")};
    for (int i = 0; i < cursorNames.size(); ++i) {
        QAction* a = makeCheckableAction(cursorMenu, cursorNames[i], QString(),
                                         state_->cursorShape() == i,
                                         [this, i](bool) { state_->setCursorShape(i); });
        cursorActs.append(a);
    }
    // Outline shape (radio): none / circle / nib preview / tilt tick.
    cursorMenu->addSeparator();
    QList<QAction*> outlineActs;
    const QStringList outlineNames = {tr("No Outline"), tr("Circle"),
                                      tr("Tip Preview"), tr("Tilt")};
    for (int i = 0; i < outlineNames.size(); ++i) {
        QAction* a = makeCheckableAction(cursorMenu, outlineNames[i], QString(),
                                         state_->outlineShape() == i,
                                         [this, i](bool) { state_->setOutlineShape(i); });
        outlineActs.append(a);
    }
    cursorMenu->addSeparator();
    QAction* showWhileA =
    makeCheckableAction(cursorMenu, tr("Show While Painting"), QString(),
                        state_->showOutlineWhilePainting(),
                        [this](bool on) { state_->setShowOutlineWhilePainting(on); });
    viewMenuRefreshers_.push_back([showWhileA, this] {
        showWhileA->setChecked(state_->showOutlineWhilePainting());
    });
    QAction* effectiveA =
    makeCheckableAction(cursorMenu, tr("Effective Ring Size"), QString(),
                        state_->outlineEffectiveSize(),
                        [this](bool on) { state_->setOutlineEffectiveSize(on); });
    viewMenuRefreshers_.push_back([effectiveA, this] {
        effectiveA->setChecked(state_->outlineEffectiveSize());
    });
    // Keep the radio checks exclusive without an action group: each pick
    // re-checks its siblings from live settings.
    connect(cursorMenu, &QMenu::aboutToShow, this, [this, cursorActs, outlineActs] {
        for (int i = 0; i < cursorActs.size(); ++i)
            cursorActs[i]->setChecked(state_->cursorShape() == i);
        for (int i = 0; i < outlineActs.size(); ++i)
            outlineActs[i]->setChecked(state_->outlineShape() == i);
    });
    QMenu* show = view->addMenu(tr("Show"));
    QAction* showGridA =
    makeCheckableAction(show, tr("Grid"), QStringLiteral("Ctrl+'"), canvas_->gridVisible(),
                        [this](bool on) {
                            canvas_->setGridVisible(on);
                            state_->setShowGrid(on);
                        });
    viewMenuRefreshers_.push_back([showGridA, this] {
        showGridA->setChecked(canvas_->gridVisible());
    });
    QAction* showGuidesA =
    makeCheckableAction(show, tr("Guides"), QStringLiteral("Ctrl+;"), canvas_->guidesVisible(),
                        [this](bool on) {
                            canvas_->setGuidesVisible(on);
                            state_->setShowGuides(on);
                        });
    viewMenuRefreshers_.push_back([showGuidesA, this] {
        showGuidesA->setChecked(canvas_->guidesVisible());
    });
    QAction* showSelA =
    makeCheckableAction(show, tr("Selection Edges"), QString(), canvas_->selectionEdgesVisible(),
                        [this](bool on) {
                            canvas_->setSelectionEdgesVisible(on);
                            state_->setShowSelectionEdges(on);
                        });
    viewMenuRefreshers_.push_back([showSelA, this] {
        showSelA->setChecked(canvas_->selectionEdgesVisible());
    });
    QAction* showSmartA =
    makeCheckableAction(show, tr("Smart Guides"), QString(), canvas_->smartGuidesVisible(),
                        [this](bool on) {
                            canvas_->setSmartGuidesVisible(on);
                            state_->setShowSmartGuides(on);
                        });
    viewMenuRefreshers_.push_back([showSmartA, this] {
        showSmartA->setChecked(canvas_->smartGuidesVisible());
    });
    QAction* showPixelA =
    makeCheckableAction(show, tr("Pixel Grid"), QString(), canvas_->pixelGridVisible(),
                        [this](bool on) {
                            canvas_->setPixelGridVisible(on);
                            state_->setShowPixelGrid(on);
                        });
    viewMenuRefreshers_.push_back([showPixelA, this] {
        showPixelA->setChecked(canvas_->pixelGridVisible());
    });
    QAction* rulersA =
    makeCheckableAction(view, tr("Rulers"), QStringLiteral("Ctrl+R"), canvas_->rulersVisible(),
                        [this](bool on) {
                            canvas_->setRulersVisible(on);
                            state_->setShowRulers(on);
                        });
    viewMenuRefreshers_.push_back([rulersA, this] {
        rulersA->setChecked(canvas_->rulersVisible());
    });
    view->addSeparator();
    QAction* snapA =
    makeCheckableAction(view, tr("Snap"), QStringLiteral("Ctrl+Shift+;"),
                        state_->snapEnabled(),
                        [this](bool on) { state_->setSnapEnabled(on); });
    viewMenuRefreshers_.push_back([snapA, this] {
        snapA->setChecked(state_->snapEnabled());
    });
    QMenu* snapTo = view->addMenu(tr("Snap To"));
    QAction* snapGuidesA =
    makeCheckableAction(snapTo, tr("Guides"), QString(),
                        state_->snapTargets() & AppState::SnapGuides,
                        [this](bool on) {
                            state_->setSnapTarget(AppState::SnapGuides, on);
                        });
    viewMenuRefreshers_.push_back([snapGuidesA, this] {
        snapGuidesA->setChecked(state_->snapTargets() & AppState::SnapGuides);
    });
    QAction* snapGridA =
    makeCheckableAction(snapTo, tr("Grid"), QString(),
                        state_->snapTargets() & AppState::SnapGrid,
                        [this](bool on) {
                            state_->setSnapTarget(AppState::SnapGrid, on);
                        });
    viewMenuRefreshers_.push_back([snapGridA, this] {
        snapGridA->setChecked(state_->snapTargets() & AppState::SnapGrid);
    });
    QAction* snapLayersA =
    makeCheckableAction(snapTo, tr("Layers"), QString(),
                        state_->snapTargets() & AppState::SnapLayers,
                        [this](bool on) {
                            state_->setSnapTarget(AppState::SnapLayers, on);
                        });
    viewMenuRefreshers_.push_back([snapLayersA, this] {
        snapLayersA->setChecked(state_->snapTargets() & AppState::SnapLayers);
    });
    QAction* snapSlicesA =
    makeCheckableAction(snapTo, tr("Slices"), QString(),
                        state_->snapTargets() & AppState::SnapSlices,
                        [this](bool on) {
                            state_->setSnapTarget(AppState::SnapSlices, on);
                        });
    viewMenuRefreshers_.push_back([snapSlicesA, this] {
        snapSlicesA->setChecked(state_->snapTargets() & AppState::SnapSlices);
    });
    QAction* snapBoundsA =
    makeCheckableAction(snapTo, tr("Document Bounds"), QString(),
                        state_->snapTargets() & AppState::SnapDocumentBounds,
                        [this](bool on) {
                            state_->setSnapTarget(AppState::SnapDocumentBounds, on);
                        });
    viewMenuRefreshers_.push_back([snapBoundsA, this] {
        snapBoundsA->setChecked(state_->snapTargets() & AppState::SnapDocumentBounds);
    });
    makeAction(snapTo, tr("All"), QString(),
               [this] { state_->setSnapTargets(AppState::SnapTargetsAll); });
    makeAction(snapTo, tr("None"), QString(),
               [this] { state_->setSnapTargets(0); });
    QMenu* symmetry = view->addMenu(tr("Symmetry"));
    QAction* symXA =
    makeCheckableAction(symmetry, tr("Vertical Axis"), QString(),
                        canvas_->symmetryX(),
                        [this](bool on) { canvas_->setSymmetryX(on); });
    viewMenuRefreshers_.push_back([symXA, this] {
        symXA->setChecked(canvas_->symmetryX());
    });
    QAction* symYA =
    makeCheckableAction(symmetry, tr("Horizontal Axis"), QString(),
                        canvas_->symmetryY(),
                        [this](bool on) { canvas_->setSymmetryY(on); });
    viewMenuRefreshers_.push_back([symYA, this] {
        symYA->setChecked(canvas_->symmetryY());
    });
    // Menus are built once: re-read live state every time one opens so checks
    // set from Settings (or another surface) never show stale.
    auto refreshViewChecks = [this] {
        for (const auto& fn : viewMenuRefreshers_) fn();
    };
    connect(view, &QMenu::aboutToShow, this, refreshViewChecks);
    connect(show, &QMenu::aboutToShow, this, refreshViewChecks);
    connect(cursorMenu, &QMenu::aboutToShow, this, refreshViewChecks);
    connect(snapTo, &QMenu::aboutToShow, this, refreshViewChecks);
    connect(symmetry, &QMenu::aboutToShow, this, refreshViewChecks);
    view->addSeparator();
    makeAction(view, tr("New Guide…"), QString(), [this] { newGuideDialog(); });
    makeAction(view, tr("New Guide Layout…"));
    makeAction(view, tr("Clear Guides"), QString(), [this] {
        if (DocumentItem* d = state_->activeDocument()) {
            d->horizontalGuides.clear();
            d->verticalGuides.clear();
            emit state_->documentModified(d);
        }
    });
    view->addSeparator();
    makeAction(view, tr("Cycle Canvas Surround"), QStringLiteral("Space+F"),
               [this] { state_->cycleSurround(); });
}

}  // namespace pittore::ui
