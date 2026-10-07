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


void MainWindow::buildTypeMenu() {
    QMenu* type = menuBar()->addMenu(tr("&Type"));
    makeAction(type, tr("More from Adobe Fonts…"));
    QMenu* panels = type->addMenu(tr("Panels"));
    makeAction(panels, tr("Character Panel"), QString(),
               [this] { showPanel(QStringLiteral("character")); });
    makeAction(panels, tr("Paragraph Panel"), QString(),
               [this] { showPanel(QStringLiteral("paragraph")); });
    makeAction(panels, tr("Glyphs Panel"));
    type->addSeparator();
    QMenu* antialias = type->addMenu(tr("Anti-Alias"));
    for (const QString& name :
         {tr("None"), tr("Sharp"), tr("Crisp"), tr("Strong"), tr("Smooth")})
        makeAction(antialias, name);
    QMenu* orientation = type->addMenu(tr("Orientation"));
    makeAction(orientation, tr("Horizontal"));
    makeAction(orientation, tr("Vertical"));
    makeAction(type, tr("OpenType"));
    makeAction(type, tr("Extrude to 3D"))->setVisible(false);  // R01: 3D discontinued
    type->addSeparator();
    makeAction(type, tr("Create Work Path"));
    makeAction(type, tr("Convert to Shape"));
    makeAction(type, tr("Text on Path…"), {},
               [this] { runCommand(QStringLiteral("text_on_path")); });
    makeAction(type, tr("Flow Text into Shape…"), {},
               [this] { runCommand(QStringLiteral("text_in_shape")); });
    makeAction(type, tr("Rasterize Type Layer"));
    type->addSeparator();
    makeAction(type, tr("Convert Text Shape Type"));
    makeAction(type, tr("Warp Text…"));
    makeAction(type, tr("Match Font…"));
    makeAction(type, tr("Font Preview Size"));
    makeAction(type, tr("Language Options"));
    makeAction(type, tr("Paste Lorem Ipsum"));
}

}  // namespace pittore::ui
