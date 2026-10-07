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
#include "ui/color_mode.h"
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


void MainWindow::editToolbarDialog() {
    QDialog dialog(this);
    dialog.setWindowTitle(tr("Customize Toolbar"));
    dialog.resize(520, 560);
    auto* column = new QVBoxLayout(&dialog);
    column->addWidget(new QLabel(
        tr("Unchecked tools move to the overflow group at the bottom of the toolbar."), &dialog));

    auto* list = new QListWidget(&dialog);
    const QStringList hidden = tools_->hiddenTools();
    for (const ToolGroup& group : allGroups()) {
        const ToolDef& def = toolDef(group.leader);
        auto* item = new QListWidgetItem(QString::fromLatin1(def.name) +
                                             (def.key ? QStringLiteral("   (%1)")
                                                            .arg(shortcutTextFor(def.key))
                                                      : QString()),
                                         list);
        item->setData(Qt::UserRole, static_cast<int>(group.leader));
        item->setFlags(item->flags() | Qt::ItemIsUserCheckable);
        item->setCheckState(hidden.contains(QString::number(static_cast<int>(group.leader)))
                                ? Qt::Unchecked
                                : Qt::Checked);
    }
    column->addWidget(list, 1);

    auto* buttons = new QDialogButtonBox(
        QDialogButtonBox::Ok | QDialogButtonBox::Cancel | QDialogButtonBox::RestoreDefaults,
        &dialog);
    connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    connect(buttons->button(QDialogButtonBox::RestoreDefaults), &QPushButton::clicked, &dialog,
            [list] {
                for (int i = 0; i < list->count(); ++i)
                    list->item(i)->setCheckState(Qt::Checked);
            });
    column->addWidget(buttons);

    if (dialog.exec() == QDialog::Accepted) {
        QStringList newHidden;
        for (int i = 0; i < list->count(); ++i)
            if (list->item(i)->checkState() == Qt::Unchecked)
                newHidden << QString::number(list->item(i)->data(Qt::UserRole).toInt());
        tools_->setHiddenTools(newHidden);
    }
}


void MainWindow::newWorkspaceDialog() {
    QDialog dialog(this);
    dialog.setWindowTitle(tr("New Workspace"));
    auto* form = new QFormLayout(&dialog);

    auto* name = new QLineEdit(tr("Workspace %1").arg(workspaces_->customNames().size() + 1),
                               &dialog);
    auto* shortcuts = new QCheckBox(tr("Keyboard Shortcuts"), &dialog);
    auto* menus = new QCheckBox(tr("Menus"), &dialog);
    auto* toolbar = new QCheckBox(tr("Toolbar"), &dialog);
    toolbar->setChecked(true);

    form->addRow(tr("Name"), name);
    form->addRow(tr("Capture"), shortcuts);
    form->addRow(QString(), menus);
    form->addRow(QString(), toolbar);

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Save | QDialogButtonBox::Cancel,
                                         &dialog);
    connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    form->addRow(buttons);

    if (dialog.exec() == QDialog::Accepted && !name->text().trimmed().isEmpty()) {
        workspaces_->saveCurrentAs(name->text().trimmed(), shortcuts->isChecked(),
                                   menus->isChecked(), toolbar->isChecked());
        syncWindowMenu();
    }
}


