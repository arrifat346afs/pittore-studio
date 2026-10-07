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
#include "ui/tools/log/tool_log.h"
#include "ui/tools_panel.h"
#include "ui/workspace.h"
#include "ui/window/shared/window_helpers.h"

namespace pittore::ui {


void MainWindow::syncWindowMenu() {
    if (!workspaceMenu_) return;
    workspaceMenu_->clear();

    auto addWorkspace = [this](const QString& name) {
        QAction* action = workspaceMenu_->addAction(name);
        action->setCheckable(true);
        action->setChecked(workspaces_ && workspaces_->current() == name);
        connect(action, &QAction::triggered, this, [this, name] { workspaces_->apply(name); });
    };

    if (workspaces_) {
        for (const QString& name : workspaces_->builtInNames()) addWorkspace(name);
        const QStringList custom = workspaces_->customNames();
        if (!custom.isEmpty()) {
            workspaceMenu_->addSeparator();
            for (const QString& name : custom) addWorkspace(name);
        }
    }
    workspaceMenu_->addSeparator();
    makeAction(workspaceMenu_, tr("Reset Workspace"), QString(), [this] {
        workspaces_->resetCurrent();
    });
    makeAction(workspaceMenu_, tr("New Workspace…"), QString(), [this] { newWorkspaceDialog(); });
    makeAction(workspaceMenu_, tr("Delete Workspace…"), QString(), [this] {
        const QStringList custom = workspaces_->customNames();
        if (custom.isEmpty()) {
            QMessageBox::information(this, tr("Delete Workspace"),
                                     tr("There are no custom workspaces to delete."));
            return;
        }
        workspaces_->remove(custom.first());
    });
    workspaceMenu_->addSeparator();
    makeAction(workspaceMenu_, tr("Keyboard Shortcuts & Menus…"), QString(),
               [this] { keyboardShortcutsDialog(); });
}


// ---------------------------------------------------------------------------
// A: workspace switcher in the application bar
// ---------------------------------------------------------------------------

void MainWindow::buildApplicationBarCorner() {
    auto* corner = new QWidget(this);
    auto* row = new QHBoxLayout(corner);
    row->setContentsMargins(0, 0, 8, 0);
    row->setSpacing(4);

    workspaceButton_ = new QToolButton(corner);
    workspaceButton_->setText(tr("Essentials"));
    workspaceButton_->setPopupMode(QToolButton::InstantPopup);
    workspaceButton_->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    workspaceButton_->setAutoRaise(true);
    connect(workspaceButton_, &QToolButton::clicked, this, [this] {
        if (workspaceMenu_) workspaceMenu_->exec(QCursor::pos());
    });
    row->addWidget(workspaceButton_);

    auto* search = new QToolButton(corner);
    search->setAutoRaise(true);
    search->setToolTip(tr("Search (Ctrl+F)"));
    row->addWidget(search);

    menuBar()->setCornerWidget(corner, Qt::TopRightCorner);
    if (workspaceMenu_) workspaceButton_->setMenu(workspaceMenu_);
}


// ---------------------------------------------------------------------------
// F: status bar
// ---------------------------------------------------------------------------

void MainWindow::buildStatusBar() {
    zoomField_ = new QLineEdit(QStringLiteral("100.00"), this);
    zoomField_->setFixedWidth(64);
    zoomField_->setAlignment(Qt::AlignRight);
    connect(zoomField_, &QLineEdit::editingFinished, this, [this] {
        bool ok = false;
        const double percent = zoomField_->text().remove(QLatin1Char('%')).toDouble(&ok);
        if (ok && percent > 0) canvas_->setZoom(percent / 100.0);
    });
    statusBar()->addPermanentWidget(new QLabel(QStringLiteral("%"), this));
    statusBar()->insertPermanentWidget(0, zoomField_);

    statusLabel_ = new QLabel(this);
    statusLabel_->setMinimumWidth(320);
    statusBar()->addWidget(statusLabel_);

    hintLabel_ = new QLabel(this);
    statusBar()->addWidget(hintLabel_, 1);

    // The compute backend the active preferences resolve to: Pittore renders
    // on the engine's chosen device, so the user needs to see which one is live
    // (CPU fallback included). Refreshes whenever Preferences change.
    deviceLabel_ = new QLabel(this);
    deviceLabel_->setText(state_->computeDeviceLabel());
    deviceLabel_->setToolTip(tr("Active compute device (Preferences ▸ Performance)"));
    statusBar()->addPermanentWidget(deviceLabel_);
    statusBar()->setSizeGripEnabled(true);
    connect(state_, &AppState::settingsChanged, this, [this] {
        deviceLabel_->setText(state_->computeDeviceLabel());
    });
}


void MainWindow::updateStatus() {
    if (!statusLabel_) return;
    DocumentItem* doc = state_->activeDocument();
    if (!doc) {
        statusLabel_->clear();
        setWindowTitle(tr("Pittore Studio"));
        return;
    }
    const QPointF cursor = canvas_ ? canvas_->cursorDocumentPosition() : QPointF();
    statusLabel_->setText(doc->statusText() + QStringLiteral("    ") +
                          QStringLiteral("X: %1  Y: %2")
                              .arg(qRound(cursor.x()))
                              .arg(qRound(cursor.y())));
    setWindowTitle(QStringLiteral("%1 @ %2%  (%3)  —  Pittore Studio")
                       .arg(doc->title)
                       .arg(QString::number(doc->zoom * 100.0, 'f', 1))
                       .arg(doc->colorMode));
}


// ---------------------------------------------------------------------------
// State wiring
// ---------------------------------------------------------------------------

void MainWindow::wireState() {
    connect(state_, &AppState::themeChanged, this, [this] { applyTheme(); });
    connect(state_, &AppState::documentsChanged, this, [this] {
        syncDocumentTabs();
        updateStatus();
        // conventional Start screen: closing the last document returns to
        // the project manager (never re-enters while the dialog is up).
        if (state_->documents().isEmpty() && !startPageActive_ && isVisible())
            QTimer::singleShot(0, this, [this] { showStartPage(); });
    });
    connect(state_, &AppState::activeDocumentChanged, this, [this](DocumentItem*) {
        suppressTabSync_ = true;
        documentTabs_->setCurrentIndex(state_->activeDocumentIndex());
        suppressTabSync_ = false;
        updateStatus();
        syncUndoRedo();
    });
    connect(state_, &AppState::documentModified, this, [this] {
        updateStatus();
        syncUndoRedo();
    });
    // Undo/redo availability and step names change with every committed step,
    // every layersChanged (structural), and every switch between documents.
    connect(state_, &AppState::historyChanged, this, [this] { syncUndoRedo(); });
    connect(state_, &AppState::layersChanged, this, [this] { syncUndoRedo(); });
    connect(state_, &AppState::toolChanged, this, [this](ToolId id) {
        state_->setStatusHint(QString::fromLatin1(toolDef(id).hint));
        if (id == ToolId::Liquify) {
            // Liquify is a task, not a resting tool: revert to whatever was
            // active before it and open the dialog. Each hop is logged in the
            // Liquify tool's own file (the dialog build is the risky part).
            tool_log(id, "window/liquify", "reverting to previous tool");
            state_->setActiveTool(toolBeforeLiquify_, "liquify-revert");
            tool_log(id, "window/liquify", "opening Liquify dialog");
            liquifyDialog();
            tool_log(id, "window/liquify", "Liquify dialog returned");
        } else {
            toolBeforeLiquify_ = id;
        }
    });
    connect(state_, &AppState::statusHintChanged, this, [this](const QString& hint) {
        if (hintLabel_) hintLabel_->setText(hint);
    });
    connect(state_, &AppState::screenModeChanged, this,
            [this](ScreenMode mode) { applyScreenMode(mode); });
    connect(state_, &AppState::chromeVisibilityChanged, this,
            [this](ChromeVisibility v) { applyChromeVisibility(v); });
    connect(state_, &AppState::layerStyleRequested, this,
            [this](int effectIndex) { layerStyleDialog(effectIndex); });
    connect(state_, &AppState::toneBlendRequested, this,
            [this](int groupIndex) { toneBlendDialog(groupIndex); });
    syncUndoRedo();
}


void MainWindow::syncUndoRedo() {
    if (undoAction_) {
        const QString name = state_->undoStepName();
        undoAction_->setEnabled(state_->canUndo());
        undoAction_->setText(name.isEmpty() ? tr("Undo") : tr("Undo %1").arg(name));
    }
    if (redoAction_) {
        const QString name = state_->redoStepName();
        redoAction_->setEnabled(state_->canRedo());
        redoAction_->setText(name.isEmpty() ? tr("Redo") : tr("Redo %1").arg(name));
    }
}


void MainWindow::syncDocumentTabs() {
    suppressTabSync_ = true;
    while (documentTabs_->count() > state_->documents().size())
        documentTabs_->removeTab(documentTabs_->count() - 1);
    for (int i = 0; i < state_->documents().size(); ++i) {
        DocumentItem* doc = state_->documents().at(i);
        const QString label =
            doc->title + (doc->dirty ? QStringLiteral(" *") : QString()) +
            QStringLiteral(" @ %1%").arg(QString::number(doc->zoom * 100.0, 'f', 1));
        if (i < documentTabs_->count()) {
            documentTabs_->setTabText(i, label);
        } else {
            documentTabs_->addTab(label);
            // The platform close glyph ignores the interface theme, so each tab
            // carries a flat button drawn from the same procedural icon set as
            // the rest of the chrome.
            auto* closeButton = new QToolButton(documentTabs_);
            closeButton->setAutoRaise(true);
            closeButton->setFixedSize(14, 14);
            closeButton->setIconSize(QSize(8, 8));
            closeButton->setCursor(Qt::ArrowCursor);
            closeButton->setToolTip(tr("Close"));
            const ThemeColors c = colorsFor(state_->theme());
            closeButton->setIcon(chromeIcon(QStringLiteral("close"), c.textDim, c.text));
            connect(closeButton, &QToolButton::clicked, this, [this, closeButton] {
                const int index = documentTabs_->tabAt(closeButton->pos());
                if (index >= 0) state_->closeDocument(index);
            });
            documentTabs_->setTabButton(i, QTabBar::RightSide, closeButton);
        }
        documentTabs_->setTabToolTip(i, doc->filePath.isEmpty() ? doc->title : doc->filePath);
    }
    documentTabs_->setCurrentIndex(state_->activeDocumentIndex());
    documentTabs_->setVisible(documentTabs_->count() > 0);
    suppressTabSync_ = false;
}


void MainWindow::applyTheme() {
    const ThemeColors c = colorsFor(state_->theme());
    qApp->setPalette(paletteFor(state_->theme()));
    qApp->setStyleSheet(styleSheetFor(state_->theme()));
    if (workspaceButton_)
        workspaceButton_->setIcon(chromeIcon(QStringLiteral("columns"), c.text, c.accentText));
    update();
}


// ---------------------------------------------------------------------------
// Screen modes and chrome visibility (R24/R25)
// ---------------------------------------------------------------------------

void MainWindow::applyScreenMode(ScreenMode mode) {
    switch (mode) {
        case ScreenMode::Standard:
            menuBar()->setVisible(true);
            statusBar()->setVisible(true);
            if (!standardState_.isEmpty()) restoreState(standardState_);
            showNormal();
            break;
        case ScreenMode::FullScreenWithMenuBar:
            standardState_ = saveState();
            menuBar()->setVisible(true);
            statusBar()->setVisible(true);
            showFullScreen();
            break;
        case ScreenMode::FullScreen:
            standardState_ = saveState();
            menuBar()->setVisible(false);
            statusBar()->setVisible(false);
            state_->setChromeVisibility(ChromeVisibility::AllHidden);
            showFullScreen();
            break;
    }
}


void MainWindow::applyChromeVisibility(ChromeVisibility visibility) {
    const bool showPanels = visibility == ChromeVisibility::All;
    const bool showTools = visibility != ChromeVisibility::AllHidden;

    for (auto it = docks_.begin(); it != docks_.end(); ++it)
        if (it.value() && panelActions_.value(it.key()) &&
            panelActions_.value(it.key())->isChecked())
            it.value()->setVisible(showPanels);

    // Floating panels follow the same rule: hidden with the chrome, restored
    // only if their menu action is still checked.
    for (auto it = floatingPanels_.begin(); it != floatingPanels_.end(); ++it)
        if (it.value())
            it.value()->setVisible(showPanels && panelActions_.value(it.key()) &&
                                   panelActions_.value(it.key())->isChecked());

    if (toolsDock_) toolsDock_->setVisible(showTools);
    if (options_) options_->setVisible(showTools);
}

}  // namespace pittore::ui
