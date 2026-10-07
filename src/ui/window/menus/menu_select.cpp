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
#include <QInputDialog>

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


void MainWindow::buildSelectMenu() {
    QMenu* select = menuBar()->addMenu(tr("&Select"));
    // Ctrl+A selects ALL LAYERS (not the pixel marquee), matching how the user
    // drives multi-layer moves and deletes; the pixel-select-all action keeps
    // its behaviour but loses the shortcut to this one.
    makeAction(select, tr("All"), {},
               [this] { runCommand(QStringLiteral("select-all")); });
    makeAction(select, tr("Deselect"), QStringLiteral("Ctrl+D"),
               [this] { runCommand(QStringLiteral("deselect")); });
    makeAction(select, tr("Reselect"), QStringLiteral("Ctrl+Shift+D"));
    makeAction(select, tr("Inverse"), QStringLiteral("Ctrl+Shift+I"),
               [this] { runCommand(QStringLiteral("invert-selection")); });
    select->addSeparator();
    makeAction(select, tr("All Layers"), QStringLiteral("Ctrl+A"),
               [this] { runCommand(QStringLiteral("select-all-layers")); });
    makeAction(select, tr("Deselect Layers"), {},
               [this] { runCommand(QStringLiteral("deselect-layers")); });
    makeAction(select, tr("Find Layers"), QStringLiteral("Ctrl+Alt+Shift+F"));
    makeAction(select, tr("Isolate Layers"));
    select->addSeparator();
    makeAction(select, tr("Color Range…"));
    makeAction(select, tr("Focus Area…"));
    makeAction(select, tr("Subject"), QString(),
               [this] { runCommand(QStringLiteral("select-subject")); });
    makeAction(select, tr("Sky"));
    makeAction(select, tr("Objects"));
    select->addSeparator();
    makeAction(select, tr("Select and Mask…"), QStringLiteral("Ctrl+Alt+R"),
               [this] { runCommand(QStringLiteral("select-and-mask")); });
    makeAction(select, tr("Enhance Edges"), {},
               [this] { runCommand(QStringLiteral("enhance-edges")); });
    QMenu* modify = select->addMenu(tr("Modify"));
    // Pixel-radius prompts over the selection_mask primitives; each commits
    // one undoable step (or a status hint when there is no selection).
    auto askPx = [this](const QString& title, int max) {
        bool ok = false;
        const int v = QInputDialog::getInt(
            this, title, tr("Pixels:"), 1, 1, max, 1, &ok);
        return ok ? v : -1;
    };
    makeAction(modify, tr("Border…"), {}, [this, askPx] {
        const int v = askPx(tr("Border Selection"), 200);
        if (v > 0) state_->modifySelectionBorder(v);
    });
    makeAction(modify, tr("Smooth…"), {}, [this] {
        bool ok = false;
        const int v = QInputDialog::getInt(this, tr("Smooth Selection"),
                                           tr("Passes:"), 1, 1, 10, 1, &ok);
        if (ok) state_->modifySelectionSmooth(v);
    });
    makeAction(modify, tr("Expand…"), {}, [this, askPx] {
        const int v = askPx(tr("Expand Selection"), 1000);
        if (v > 0) state_->modifySelectionExpand(v);
    });
    makeAction(modify, tr("Contract…"), {}, [this, askPx] {
        const int v = askPx(tr("Contract Selection"), 1000);
        if (v > 0) state_->modifySelectionContract(v);
    });
    makeAction(modify, tr("Feather…"), {}, [this, askPx] {
        const int v = askPx(tr("Feather Selection"), 1000);
        if (v > 0) state_->modifySelectionFeather(v);
    });
    select->addSeparator();
    makeAction(select, tr("Grow"));
    makeAction(select, tr("Similar"));
    makeAction(select, tr("Transform Selection"));
    select->addSeparator();
    makeAction(select, tr("Edit in Quick Mask Mode"), QStringLiteral("Q"),
               [this] { state_->setQuickMask(!state_->quickMask()); });
    makeAction(select, tr("Load Selection…"));
    makeAction(select, tr("Save Selection…"));
}

}  // namespace pittore::ui
