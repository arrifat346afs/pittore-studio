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


void MainWindow::buildEditMenu() {
    QMenu* edit = menuBar()->addMenu(tr("&Edit"));
    undoAction_ = makeAction(edit, tr("Undo"), QStringLiteral("Ctrl+Z"),
                             [this] { runCommand(QStringLiteral("undo")); });
    redoAction_ = makeAction(edit, tr("Redo"), QStringLiteral("Ctrl+Shift+Z"),
                             [this] { runCommand(QStringLiteral("redo")); });
    makeAction(edit, tr("Toggle Last State"), QStringLiteral("Ctrl+Alt+Z"),
               [this] { toggleLastState(); });
    edit->addSeparator();
    makeAction(edit, tr("Cut"), QStringLiteral("Ctrl+X"),
               [this] { cutSelection(); });
    makeAction(edit, tr("Copy"), QStringLiteral("Ctrl+C"),
               [this] { runCommand(QStringLiteral("copy")); });
    makeAction(edit, tr("Copy Merged"), QStringLiteral("Ctrl+Shift+C"),
               [this] { copyMerged(); });
    makeAction(edit, tr("Paste"), QStringLiteral("Ctrl+V"),
               [this] { runCommand(QStringLiteral("paste")); });
    QMenu* pasteSpecial = edit->addMenu(tr("Paste Special"));
    makeAction(pasteSpecial, tr("Paste in Place"), QStringLiteral("Ctrl+Shift+V"),
               [this] { runCommand(QStringLiteral("paste-in-place")); });
    makeAction(pasteSpecial, tr("Paste Into"), QStringLiteral("Ctrl+Alt+Shift+V"),
               [this] { pasteMaskedToSelection(true); });
    makeAction(pasteSpecial, tr("Paste Outside"), QString(),
               [this] { pasteMaskedToSelection(false); });
    makeAction(edit, tr("Clear"), QString(),
               [this] { runCommand(QStringLiteral("clear")); });
    edit->addSeparator();
    makeAction(edit, tr("Search"), QStringLiteral("Ctrl+F"),
               [this] { showSpotlight(); });
    makeAction(edit, tr("Check Spelling…"), QString(),
               [this] { spellCheckUnavailable(); });
    makeAction(edit, tr("Find and Replace Text…"), QString(),
               [this] { findReplaceTextDialog(); });
    edit->addSeparator();
    makeAction(edit, tr("Fill…"), QStringLiteral("Shift+F5"),
               [this] { fillDialog(); });
    makeAction(edit, tr("Stroke…"), QString(), [this] { strokeSelectionDialog(); });
    makeAction(edit, tr("Content-Aware Fill…"), QString(),
               [this] { contentAwareFill(); });
    makeAction(edit, tr("Generative Fill…"),
               QString(), [this] { runCommand(QStringLiteral("generative-fill")); });
    makeAction(edit, tr("Generative Expand"), QString(),
               [this] { generativeExpandUnavailable(); });
    makeAction(edit, tr("Sky Replacement…"), QString(),
               [this] { skyReplacementUnavailable(); });
    edit->addSeparator();
    makeAction(edit, tr("Free Transform"), QStringLiteral("Ctrl+T"),
               [this] { runCommand(QStringLiteral("free-transform")); });
    QMenu* transform = edit->addMenu(tr("Transform"));
    for (const QString& name :
         {tr("Again"), tr("Scale"), tr("Rotate"), tr("Skew"), tr("Distort"), tr("Perspective"),
          tr("Warp"), tr("Rotate 180°"), tr("Rotate 90° Clockwise"),
          tr("Rotate 90° Counter Clockwise"), tr("Flip Horizontal"), tr("Flip Vertical")})
        makeAction(transform, name, QString(),
                   [this, name] { transformMenuCommand(name); });
    makeAction(edit, tr("Auto-Align Layers…"), QString(),
               [this] { autoAlignLayers(); });
    makeAction(edit, tr("Auto-Blend Layers…"), QString(),
               [this] { autoBlendLayers(); });
    edit->addSeparator();
    makeAction(edit, tr("Define Brush Preset…"), QString(),
               [this] { defineBrushPreset(); });
    makeAction(edit, tr("Define Pattern…"), QString(), [this] { definePattern(); });
    makeAction(edit, tr("Define Custom Shape…"), QString(),
               [this] { defineCustomShapeUnavailable(); });
    edit->addSeparator();
    makeAction(edit, tr("Purge"), QString(), [this] { purgeClipboard(); });
    makeAction(edit, tr("Color Settings…"), QStringLiteral("Ctrl+Shift+K"),
               [this] { openPreferencesAt(QStringLiteral("Documents")); });
    makeAction(edit, tr("Keyboard Shortcuts…"), QStringLiteral("Ctrl+Alt+Shift+K"),
               [this] { keyboardShortcutsDialog(); });
    makeAction(edit, tr("Menus…"), QString(),
               [this] { openPreferencesAt(QStringLiteral("Shortcuts")); });
    makeAction(edit, tr("Toolbar…"), QString(), [this] { editToolbarDialog(); });

    QMenu* prefs = edit->addMenu(tr("Preferences"));
    QMenu* themeMenu = prefs->addMenu(tr("Interface Color Theme"));
    auto* themeGroup = new QActionGroup(this);
    const QVector<QPair<QString, UiTheme>> themes{{tr("Black"), UiTheme::Black},
                                                 {tr("Dark Gray"), UiTheme::DarkGray},
                                                 {tr("Medium Gray"), UiTheme::MediumGray},
                                                 {tr("Light Gray"), UiTheme::LightGray}};
    for (const auto& entry : themes) {
        QAction* action = themeMenu->addAction(entry.first);
        action->setCheckable(true);
        action->setChecked(state_->theme() == entry.second);
        themeGroup->addAction(action);
        const UiTheme theme = entry.second;
        connect(action, &QAction::triggered, this, [this, theme] { state_->setTheme(theme); });
        viewMenuRefreshers_.push_back([action, this, theme] {
            action->setChecked(state_->theme() == theme);
        });
    }
    connect(prefs, &QMenu::aboutToShow, this, [this] {
        for (const auto& fn : viewMenuRefreshers_) fn();
    });
    makeAction(prefs, tr("General…"), QStringLiteral("Ctrl+K"), [this] { preferencesDialog(); });
    makeAction(prefs, tr("Workspace…"), QString(),
               [this] { openPreferencesAt(QStringLiteral("Interface")); });
    makeAction(prefs, tr("Performance…"), QString(),
               [this] { openPreferencesAt(QStringLiteral("Performance")); });
    makeAction(prefs, tr("Scratch Disks…"), QString(),
               [this] { openPreferencesAt(QStringLiteral("Advanced")); });
}

}  // namespace pittore::ui
