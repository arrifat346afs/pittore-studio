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
#include <QDragMoveEvent>
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
#include <functional>

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

// A document tab strip that also accepts file drops: dragging an image onto
// the tab bar opens it as its own document (Photoshop behaviour). Drops on
// the canvas viewport place as layers instead; this only owns the tab strip.
// Non-file drags (internal tab reorders) fall through to QTabBar untouched.
class DocumentTabBar final : public QTabBar {
  public:
    explicit DocumentTabBar(QWidget* parent = nullptr) : QTabBar(parent) {}
    std::function<bool(const QMimeData*)> canDrop;
    std::function<void(const QMimeData*)> handleDrop;

  protected:
    void dragEnterEvent(QDragEnterEvent* event) override {
        if (canDrop && canDrop(event->mimeData())) {
            event->acceptProposedAction();
            return;
        }
        QTabBar::dragEnterEvent(event);
    }
    void dragMoveEvent(QDragMoveEvent* event) override {
        if (canDrop && canDrop(event->mimeData())) {
            event->acceptProposedAction();
            return;
        }
        QTabBar::dragMoveEvent(event);
    }
    void dropEvent(QDropEvent* event) override {
        if (canDrop && canDrop(event->mimeData())) {
            event->acceptProposedAction();
            if (handleDrop) handleDrop(event->mimeData());
            return;
        }
        QTabBar::dropEvent(event);
    }
};

// ---------------------------------------------------------------------------
// D: document area
// ---------------------------------------------------------------------------