void MainWindow::keyboardShortcutsDialog() {
    QDialog dialog(this);
    dialog.setWindowTitle(tr("Keyboard Shortcuts"));
    dialog.resize(680, 640);
    auto* column = new QVBoxLayout(&dialog);

    auto* search = new QLineEdit(&dialog);
    search->setPlaceholderText(tr("Search shortcuts…"));
    search->setClearButtonEnabled(true);
    column->addWidget(search);

    auto* tree = new QTreeWidget(&dialog);
    tree->setColumnCount(2);
    tree->setHeaderLabels({tr("Command"), tr("Shortcut")});
    tree->setUniformRowHeights(true);
    column->addWidget(tree, 1);

    // Menu commands: one top-level group per menu, recursively. Only bound
    // commands are listed — this is a shortcut reference, not a menu dump.
    std::function<void(QMenu*, QTreeWidgetItem*)> walk = [&](QMenu* menu,
                                                             QTreeWidgetItem* parent) {
        for (QAction* action : menu->actions()) {
            if (action->isSeparator()) continue;
            if (action->menu()) {
                auto* group =
                    parent ? new QTreeWidgetItem(parent) : new QTreeWidgetItem(tree);
                group->setText(0, action->text().remove(QLatin1Char('&')));
                walk(action->menu(), group);
                if (group->childCount() == 0)
                    delete group;
                else
                    group->setExpanded(true);
                continue;
            }
            const QString shortcut = action->shortcut().toString(QKeySequence::NativeText);
            if (shortcut.isEmpty()) continue;
            auto* item = parent ? new QTreeWidgetItem(parent) : new QTreeWidgetItem(tree);
            item->setText(0, action->text().remove(QLatin1Char('&')));
            item->setText(1, shortcut);
        }
    };
    for (QAction* top : menuBar()->actions()) {
        if (top->isSeparator() || !top->menu()) continue;
        auto* group = new QTreeWidgetItem(tree);
        group->setText(0, top->text().remove(QLatin1Char('&')));
        walk(top->menu(), group);
        if (group->childCount() == 0)
            delete group;
        else
            group->setExpanded(true);
    }

    // Tool letters are not QActions; list them from the tool registry.
    auto* toolsGroup = new QTreeWidgetItem(tree);
    toolsGroup->setText(0, tr("Tools"));
    for (const ToolGroup& group : allGroups()) {
        if (!group.key) continue;
        QStringList members;
        for (ToolId id : group.members) members << toolName(id);
        auto* item = new QTreeWidgetItem(toolsGroup);
        item->setText(0, members.join(QStringLiteral(", ")));
        item->setText(1, shortcutTextFor(group.key));
    }
    toolsGroup->setExpanded(true);

    // Canvas-level keys held during a drag are not QActions either.
    auto* canvasGroup = new QTreeWidgetItem(tree);
    canvasGroup->setText(0, tr("Canvas"));
    const QVector<QPair<QString, QString>> canvasKeys{
        {tr("Temporary Hand"), QStringLiteral("Space")},
        {tr("Temporary Move"), QStringLiteral("Ctrl")},
        {tr("Hide all chrome"), QStringLiteral("Tab")},
        {tr("Hide panels only"), QStringLiteral("Shift+Tab")},
        {tr("Cycle screen modes"), QStringLiteral("F")},
        {tr("Cycle screen modes backwards"), QStringLiteral("Shift+F")},
        {tr("Cycle canvas surround"), QStringLiteral("Space+F")},
    };
    for (const auto& entry : canvasKeys) {
        auto* item = new QTreeWidgetItem(canvasGroup);
        item->setText(0, entry.first);
        item->setText(1, entry.second);
    }
    canvasGroup->setExpanded(true);

    tree->setColumnWidth(0, qMax(tree->columnWidth(0), 280));
    tree->resizeColumnToContents(1);

    // Live filter: hide rows whose command/keys do not contain the query; a
    // group stays visible while any descendant matches.
    connect(search, &QLineEdit::textChanged, &dialog, [tree](const QString& query) {
        const QString q = query.trimmed();
        std::function<bool(QTreeWidgetItem*)> apply = [&](QTreeWidgetItem* item) -> bool {
            bool anyMatch = false;
            for (int i = 0; i < item->childCount(); ++i)
                anyMatch = apply(item->child(i)) || anyMatch;
            const bool selfMatch =
                q.isEmpty() || item->text(0).contains(q, Qt::CaseInsensitive) ||
                item->text(1).contains(q, Qt::CaseInsensitive);
            const bool visible = selfMatch || anyMatch;
            item->setHidden(!visible);
            if (!q.isEmpty() && visible) item->setExpanded(true);
            return visible;
        };
        for (int i = 0; i < tree->topLevelItemCount(); ++i)
            apply(tree->topLevelItem(i));
    });

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Close, &dialog);
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    column->addWidget(buttons);
    auto* keyRow = new QHBoxLayout();
    auto* keyLabel = new QLabel(keymapFilePath(), &dialog);
    keyLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);
    keyRow->addWidget(keyLabel, 1);
    auto* openKey = new QPushButton(tr("Open Keymap File"), &dialog);
    keyRow->addWidget(openKey);
    auto* resetKey = new QPushButton(tr("Reset to Defaults"), &dialog);
    keyRow->addWidget(resetKey);
    column->addLayout(keyRow);
    connect(openKey, &QPushButton::clicked, &dialog, [] {
        saveKeymapEntries(loadKeymapEntries());
        QDesktopServices::openUrl(QUrl::fromLocalFile(keymapFilePath()));
    });
    connect(resetKey, &QPushButton::clicked, this, [this, &dialog] {
        saveKeymapEntries(defaultKeymapEntries());
        applyKeymapToMenus();
        refreshCarrierShortcuts();
        dialog.accept();
        keyboardShortcutsDialog();
    });
    dialog.exec();
}


