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


void MainWindow::buildWindowMenu() {
    windowMenu_ = menuBar()->addMenu(tr("&Window"));
    QMenu* arrange = windowMenu_->addMenu(tr("Arrange"));
    for (const QString& name :
         {tr("Tile All Vertically"), tr("Tile All Horizontally"), tr("2-up Vertical"),
          tr("2-up Horizontal"), tr("Float in Window"), tr("Float All in Windows"),
          tr("Consolidate All to Tabs")})
        makeAction(arrange, name);
    workspaceMenu_ = windowMenu_->addMenu(tr("Workspace"));
    windowMenu_->addSeparator();

    // Text panels live under a Text submenu, conventional; everything else is
    // flat in the Window menu. Both end up in panelActions_, so syncWindowMenu
    // drives the checkmarks either way.
    const QSet<QString> textPanels = {QStringLiteral("character"),
                                      QStringLiteral("paragraph")};
    QMenu* textMenu = windowMenu_->addMenu(tr("Text"));

    for (const PanelInfo& info : allPanels()) {
        QMenu* host = textPanels.contains(info.id) ? textMenu : windowMenu_;
        QAction* action = host->addAction(info.title);
        action->setCheckable(true);
        action->setChecked(info.defaultVisible);
        const QString id = info.id;
        connect(action, &QAction::toggled, this, [this, id](bool on) { showPanel(id, on); });
        panelActions_.insert(id, action);
    }
    windowMenu_->addSeparator();
    QAction* toolsAction = windowMenu_->addAction(tr("Tools"));
    toolsAction->setCheckable(true);
    toolsAction->setChecked(true);
    connect(toolsAction, &QAction::toggled, this,
            [this](bool on) { if (toolsDock_) toolsDock_->setVisible(on); });
    QAction* optionsAction = windowMenu_->addAction(tr("Options"));
    optionsAction->setCheckable(true);
    optionsAction->setChecked(true);
    connect(optionsAction, &QAction::toggled, this,
            [this](bool on) { if (options_) options_->setVisible(on); });

    syncWindowMenu();
}

}  // namespace pittore::ui