void MainWindow::buildDocumentArea() {
    auto* central = new QWidget(this);
    auto* column = new QVBoxLayout(central);
    column->setContentsMargins(0, 0, 0, 0);
    column->setSpacing(0);

    options_ = new OptionsBar(state_, central);
    connect(options_, &OptionsBar::commandTriggered, this,
            [this](ToolId, const QString& id) { runCommand(id); });
    column->addWidget(options_);
    // Selecting the Brush opens its panel: presets, import and the scratch
    // strip live there. Other tools leave the rail alone.
    connect(state_, &AppState::toolChanged, this, [this](ToolId tool) {
        if (tool == ToolId::Brush) showPanel(QStringLiteral("brushes"));
    });

    // R25: documents are tabs by default; a tab dragged out becomes a floating
    // window, and Window > Arrange > Consolidate brings them back.
    // A file dragged onto the strip opens as its own document (Photoshop):
    // canvas drops place as layers, tab-strip drops open as new tabs.
    auto* tabStrip = new DocumentTabBar(central);
    documentTabs_ = tabStrip;
    documentTabs_->setObjectName(QStringLiteral("documentTabs"));
    documentTabs_->setExpanding(false);
    documentTabs_->setMovable(true);
    documentTabs_->setTabsClosable(true);
    documentTabs_->setDrawBase(false);
    documentTabs_->setUsesScrollButtons(true);
    documentTabs_->setElideMode(Qt::ElideRight);
    documentTabs_->setAcceptDrops(true);
    documentTabs_->setToolTip(tr("Drag images here to open as new documents"));
    tabStrip->canDrop = [](const QMimeData* mime) {
        return mime && (mime->hasImage() || dropHasOpenableFiles(mime));
    };
    tabStrip->handleDrop = [this](const QMimeData* mime) {
        bool openedFile = false;
        if (mime->hasUrls()) {
            for (const QUrl& url : mime->urls()) {
                if (!url.isLocalFile()) continue;
                const QString localPath = url.toLocalFile();
                if (!suffixIsOpenable(QFileInfo(localPath).suffix())) continue;
                openProjectFile(localPath);
                openedFile = true;
            }
        }
        // Raw image data (e.g. dragged from another app) with no openable
        // file underneath becomes a new document at native size.
        if (!openedFile && mime->hasImage()) {
            const QImage img = qvariant_cast<QImage>(mime->imageData());
            if (img.isNull()) return;
            DocumentItem* doc = state_->addDocument(
                tr("Dropped Image"), img.size(), 300);
            if (!doc) return;
            state_->placeImageLayer(img, doc->title,
                                    QPointF(img.width() / 2.0, img.height() / 2.0),
                                    1.0);
            canvas_->zoomToFit();
            syncDocumentTabs();
            updateStatus();
        }
    };
    connect(documentTabs_, &QTabBar::currentChanged, this, [this](int index) {
        if (suppressTabSync_) return;
        state_->setActiveDocumentIndex(index);
    });
    connect(documentTabs_, &QTabBar::tabCloseRequested, this,
            [this](int index) { state_->closeDocument(index); });
    column->addWidget(documentTabs_);

    canvas_ = new CanvasView(state_, central);
    // Canvas "Show" toggles persist in Settings.toml (View menu + Settings >
    // Canvas write them back); adopt the stored defaults for this launch.
    {
        const AppSettings& prefs = state_->settings();
        canvas_->setRulersVisible(prefs.showRulers);
        canvas_->setGuidesVisible(prefs.showGuides);
        canvas_->setGridVisible(prefs.showGrid);
        canvas_->setSelectionEdgesVisible(prefs.showSelectionEdges);
        canvas_->setSmartGuidesVisible(prefs.showSmartGuides);
        canvas_->setPixelGridVisible(prefs.showPixelGrid);
        canvas_->setExtrasVisible(prefs.showExtras);
    }
    connect(canvas_, &CanvasView::zoomChanged, this, [this](double zoom) {
        if (zoomField_) zoomField_->setText(QString::number(zoom * 100.0, 'f', 2));
        syncDocumentTabs();
        updateStatus();
    });
    connect(canvas_, &CanvasView::cursorMoved, this, [this](QPointF pos) {
        updateStatus();
        // Feed the Info panel a live position + colour readout. The sample
        // window follows the Color Sampler's Sample Size option.
        if (DocumentItem* d = state_->activeDocument()) {
            const int sizeIdx = state_->option(
                ToolId::ColorSampler, QStringLiteral("sample_size")).toInt();
            state_->setCursorInfo(pos, sampleCompositeColor(*d, pos, sizeIdx));
        }
    });
    connect(canvas_, &CanvasView::colorSampled, this, [this](const QColor& c, bool toBackground) {
        if (toBackground)
            state_->setBackground(c);
        else
            state_->setForeground(c);
    });
    connect(canvas_, &CanvasView::copyRequested, this, [this] { runCommand(QStringLiteral("copy")); });
    connect(canvas_, &CanvasView::pasteRequested, this, [this] { runCommand(QStringLiteral("paste")); });
    connect(canvas_, &CanvasView::layerPickedFromCanvas, this, [this] {
        // Click-an-image-to-find-its-layer: surface the Layers dock so the
        // picked row is visible (the panel already highlights + scrolls to
        // it). Only an open dock is surfaced: tabbed-behind comes forward,
        // floating comes to the front, but a panel the user closed stays
        // closed — canvas picks never resurrect, same house rule as
        // raiseDefaultTabs.
        QDockWidget* dock = dockFor(QStringLiteral("layers"), false);
        if (!dock || !dock->toggleViewAction()->isChecked()) return;
        dock->show();
        dock->raise();
    });
    connect(canvas_, &CanvasView::placeRequested, this,
            [this](QPointF docPos) { placeAt(docPos); });
    connect(canvas_, &CanvasView::pasteInPlaceRequested, this, [this] { runCommand(QStringLiteral("paste-in-place")); });
    connect(canvas_, &CanvasView::clearRequested, this, [this] { runCommand(QStringLiteral("clear")); });
    if (ContextualTaskBar* bar = canvas_->taskBar())
        connect(bar, &ContextualTaskBar::commandTriggered, this,
                [this](const QString& id) { runCommand(id); });
    column->addWidget(canvas_, 1);

    setCentralWidget(central);
}

}  // namespace pittore::ui