void MainWindow::showSpotlight() {
    QVector<SpotlightEntry> entries;
    std::function<void(QMenu*, const QString&)> walk = [&](QMenu* menu, const QString& group) {
        for (QAction* a : menu->actions()) {
            if (a->isSeparator())
                continue;
            if (a->menu()) {
                walk(a->menu(), a->text().remove(QLatin1Char('&')));
                continue;
            }
            if (!a->isEnabled())
                continue;
            const QString title = a->text().remove(QLatin1Char('&'));
            const QString hint = a->shortcut().toString(QKeySequence::NativeText);
            QPointer<QAction> guard(a);
            entries.push_back({title, hint, group, [this, guard] {
                if (guard)
                    guard->trigger();
            }});
        }
    };
    for (QAction* top : menuBar()->actions()) {
        if (top->menu())
            walk(top->menu(), top->text().remove(QLatin1Char('&')));
    }
    for (const ToolDef& def : allTools()) {
        const QString title = toolName(def.id);
        const QString hint = def.key ? shortcutTextFor(def.key) : QString();
        const ToolId id = def.id;
        entries.push_back({title, hint, tr("Tools"), [this, id] { state_->setActiveTool(id); }});
    }
    for (const PanelInfo& info : allPanels()) {
        const QString title = info.title;
        const QString id = info.id;
        entries.push_back({title, QString(), tr("Panels"), [this, id] { showPanel(id, true); }});
    }
    entries.push_back({tr("Fit on Screen"), QStringLiteral("Ctrl+0"), tr("View"), [this] { if (canvas_) canvas_->zoomToFit(); }});
    entries.push_back({tr("Actual Pixels"), QStringLiteral("Ctrl+1"), tr("View"), [this] { if (canvas_) canvas_->zoomActualPixels(); }});
    entries.push_back({tr("Swap Colors"), QStringLiteral("X"), tr("Tools"), [this] { state_->swapColors(); }});
    entries.push_back({tr("Reset Colors"), QStringLiteral("D"), tr("Tools"), [this] { state_->resetColors(); }});
    entries.push_back({tr("Preferences"), QStringLiteral("Ctrl+K"), tr("Edit"), [this] { preferencesDialog(); }});
    entries.push_back({tr("Keyboard Shortcuts"), QStringLiteral(""), tr("Help"), [this] { keyboardShortcutsDialog(); }});
    SpotlightDialog dlg(entries, this);
    dlg.exec();
}


void MainWindow::preferencesDialog() { openPreferencesAt(QStringLiteral("General")); }

void MainWindow::openPreferencesAt(const QString& tab) {
    InterfaceHooks hooks;
    if (tools_) {
        hooks.hasToolbar = true;
        hooks.twoColumn = tools_->twoColumn();
        hooks.setTwoColumn = [t = tools_](bool on) { t->setTwoColumn(on); };
        hooks.showAllTools = [t = tools_] { t->setHiddenTools({}); };
    }
    if (workspaces_) {
        for (const QString& name : workspaces_->builtInNames()) {
            hooks.workspaceNames << name;
            hooks.workspaceIds << name;
        }
        for (const QString& name : workspaces_->customNames()) {
            hooks.workspaceNames << name + tr(" (custom)");
            hooks.workspaceIds << name;
        }
        hooks.currentWorkspace = workspaces_->current();
        hooks.applyWorkspace = [w = workspaces_](const QString& name) { w->apply(name); };
    }
    ViewHooks view;
    if (canvas_) {
        view.setRulers = [c = canvas_](bool on) { c->setRulersVisible(on); };
        view.setGuides = [c = canvas_](bool on) { c->setGuidesVisible(on); };
        view.setGrid = [c = canvas_](bool on) { c->setGridVisible(on); };
        view.setSelectionEdges = [c = canvas_](bool on) { c->setSelectionEdgesVisible(on); };
        view.setSmartGuides = [c = canvas_](bool on) { c->setSmartGuidesVisible(on); };
        view.setPixelGrid = [c = canvas_](bool on) { c->setPixelGridVisible(on); };
        view.setExtras = [c = canvas_](bool on) { c->setExtrasVisible(on); };
    }
    AdvancedHooks advanced;
    advanced.extraNames << tr("Recovery snapshot") << tr("Project library");
    advanced.extraPaths << recoveryFilePath() << projectsRootDir();
    PreferencesDialog dialog(state_, this, tab, hooks, view, advanced);
    connect(&dialog, &PreferencesDialog::keymapChanged, this, [this] {
        applyKeymapToMenus();
        refreshCarrierShortcuts();
    });
    dialog.exec();
}


