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
#include "ui/color_mismatch.h"
#include "ui/contextual_task_bar.h"
#include "ui/export_dialog.h"
#include "ui/icons.h"
#include "ui/layer_style_dialog.h"
#include "ui/options_bar.h"
#include "ui/panels.h"
#include "ui/persona/persona_wire.h"
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
// Construction
// ---------------------------------------------------------------------------

MainWindow::MainWindow(AppState* state, QWidget* parent)
    : QMainWindow(parent), state_(state) {
    setObjectName(QStringLiteral("PittoreMainWindow"));
    setWindowTitle(tr("Pittore Studio"));
    setDockNestingEnabled(true);
    setDockOptions(QMainWindow::AnimatedDocks | QMainWindow::AllowTabbedDocks |
                   QMainWindow::AllowNestedDocks | QMainWindow::GroupedDragging);
    setTabPosition(Qt::AllDockWidgetAreas, QTabWidget::North);
    setCorner(Qt::TopRightCorner, Qt::RightDockWidgetArea);
    setCorner(Qt::BottomRightCorner, Qt::RightDockWidgetArea);
    setCorner(Qt::TopLeftCorner, Qt::LeftDockWidgetArea);
    setCorner(Qt::BottomLeftCorner, Qt::LeftDockWidgetArea);

    // Drops anywhere on the window (toolbar, docks, empty canvas around the
    // viewport, …) open the dropped files. Drops on the canvas viewport itself
    // are intercepted earlier by CanvasView, which places images as layers.
    setAcceptDrops(true);

    buildDocumentArea();
    buildMenuBar();
    applyKeymapToMenus();
    buildApplicationBarCorner();
    buildDocks();
    buildStatusBar();

    // Vector/Pixel persona tab (own folder, src/ui/persona): switcher bar +
    // tool/panel filtering. No-op until the user clicks Vector.
    wirePersona(this, state_);

    workspaces_ = new WorkspaceManager(this, state_, tools_, this);
    connect(workspaces_, &WorkspaceManager::presetRequested, this,
            [this](const QString&, const QStringList& panels) {
                for (const PanelInfo& info : allPanels())
                    showPanel(info.id, panels.contains(info.id));
                // Batch shows raise in registry order (an odd tab can end up
                // on top): put the defaults back. Custom snapshots restore
                // their saved selection right after, which wins as it should.
                raiseDefaultTabs();
            });
    connect(workspaces_, &WorkspaceManager::currentChanged, this, [this](const QString& name) {
        if (workspaceButton_) workspaceButton_->setText(name);
        syncWindowMenu();
    });
    workspaces_->loadFromDisk();

    wireState();
    installShortcuts();
    applyTheme();
    initCrashSafety();

    // Import profile-mismatch policy (the "Embedded Profile Mismatch" policy
    // workflow): AppState calls back here so the dialog stays window-owned.
    armProfileMismatchResolver();

    // Startup page: with no document open the app opens on the Project
    // Manager. A clean-quit session restores first (crash recovery owns
    // crashed launches instead): restores resolve silently (no modal storm
    // before first paint — matches pre-dialog behavior), then the resolver
    // is re-armed for interactive opens. Deferred so dialogs show once the
    // window exists.
    if (state_->documents().isEmpty()) {
        bool restored = false;
        if (!crashedLastRun() && state_->settings().reopenDocuments) {
            state_->setProfileMismatchResolver({});
            restored = state_->restoreSession(sessionFilePath()) > 0;
            armProfileMismatchResolver();
        }
        if (restored) {
            syncDocumentTabs();
            updateStatus();
            if (canvas_) canvas_->zoomToFit();
        } else {
            QTimer::singleShot(0, this, [this] { showStartPage(); });
        }
    }

    syncDocumentTabs();
    updateStatus();
    standardState_ = saveState();
}


void MainWindow::armProfileMismatchResolver() {
    state_->setProfileMismatchResolver(
        [this](const QString& embedded, const QString& working) {
            ::pittore::core::log::log_info(
                "[import] showing mismatch dialog embedded='%s' working='%s'",
                embedded.toUtf8().constData(), working.toUtf8().constData());
            const ImportProfileChoice choice =
                askImportedProfile(this, embedded, working);
            ::pittore::core::log::log_info(
                "[import] mismatch dialog returned choice=%d",
                static_cast<int>(choice));
            return choice;
        });
}


MainWindow::~MainWindow() {
    // Each dock's visibilityChanged handler reads panelActions_, but the base
    // QWidget destructor hides children after the members are already gone.
    // Tear the docks down here, disconnected, while the map is still alive.
    const auto docks = docks_.values();
    for (const QPointer<QDockWidget>& dock : docks) {
        if (!dock) continue;
        dock->disconnect(this);
        delete dock;
    }
    docks_.clear();
    // Floating panels carry a close callback that touches panelActions_ too;
    // drop it before the members go away.
    const auto panels = floatingPanels_.values();
    for (const QPointer<QWidget>& window : panels) {
        if (auto* panel = static_cast<FloatingPanelWindow*>(window.data()))
            panel->onClose = nullptr;
    }
    floatingPanels_.clear();
}

}  // namespace pittore::ui
