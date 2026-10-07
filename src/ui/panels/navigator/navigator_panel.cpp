#include "ui/dpi_pixmap.h"
#include "ui/panels.h"

#include <QApplication>
#include <QButtonGroup>
#include <QCheckBox>
#include <QColorDialog>
#include <QComboBox>
#include <QContextMenuEvent>
#include <QDataStream>
#include <QDir>
#include <QDoubleSpinBox>
#include <QDrag>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QFileDialog>
#include <QFont>
#include <QFormLayout>
#include <QFrame>
#include <QGridLayout>
#include <QHash>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QIcon>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMenu>
#include <QMessageBox>
#include <QMimeData>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QPixmap>
#include <QScrollArea>
#include <QSlider>
#include <QSpinBox>
#include <QStringList>
#include <QTimer>
#include <QToolButton>
#include <QTreeWidget>
#include <QVBoxLayout>
#include <QtMath>

#include <cmath>

#include "ui/ai_models.h"
#include "ui/icons.h"
#include "ui/tool_registry.h"

#include <chrono>

#include "engine/core/log.h"
#include "engine/compute/adjust.h"
#include "ui/curve_editor.h"
#include "ui/panels/shared/panel_helpers.h"

namespace pittore::ui {
namespace {


class NavigatorPanel final : public QWidget {
  public:
    NavigatorPanel(AppState* state, QWidget* parent) : QWidget(parent), state_(state) {
        auto* column = new QVBoxLayout(this);
        column->setContentsMargins(12, 12, 12, 12);
        column->setSpacing(8);

        thumbnail_ = new QLabel(this);
        thumbnail_->setMinimumHeight(110);
        thumbnail_->setAlignment(Qt::AlignCenter);
        column->addWidget(thumbnail_, 1);

        auto* row = new QHBoxLayout;
        zoomLabel_ = new QLabel(QStringLiteral("100%"), this);
        zoomLabel_->setFixedWidth(52);
        row->addWidget(zoomLabel_);
        slider_ = new QSlider(Qt::Horizontal, this);
        slider_->setRange(1, 1600);
        slider_->setValue(100);
        row->addWidget(slider_, 1);
        column->addLayout(row);

        // A brush move emits documentModified on every mouse event; rescaling
        // the flattened image per event would eat the framerate. Debounce the
        // thumbnail to ~10 Hz and always refresh on structural changes.
        throttle_ = new QTimer(this);
        throttle_->setSingleShot(true);
        throttle_->setInterval(100);
        connect(throttle_, &QTimer::timeout, this, [this] { refresh(true); });

        connect(state_, &AppState::documentModified, this,
                [this] { throttle_->start(); });
        connect(state_, &AppState::activeDocumentChanged, this,
                [this] { refresh(true); });
        connect(state_, &AppState::layersChanged, this, [this] { refresh(true); });
        refresh(true);
    }

    QSlider* zoomSlider() const { return slider_; }
    void setZoomDisplay(double zoom) {
        zoomLabel_->setText(QStringLiteral("%1%").arg(zoom * 100.0, 0, 'f', zoom < 0.1 ? 2 : 0));
        QSignalBlocker block(slider_);
        slider_->setValue(qBound(1, int(zoom * 100), 1600));
    }

  protected:
    void contextMenuEvent(QContextMenuEvent* event) override {
        QMenu menu(this);
        QAction* fitA = menu.addAction(tr("Fit on Screen"));
        fitA->setShortcut(QKeySequence(QStringLiteral("Ctrl+0")));
        QAction* actualA = menu.addAction(tr("Actual Pixels"));
        actualA->setShortcut(QKeySequence(QStringLiteral("Ctrl+1")));
        QAction* chosen = menu.exec(event->globalPos());
        if (chosen == fitA)
            setZoomDisplay(0.5);
        else if (chosen == actualA)
            setZoomDisplay(1.0);
        if (chosen)
            refresh(true);
    }

  private:
    void refresh(bool force = false) {
        DocumentItem* d = state_->activeDocument();
        if (!d || d->composite.isNull()) {
            thumbnail_->clear();
            return;
        }
        if (!force && !isVisible()) return;   // avoid layout pops while hidden
        thumbnail_->setPixmap(pixmapForWidget(
            QImage(d->composite)
                .scaled(thumbnail_->size(), Qt::KeepAspectRatio,
                        Qt::SmoothTransformation),
            thumbnail_));
    }

    AppState* state_;
    QLabel* thumbnail_ = nullptr;
    QLabel* zoomLabel_ = nullptr;
    QSlider* slider_ = nullptr;
    QTimer* throttle_ = nullptr;
};

}  // namespace

QWidget* createNavigatorPanel(AppState* state, QWidget* parent) {
    return new NavigatorPanel(state, parent);
}

}  // namespace pittore::ui