void MainWindow::proofSetupDialog() {
    QDialog dialog(this);
    dialog.setWindowTitle(tr("Proof Setup"));
    auto* form = new QFormLayout(&dialog);

    const AppSettings& s = state_->settings();
    auto* path = new QLineEdit(s.proofProfile, &dialog);
    path->setPlaceholderText(tr("No proof profile — proofing is off"));
    path->setClearButtonEnabled(true);
    auto* browse = new QPushButton(tr("Browse…"), &dialog);
    QObject::connect(browse, &QPushButton::clicked, &dialog, [&] {
        const QString file = QFileDialog::getOpenFileName(
            &dialog, tr("Proof profile"), path->text().trimmed(),
            tr("ICC profiles (*.icc *.icm);;All files (*)"));
        if (!file.isEmpty()) path->setText(file);
    });
    auto* profileRow = new QHBoxLayout;
    profileRow->addWidget(path, 1);
    profileRow->addWidget(browse);
    form->addRow(tr("Profile"), profileRow);

    auto* intent = new QComboBox(&dialog);
    intent->addItem(tr("Perceptual"), 0);
    intent->addItem(tr("Relative Colorimetric"), 1);
    intent->addItem(tr("Saturation"), 2);
    intent->addItem(tr("Absolute Colorimetric"), 3);
    intent->setCurrentIndex(
        qMax(0, intent->findData(qBound(0, s.proofIntent, 3))));
    form->addRow(tr("Intent"), intent);

    auto* bpc = new QCheckBox(tr("Black point compensation"), &dialog);
    bpc->setChecked(s.proofBpc);
    form->addRow(QString(), bpc);

    auto* buttons =
        new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel,
                             &dialog);
    QObject::connect(buttons, &QDialogButtonBox::accepted, &dialog,
                     &QDialog::accept);
    QObject::connect(buttons, &QDialogButtonBox::rejected, &dialog,
                     &QDialog::reject);
    form->addRow(buttons);

    if (dialog.exec() != QDialog::Accepted) return;
    const QString picked = path->text().trimmed();
    if (!picked.isEmpty() && !QFileInfo::exists(picked)) {
        QMessageBox::warning(this, tr("Proof Setup"),
                             tr("Profile file not found:\n%1").arg(picked));
        return;
    }
    AppSettings next = s;
    next.proofProfile = picked;
    next.proofIntent = intent->currentData().toInt();
    next.proofBpc = bpc->isChecked();
    state_->applySettings(next);
}

void MainWindow::cmykConvertDialog() {
    QDialog dialog(this);
    dialog.setWindowTitle(tr("CMYK Color"));
    auto* form = new QFormLayout(&dialog);

    auto* note = new QLabel(
        tr("Colours convert through this destination profile and the "
           "document is retagged CMYK; the profile rides along with CMYK "
           "exports. Blank uses the document's own profile, a system "
           "default, or a naive (not colour-managed) separation."),
        &dialog);
    note->setWordWrap(true);
    form->addRow(note);

    auto* path = new QLineEdit(defaultCmykProfilePath(), &dialog);
    path->setPlaceholderText(tr("No profile — naive conversion"));
    path->setClearButtonEnabled(true);
    auto* browse = new QPushButton(tr("Browse…"), &dialog);
    QObject::connect(browse, &QPushButton::clicked, &dialog, [&] {
        const QString file = QFileDialog::getOpenFileName(
            &dialog, tr("CMYK destination profile"), path->text().trimmed(),
            tr("ICC profiles (*.icc *.icm);;All files (*)"));
        if (!file.isEmpty()) path->setText(file);
    });
    auto* profileRow = new QHBoxLayout;
    profileRow->addWidget(path, 1);
    profileRow->addWidget(browse);
    form->addRow(tr("Profile"), profileRow);

    auto* buttons =
        new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel,
                             &dialog);
    buttons->button(QDialogButtonBox::Ok)->setText(tr("Convert"));
    QObject::connect(buttons, &QDialogButtonBox::accepted, &dialog,
                     &QDialog::accept);
    QObject::connect(buttons, &QDialogButtonBox::rejected, &dialog,
                     &QDialog::reject);
    form->addRow(buttons);

    if (dialog.exec() != QDialog::Accepted) return;
    const QString picked = path->text().trimmed();
    if (!picked.isEmpty() && !QFileInfo::exists(picked)) {
        QMessageBox::warning(this, tr("CMYK Color"),
                             tr("Profile file not found:\n%1").arg(picked));
        return;
    }
    if (!picked.isEmpty()) {
        AppSettings next = state_->settings();
        next.cmykProfile = picked;
        state_->applySettings(next);
    }
    state_->convertDocumentMode(AppState::ModeTarget::Cmyk);
}

}  // namespace pittore::ui
