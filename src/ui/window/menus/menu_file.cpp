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


void MainWindow::buildFileMenu() {
    QMenu* file = menuBar()->addMenu(tr("&File"));
    makeAction(file, tr("New Project…"), QStringLiteral("Ctrl+N"),
               [this] { showStartPage(); });
    makeAction(file, tr("Open Project…"), QStringLiteral("Ctrl+O"),
               [this] { openProjectDialog(); });
    QMenu* recent = file->addMenu(tr("Open Recent"));
    connect(recent, &QMenu::aboutToShow, this, [this, recent] {
        recent->clear();
        const QStringList dirs = state_->settings().recentProjects;
        if (dirs.isEmpty()) {
            recent->addAction(tr("(no recent projects)"))->setEnabled(false);
        } else {
            for (const QString& dir : dirs) {
                QAction* a = recent->addAction(QFileInfo(dir).fileName());
                a->setToolTip(dir);
                connect(a, &QAction::triggered, this, [this, dir] { openProjectFile(dir); });
            }
            recent->addSeparator();
            QAction* clear = recent->addAction(tr("Clear Recent Project List"));
            connect(clear, &QAction::triggered, this, [this] {
                AppSettings next = state_->settings();
                next.recentProjects.clear();
                state_->applySettings(next);
            });
        }
    });
    makeAction(file, tr("Open as Smart Object…"));
    file->addSeparator();
    makeAction(file, tr("Close"), QStringLiteral("Ctrl+W"), [this] {
        state_->closeDocument(state_->activeDocumentIndex());
    });
    makeAction(file, tr("Close All"), QStringLiteral("Ctrl+Alt+W"), [this] {
        while (!state_->documents().isEmpty()) state_->closeDocument(0);
    });
    makeAction(file, tr("Save"), QStringLiteral("Ctrl+S"),
               [this] { saveActiveProject(); });
    makeAction(file, tr("Save As…"), QStringLiteral("Ctrl+Shift+S"),
               [this] { saveActiveProjectAs(); });
    makeAction(file, tr("Save a Copy…"), QStringLiteral("Ctrl+Alt+S"),
               [this] { exportActiveDocument(); });
    makeAction(file, tr("Revert"), QStringLiteral("F12"));
    file->addSeparator();
    QMenu* exportMenu = file->addMenu(tr("Export"));
    makeAction(exportMenu, tr("Quick Export as PNG"), {},
               [this] { quickExportPng(); });
    makeAction(exportMenu, tr("Export As…"), QStringLiteral("Ctrl+Alt+Shift+W"),
               [this] { exportActiveDocument(); });
    makeAction(exportMenu, tr("Save for Web (Legacy)…"));
    makeAction(exportMenu, tr("Artboards to Files…"));
    makeAction(exportMenu, tr("Layers to Files…"));
    makeAction(exportMenu, tr("Export Layered PSD…"), {},
               [this] { exportLayeredPsd(); });
    makeAction(exportMenu, tr("Export Layered Affinity…"), {},
               [this] { exportLayeredAf(); });
    QMenu* automate = file->addMenu(tr("Automate"));
    makeAction(automate, tr("Batch…"));
    makeAction(automate, tr("Create Droplet…"));
    makeAction(automate, tr("Contact Sheet II…"));
    QMenu* scripts = file->addMenu(tr("Scripts"));
    makeAction(scripts, tr("Image Processor…"));
    makeAction(scripts, tr("Browse…"));
    file->addSeparator();
    makeAction(file, tr("File Info…"), QStringLiteral("Ctrl+Alt+Shift+I"));
    makeAction(file, tr("Print…"), QStringLiteral("Ctrl+P"));
    makeAction(file, tr("Print One Copy"), QStringLiteral("Ctrl+Alt+Shift+P"));
    file->addSeparator();
    makeAction(file, tr("Exit"), QStringLiteral("Ctrl+Q"), [this] { close(); });
}

}  // namespace pittore::ui
