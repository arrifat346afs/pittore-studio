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
// A: menu bar
// ---------------------------------------------------------------------------

QAction* MainWindow::makeAction(QMenu* menu, const QString& text, const QString& shortcut,
                                std::function<void()> handler) {
    auto* action = menu->addAction(text);
    if (!shortcut.isEmpty()) {
        action->setShortcut(QKeySequence(shortcut));
        action->setProperty("defaultShortcut", shortcut);
    }
    if (handler) {
        connect(action, &QAction::triggered, this, [handler] { handler(); });
    } else {
        action->setEnabled(false);
    }
    return action;
}


QAction* MainWindow::makeCheckableAction(QMenu* menu, const QString& text, const QString& shortcut,
                                         bool checked, std::function<void(bool)> handler) {
    auto* action = menu->addAction(text);
    action->setCheckable(true);
    action->setChecked(checked);
    if (!shortcut.isEmpty()) {
        action->setShortcut(QKeySequence(shortcut));
        action->setProperty("defaultShortcut", shortcut);
    }
    if (handler)
        connect(action, &QAction::toggled, this, [handler](bool on) { handler(on); });
    else
        action->setEnabled(false);
    return action;
}


void MainWindow::applyKeymapToMenus() {
    const QMap<QString, QString> entries = loadKeymapEntries();
    const QMap<QString, QString> defaults = defaultKeymapEntries();
    QMap<QString, QString> byDefault;
    for (auto it = defaults.begin(); it != defaults.end(); ++it)
        byDefault.insert(it.value().toLower(), it.key());
    std::function<void(QMenu*)> walk = [&](QMenu* menu) {
        for (QAction* a : menu->actions()) {
            if (a->menu()) {
                walk(a->menu());
                continue;
            }
            const QString def = a->property("defaultShortcut").toString();
            if (def.isEmpty())
                continue;
            const QString id = byDefault.value(def.toLower(), QString());
            if (id.isEmpty())
                continue;
            if (!entries.contains(id)) {
                a->setShortcut(QKeySequence());
                continue;
            }
            const QString seq = entries.value(id);
            if (seq != def)
                a->setShortcut(QKeySequence(seq, QKeySequence::PortableText));
        }
    };
    for (QAction* top : menuBar()->actions()) {
        if (top->menu())
            walk(top->menu());
    }
}

}  // namespace pittore::ui
