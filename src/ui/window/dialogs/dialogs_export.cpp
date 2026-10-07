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


// R19: slices from the current guides. The guide positions partition the
// document into a grid; each cell becomes one slice.
void MainWindow::slicesFromGuides() {
    DocumentItem* doc = state_->activeDocument();
    if (!doc) return;

    QVector<double> xs{0.0};
    QVector<double> ys{0.0};
    for (double g : doc->verticalGuides) xs.append(g);
    for (double g : doc->horizontalGuides) ys.append(g);
    xs.append(doc->size.width());
    ys.append(doc->size.height());
    std::sort(xs.begin(), xs.end());
    std::sort(ys.begin(), ys.end());

    state_->beginUndoStep();
    doc->slices.clear();
    for (int i = 0; i + 1 < xs.size(); ++i) {
        for (int j = 0; j + 1 < ys.size(); ++j) {
            const QRect r(qRound(xs[i]), qRound(ys[j]), qRound(xs[i + 1] - xs[i]),
                          qRound(ys[j + 1] - ys[j]));
            if (r.width() >= 1 && r.height() >= 1) doc->slices.append(r);
        }
    }
    doc->selectedSlice = doc->slices.isEmpty() ? -1 : 0;
    state_->commitUndoStep(tr("Slices From Guides"), QStringLiteral("slice"));
    state_->markAnnotationsChanged();
    state_->setStatusHint(tr("Slices From Guides: %1 slice(s).").arg(doc->slices.size()));
}


// R19: write every slice out as a PNG (slice-01.png …) into a chosen folder.
void MainWindow::exportSlices() {
    DocumentItem* doc = state_->activeDocument();
    if (!doc || doc->slices.isEmpty()) {
        state_->setStatusHint(tr("Export Slices: no slices. Draw one with the Slice tool."));
        return;
    }
    const QString dir = QFileDialog::getExistingDirectory(this, tr("Export Slices to Folder"));
    if (dir.isEmpty()) return;

    int written = 0;
    for (int i = 0; i < doc->slices.size(); ++i) {
        const QRect crop = doc->slices[i].intersected(QRect(0, 0, doc->size.width(),
                                                            doc->size.height()));
        if (crop.isEmpty()) continue;
        const QImage image = doc->composite.copy(crop);
        const QString path =
            QDir(dir).filePath(QStringLiteral("slice-%1.png").arg(i + 1, 2, 10, QChar('0')));
        if (image.save(path)) ++written;
    }
    state_->setStatusHint(tr("Exported %1 slice(s) to %2").arg(written).arg(dir));
}


void MainWindow::aboutDialog() {
    QStringList devices;
    for (const auto& device : pittore::compute::enumerate_devices())
        devices << QStringLiteral("%1 — %2 (%3 MB)")
                       .arg(QString::fromLatin1(pittore::compute::to_string(device.type)),
                            QString::fromStdString(device.name))
                       .arg(device.memory_mb);

    QMessageBox::about(
        this, tr("About Pittore Studio"),
        tr("<b>Pittore Studio</b><br>"
           "A GPU-first raster and vector editor for Linux.<br><br>"
           "Qt %1 · %2<br><br>"
           "<b>Compute devices</b><br>%3")
            .arg(QString::fromLatin1(qVersion()),
                 QGuiApplication::platformName(),
                 devices.join(QStringLiteral("<br>"))));
}

}  // namespace pittore::ui
