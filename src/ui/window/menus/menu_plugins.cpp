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


void MainWindow::buildPluginsMenu() {
    QMenu* plugins = menuBar()->addMenu(tr("E&xtensions"));
    makeAction(plugins, tr("Manage Extensions…"), {},
               [this] { showPanel(QStringLiteral("extensions")); });
    makeAction(plugins, tr("Extension Editor…"), {},
               [this] { showPanel(QStringLiteral("xml")); });
    plugins->addSeparator();
    QMenu* render = plugins->addMenu(tr("Render"));
    for (const QString& name :
         {tr("Barcode / QR…"), tr("Mesh Polyfill (JS)…"), tr("Hatch Polyfill (JS)…")})
        makeAction(render, name, {}, [this] { showPanel(QStringLiteral("extensions")); });
    QMenu* modify = plugins->addMenu(tr("Modify Path"));
    makeAction(modify, tr("Unlink Clones (Recursive)…"), {},
               [this] { showPanel(QStringLiteral("symbols")); });
    makeAction(modify, tr("Replace SVG2 Text…"), {},
               [this] { showPanel(QStringLiteral("extensions")); });
    QMenu* palettes = plugins->addMenu(tr("Palettes"));
    for (const QString& name : {tr("Import ASE…"), tr("Import ACB…"), tr("Import GPL…")})
        makeAction(palettes, name, {}, [this] { showPanel(QStringLiteral("extensions")); });
    QMenu* exportSub = plugins->addMenu(tr("Export"));
    for (const QString& name : {tr("Export PDF with Links…"), tr("Export DXF…"),
                                tr("Command Line… (--export-*)")})
        makeAction(exportSub, name);
    plugins->addSeparator();
    makeAction(plugins, tr("Browse Plugins…"));
    makeAction(plugins, tr("Manage Plugins…"));
}

}  // namespace pittore::ui
