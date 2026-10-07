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


// ---------------------------------------------------------------------------
// Color
// ---------------------------------------------------------------------------
class ColorField final : public QWidget {
  public:
    explicit ColorField(AppState* state, QWidget* parent) : QWidget(parent), state_(state) {
        setMinimumHeight(88);
        setCursor(Qt::CrossCursor);
        connect(state_, &AppState::colorsChanged, this, qOverload<>(&QWidget::update));
    }

  protected:
    void paintEvent(QPaintEvent*) override {
        QPainter p(this);
        // Saturation/value field for the current hue, with a hue strip beneath.
        const int hue = state_->foreground().hue() < 0 ? 0 : state_->foreground().hue();
        const QRect field = rect().adjusted(0, 0, 0, -14);
        QImage image(field.size(), QImage::Format_RGB32);
        for (int y = 0; y < field.height(); ++y)
            for (int x = 0; x < field.width(); ++x)
                image.setPixelColor(x, y,
                                    QColor::fromHsv(hue, x * 255 / qMax(1, field.width() - 1),
                                                    255 - y * 255 / qMax(1, field.height() - 1)));
        p.drawImage(field, image);

        const QRect strip(0, height() - 12, width(), 12);
        QLinearGradient hues(strip.topLeft(), strip.topRight());
        for (int i = 0; i <= 6; ++i) hues.setColorAt(i / 6.0, QColor::fromHsv(i * 60 % 360, 255, 255));
        p.fillRect(strip, hues);

        // Marker.
        const QColor fg = state_->foreground();
        const QPointF marker(field.width() * fg.saturationF(),
                             field.height() * (1.0 - fg.valueF()));
        p.setPen(QPen(Qt::white, 1.4));
        p.setBrush(Qt::NoBrush);
        p.drawEllipse(marker, 4, 4);
        p.setPen(QPen(Qt::black, 1));
        p.drawEllipse(marker, 5.5, 5.5);
    }

    void mousePressEvent(QMouseEvent* event) override { pick(event); }
    void mouseMoveEvent(QMouseEvent* event) override {
        if (event->buttons() & Qt::LeftButton) pick(event);
    }

  private:
    void pick(QMouseEvent* event) {
        const QPointF pos = event->position();
        if (pos.y() >= height() - 12) {
            const int hue = qBound(0, int(pos.x() * 359 / qMax(1, width() - 1)), 359);
            const QColor fg = state_->foreground();
            state_->setForeground(QColor::fromHsv(hue, qMax(1, fg.saturation()),
                                                  qMax(1, fg.value())));
            return;
        }
        const QRect field = rect().adjusted(0, 0, 0, -14);
        const int hue = state_->foreground().hue() < 0 ? 0 : state_->foreground().hue();
        state_->setForeground(QColor::fromHsv(
            hue, qBound(0, int(pos.x() * 255 / qMax(1, field.width() - 1)), 255),
            qBound(0, 255 - int(pos.y() * 255 / qMax(1, field.height() - 1)), 255)));
    }

    AppState* state_;
};

class ColorPanel final : public QWidget {
  public:
    ColorPanel(AppState* state, QWidget* parent) : QWidget(parent), state_(state) {
        auto* column = new QVBoxLayout(this);
        column->setContentsMargins(12, 12, 12, 12);
        column->setSpacing(8);

        column->addWidget(new ColorField(state_, this));

        auto* grid = new QGridLayout;
        grid->setSpacing(6);
        const char* names[] = {"R", "G", "B"};
        for (int i = 0; i < 3; ++i) {
            grid->addWidget(sectionLabel(QString::fromUtf8(names[i]), state_, this), i, 0);
            sliders_[i] = new QSlider(Qt::Horizontal, this);
            sliders_[i]->setRange(0, 255);
            grid->addWidget(sliders_[i], i, 1);
            spins_[i] = new QSpinBox(this);
            spins_[i]->setRange(0, 255);
            spins_[i]->setFixedWidth(52);
            grid->addWidget(spins_[i], i, 2);
            connect(sliders_[i], &QSlider::valueChanged, this, [this] { pushFromChannels(); });
            connect(spins_[i], &QSpinBox::valueChanged, this, [this, i](int v) {
                QSignalBlocker block(sliders_[i]);
                sliders_[i]->setValue(v);
                pushFromChannels();
            });
        }
        column->addLayout(grid);

        auto* hexRow = new QHBoxLayout;
        hexRow->addWidget(sectionLabel(QStringLiteral("#"), state_, this));
        hex_ = new QLineEdit(this);
        hex_->setMaxLength(7);
        hexRow->addWidget(hex_);
        column->addLayout(hexRow);
        connect(hex_, &QLineEdit::editingFinished, this, [this] {
            QColor c(hex_->text().startsWith('#') ? hex_->text() : '#' + hex_->text());
            if (c.isValid()) state_->setForeground(c);
        });

        column->addStretch(1);
        connect(state_, &AppState::colorsChanged, this, [this] { pullFromState(); });
        pullFromState();
    }

  protected:
    void contextMenuEvent(QContextMenuEvent* event) override {
        QMenu menu(this);
        QAction* swapA = menu.addAction(tr("Swap Colors"));
        swapA->setShortcut(QKeySequence(QStringLiteral("X")));
        QAction* resetA = menu.addAction(tr("Reset Colors"));
        resetA->setShortcut(QKeySequence(QStringLiteral("D")));
        QAction* chosen = menu.exec(event->globalPos());
        if (chosen == swapA)
            state_->swapColors();
        else if (chosen == resetA)
            state_->resetColors();
    }

  private:
    void pullFromState() {
        const QColor c = state_->foreground();
        const int values[3] = {c.red(), c.green(), c.blue()};
        for (int i = 0; i < 3; ++i) {
            QSignalBlocker b1(sliders_[i]), b2(spins_[i]);
            sliders_[i]->setValue(values[i]);
            spins_[i]->setValue(values[i]);
        }
        QSignalBlocker b(hex_);
        hex_->setText(c.name().toUpper());
    }

    void pushFromChannels() {
        const QColor c(sliders_[0]->value(), sliders_[1]->value(), sliders_[2]->value());
        state_->setForeground(c);
    }

    AppState* state_;
    QSlider* sliders_[3]{};
    QSpinBox* spins_[3]{};
    QLineEdit* hex_ = nullptr;
};

}  // namespace

QWidget* createColorPanel(AppState* state, QWidget* parent) {
    return new ColorPanel(state, parent);
}

}  // namespace pittore::ui
