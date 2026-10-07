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
// Crash safety (R98)
// ---------------------------------------------------------------------------
QString MainWindow::recoveryFilePath() const {
    QDir base(QStandardPaths::writableLocation(QStandardPaths::AppDataLocation));
    base.mkpath(QStringLiteral("recovery"));
    return base.filePath(QStringLiteral("recovery/session.psc"));
}


QString MainWindow::legacyRecoveryFilePath() const {
    QDir base(QStandardPaths::writableLocation(QStandardPaths::AppDataLocation));
    return base.filePath(QStringLiteral("recovery/session.ifp"));
}

// The snapshot to recover: the current one, else a legacy .ifp snapshot.
// Empty when neither exists.
QString MainWindow::existingRecoveryFilePath() const {
    const QString fresh = recoveryFilePath();
    if (QFileInfo::exists(fresh) && QFileInfo(fresh).size() > 0) return fresh;
    const QString legacy = legacyRecoveryFilePath();
    if (QFileInfo::exists(legacy) && QFileInfo(legacy).size() > 0) return legacy;
    return QString();
}


QString MainWindow::recoveryMarkerPath() const {
    QDir base(QStandardPaths::writableLocation(QStandardPaths::AppDataLocation));
    base.mkpath(QStringLiteral("recovery"));
    return base.filePath(QStringLiteral("recovery/session.lock"));
}


void MainWindow::initCrashSafety() {
    // A marker that outlived the process + a non-empty snapshot means the last
    // run ended abnormally; closeEvent removes both on a clean quit.
    const bool crashed = crashedLastRun();

    // Write-ahead marker for *this* run, before the first edit.
    QFile m(recoveryMarkerPath());
    if (m.open(QIODevice::WriteOnly | QIODevice::Truncate)) m.close();

    if (crashed) recoverSession();

    autosaveTimer_ = new QTimer(this);
    connect(autosaveTimer_, &QTimer::timeout, this, [this] { autosaveNow(); });
    connect(state_, &AppState::settingsChanged, this, [this] { scheduleAutosave(); });
    connect(state_, &AppState::activeDocumentChanged, this,
            [this](DocumentItem*) { scheduleAutosave(); });
    scheduleAutosave();
}


void MainWindow::scheduleAutosave() {
    if (!autosaveTimer_) return;
    const AppSettings& s = state_->settings();
    if (!s.autosaveEnabled) {
        autosaveTimer_->stop();
        return;
    }
    autosaveTimer_->start(qBound(1, s.autosaveIntervalMinutes, 60) * 60 * 1000);
}


void MainWindow::autosaveNow() {
    if (!state_->settings().autosaveEnabled) return;
    DocumentItem* doc = state_->activeDocument();
    if (!doc || !doc->dirty) return;

    QString error;
    if (!state_->writeRecoverySnapshot(recoveryFilePath(), &error)) {
        state_->setStatusHint(tr("Autosave failed: %1").arg(error));
        return;
    }
    QFile marker(recoveryMarkerPath());
    if (marker.open(QIODevice::WriteOnly | QIODevice::Truncate))
        marker.write(QDateTime::currentDateTime().toString(Qt::ISODate).toUtf8());
}


void MainWindow::clearRecovery() {
    QFile::remove(recoveryFilePath());
    QFile::remove(legacyRecoveryFilePath());
    QFile::remove(recoveryMarkerPath());
}


bool MainWindow::crashedLastRun() const {
    const QFileInfo marker(recoveryMarkerPath());
    return marker.exists() && !existingRecoveryFilePath().isEmpty();
}


QString MainWindow::sessionFilePath() const {
    QDir base(QStandardPaths::writableLocation(QStandardPaths::AppDataLocation));
    base.mkpath(QStringLiteral("."));
    return base.filePath(QStringLiteral("session.json"));
}


void MainWindow::recoverSession() {
    const QString snapshot = existingRecoveryFilePath();
    const QFileInfo recovery(snapshot);
    QMessageBox box(this);
    box.setIcon(QMessageBox::Question);
    box.setWindowTitle(tr("Crash Recovery"));
    box.setText(tr("Pittore Studio did not shut down cleanly."));
    box.setInformativeText(tr("A recovery snapshot from %1 was found. "
                              "Recover the unsaved document?")
                               .arg(recovery.lastModified().toString(Qt::TextDate)));
    QPushButton* recoverButton = box.addButton(tr("Recover"), QMessageBox::AcceptRole);
    box.addButton(tr("Discard"), QMessageBox::RejectRole);
    box.exec();
    if (box.clickedButton() == recoverButton) {
        QString error;
        if (!state_->openProject(snapshot, &error)) {
            state_->setStatusHint(tr("Recovery failed: %1").arg(error));
        } else if (DocumentItem* doc = state_->activeDocument()) {
            // The snapshot is not the document's real project file: detach it so
            // Save cannot write back into the recovery path we are about to
            // delete, and mark it unsaved so the user is prompted to Save As.
            doc->filePath.clear();
            doc->dirty = true;
            state_->setStatusHint(tr("Recovered the unsaved document — save it now."));
        }
    }
    // The stale snapshot is consumed either way; this run writes its own.
    QFile::remove(recoveryFilePath());
    QFile::remove(legacyRecoveryFilePath());
}


void MainWindow::showEvent(QShowEvent* event) {
    QMainWindow::showEvent(event);
    if (shownOnce_) return;
    shownOnce_ = true;
    raiseDefaultTabs();
}

void MainWindow::closeEvent(QCloseEvent* event) {    if (workspaces_) workspaces_->saveToDisk();
    // Clean-quit session for reopen-on-startup (dirty/unsaved work is not
    // preserved — same as quit today). Disabled removes any stale file.
    if (state_->settings().reopenDocuments) {
        if (!state_->saveSession(sessionFilePath()))
            QFile::remove(sessionFilePath());
    } else {
        QFile::remove(sessionFilePath());
    }
    // Clean shutdown consumes the write-ahead marker, so the next launch knows
    // there is nothing to recover (R98).
    clearRecovery();
    event->accept();
}

}  // namespace pittore::ui
