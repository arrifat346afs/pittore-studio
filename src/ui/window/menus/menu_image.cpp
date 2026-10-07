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


void MainWindow::buildImageMenu() {
    QMenu* image = menuBar()->addMenu(tr("&Image"));
    QMenu* mode = image->addMenu(tr("Mode"));
    // Grayscale / RGB / CMYK convert the document (one undo step; see
    // ui/color_mode.cpp). The rest stay disabled placeholders: they need
    // native side data the document model doesn't store (1-bit bitmaps, a
    // colour table, duotone inks, Lab numbers, open channels), and the
    // bit-depth rows below are likewise not conversions yet.
    makeAction(mode, tr("Bitmap"));
    makeAction(mode, tr("Grayscale"), QString(),
               [this] { state_->convertDocumentMode(AppState::ModeTarget::Grayscale); });
    makeAction(mode, tr("Duotone"));
    makeAction(mode, tr("Indexed Color"));
    makeAction(mode, tr("RGB Color"), QString(),
               [this] { state_->convertDocumentMode(AppState::ModeTarget::Rgb); });
    makeAction(mode, tr("CMYK Color"), QString(), [this] {
        // A configured working CMYK converts silently, the way a mode
        // flip would; otherwise ask once (and remember the choice).
        const AppSettings s = state_->settings();
        if (!s.cmykProfile.isEmpty() && QFileInfo::exists(s.cmykProfile)) {
            state_->convertDocumentMode(AppState::ModeTarget::Cmyk);
        } else {
            cmykConvertDialog();
        }
    });
    makeAction(mode, tr("Lab Color"));
    makeAction(mode, tr("Multichannel"));
    mode->addSeparator();
    for (const QString& name : {tr("8 Bits/Channel"), tr("16 Bits/Channel"), tr("32 Bits/Channel")})
        makeAction(mode, name);

    QMenu* adjust = image->addMenu(tr("Adjustments"));
    makeAction(adjust, tr("Brightness/Contrast…"));
    makeAction(adjust, tr("Levels…"), QString(), [this] { levelsDialog(); });
    makeAction(adjust, tr("Curves…"), QString(), [this] { curvesDialog(); });
    for (const QString& name :
         {tr("Exposure…"),
          tr("Vibrance…"), tr("Hue/Saturation…"), tr("Color Balance…"), tr("Black & White…"),
          tr("Photo Filter…"), tr("Channel Mixer…"), tr("Color Lookup…"), tr("Invert"),
          tr("Posterize…"), tr("Threshold…"), tr("Gradient Map…"), tr("Selective Color…"),
          tr("Shadows/Highlights…"), tr("HDR Toning…"), tr("Desaturate"), tr("Match Color…"),
          tr("Replace Color…"), tr("Equalize")})
        makeAction(adjust, name);

    image->addSeparator();
    makeAction(image, tr("Auto Tone"), QStringLiteral("Ctrl+Shift+L"));
    makeAction(image, tr("Auto Contrast"), QStringLiteral("Ctrl+Alt+Shift+L"));
    makeAction(image, tr("Auto Color"), QStringLiteral("Ctrl+Shift+B"));
    image->addSeparator();
    makeAction(image, tr("Image Size…"), QStringLiteral("Ctrl+Alt+I"));
    makeAction(image, tr("Canvas Size…"), QStringLiteral("Ctrl+Alt+C"));
    QMenu* rotate = image->addMenu(tr("Image Rotation"));
    makeAction(rotate, tr("180°"), QString(),
               [this] { state_->transformDocumentImage(QStringLiteral("180")); });
    makeAction(rotate, tr("90° Clockwise"), QString(),
               [this] { state_->transformDocumentImage(QStringLiteral("cw90")); });
    makeAction(rotate, tr("90° Counter Clockwise"), QString(),
               [this] { state_->transformDocumentImage(QStringLiteral("ccw90")); });
    makeAction(rotate, tr("Arbitrary…"));
    makeAction(rotate, tr("Flip Canvas Horizontal"), QString(),
               [this] { state_->transformDocumentImage(QStringLiteral("flipH")); });
    makeAction(rotate, tr("Flip Canvas Vertical"), QString(),
               [this] { state_->transformDocumentImage(QStringLiteral("flipV")); });
    makeAction(image, tr("Crop"));
    makeAction(image, tr("Trim…"));
    makeAction(image, tr("Reveal All"));
    image->addSeparator();
    makeAction(image, tr("Duplicate…"));
    makeAction(image, tr("Apply Image…"));
    makeAction(image, tr("Calculations…"));
    makeAction(image, tr("Variables"));
    makeAction(image, tr("Analysis"));
}

}  // namespace pittore::ui
