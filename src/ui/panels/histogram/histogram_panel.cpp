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
// Histogram (R80)
// ---------------------------------------------------------------------------
// The histogram panel plots the flattened composite's 256-bin RGB/luminance
// distribution. RGB mode overlays the three channels; the single-channel modes
// show one trace plus the mean / median / standard deviation of that channel.
class HistogramPlot final : public QWidget {
  public:
    HistogramPlot(AppState* state, QWidget* parent) : QWidget(parent), state_(state) {
        setMinimumHeight(110);
        setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    }

    void setHistogram(const pittore::Histogram256& h) {
        hist_ = h;
        update();
    }
    // 0 = RGB overlay, 1 = R, 2 = G, 3 = B, 4 = luminosity.
    void setChannel(int channel) {
        channel_ = channel;
        update();
    }

  protected:
    void paintEvent(QPaintEvent*) override {
        QPainter p(this);
        const ThemeColors c = colorsFor(state_->theme());
        p.fillRect(rect(), c.chromeSunken);
        const QRect area = rect().adjusted(1, 1, -2, -2);
        if (area.width() < 8 || area.height() < 8) return;

        // Peak across whichever traces are drawn; a flat (empty) image would
        // divide by zero, so keep a floor.
        std::uint64_t peak = 1;
        auto consider = [&](const std::uint64_t* bins) {
            for (int i = 0; i < 256; ++i) peak = std::max(peak, bins[i]);
        };
        if (channel_ == 0 || channel_ == 1) consider(hist_.rgb[0]);
        if (channel_ == 0 || channel_ == 2) consider(hist_.rgb[1]);
        if (channel_ == 0 || channel_ == 3) consider(hist_.rgb[2]);
        if (channel_ == 0 || channel_ == 4) consider(hist_.luma);

        const double sx = area.width() / 256.0;
        const auto trace = [&](const std::uint64_t* bins, const QColor& color) {
            QPainterPath path;
            path.moveTo(area.left(), area.bottom());
            for (int i = 0; i < 256; ++i) {
                const double x = area.left() + i * sx;
                const double norm = double(bins[i]) / double(peak);
                const double y = area.bottom() - norm * (area.height() - 1);
                path.lineTo(x, y);
            }
            path.lineTo(area.right(), area.bottom());
            path.closeSubpath();
            p.fillPath(path, color);
        };

        p.setRenderHint(QPainter::Antialiasing, false);
        if (channel_ == 0) {
            trace(hist_.rgb[2], QColor(60, 120, 255, 150));
            trace(hist_.rgb[1], QColor(60, 220, 90, 150));
            trace(hist_.rgb[0], QColor(255, 70, 70, 150));
        } else if (channel_ == 1) {
            trace(hist_.rgb[0], QColor(235, 60, 60, 200));
        } else if (channel_ == 2) {
            trace(hist_.rgb[1], QColor(60, 210, 90, 200));
        } else if (channel_ == 3) {
            trace(hist_.rgb[2], QColor(70, 120, 255, 200));
        } else {
            trace(hist_.luma, QColor(215, 215, 215, 200));
        }

        p.setPen(QPen(c.border, 1));
        p.drawRect(rect().adjusted(0, 0, -1, -1));
    }

  private:
    AppState* state_;
    pittore::Histogram256 hist_;
    int channel_ = 0;
};

class HistogramPanel final : public QWidget {
  public:
    HistogramPanel(AppState* state, QWidget* parent) : QWidget(parent), state_(state) {
        auto* column = new QVBoxLayout(this);
        column->setContentsMargins(12, 12, 12, 12);
        column->setSpacing(8);

        channel_ = new QComboBox(this);
        channel_->addItems({tr("RGB"), tr("Red"), tr("Green"), tr("Blue"), tr("Luminosity")});
        column->addWidget(channel_);

        plot_ = new HistogramPlot(state, this);
        column->addWidget(plot_, 1);

        stats_ = new QLabel(this);
        stats_->setTextFormat(Qt::RichText);
        stats_->setWordWrap(true);
        stats_->setStyleSheet(
            QStringLiteral("color: %1; font-size: 11px;").arg(colorsFor(state->theme()).textDim.name()));
        column->addWidget(stats_);

        // A brush stroke marks the document modified on every event; rebuild the
        // histogram at ~12 Hz rather than per event.
        throttle_ = new QTimer(this);
        throttle_->setSingleShot(true);
        throttle_->setInterval(80);
        connect(throttle_, &QTimer::timeout, this, [this] { refresh(); });
        connect(state_, &AppState::documentModified, this, [this] { throttle_->start(); });
        connect(state_, &AppState::layersChanged, this, [this] { throttle_->start(); });
        connect(state_, &AppState::activeDocumentChanged, this, [this] { refresh(); });
        connect(channel_, &QComboBox::currentIndexChanged, this, [this](int) {
            plot_->setChannel(channel_->currentIndex());
        });
        refresh();
    }

  private:
    const std::uint64_t* binsFor(int channel) const {
        switch (channel) {
            case 1: return hist_.rgb[0];
            case 2: return hist_.rgb[1];
            case 3: return hist_.rgb[2];
            default: return hist_.luma;   // RGB overlay summarises in luminance
        }
    }

    void refresh() {
        pittore::Histogram256 h;
        if (!state_->activeHistogram(h)) {
            hist_ = pittore::Histogram256{};
            plot_->setHistogram(hist_);
            stats_->clear();
            return;
        }
        hist_ = h;
        plot_->setHistogram(hist_);
        plot_->setChannel(channel_->currentIndex());
        updateStats();
    }

    void updateStats() {
        const std::uint64_t* bins = binsFor(channel_->currentIndex());
        std::uint64_t total = 0;
        long double sum = 0.0L;
        for (int i = 0; i < 256; ++i) {
            total += bins[i];
            sum += static_cast<long double>(bins[i]) * i;
        }
        if (total == 0) {
            stats_->clear();
            return;
        }
        const double mean = static_cast<double>(sum / total);
        std::uint64_t running = 0;
        int median = 0;
        for (int i = 0; i < 256; ++i) {
            running += bins[i];
            if (running * 2 >= total) {
                median = i;
                break;
            }
        }
        long double var = 0.0L;
        for (int i = 0; i < 256; ++i) {
            const long double d = i - mean;
            var += static_cast<long double>(bins[i]) * d * d;
        }
        const double stddev = std::sqrt(static_cast<double>(var / total));
        stats_->setText(QStringLiteral(
                            "<table cellspacing='0' cellpadding='0'>"
                            "<tr><td>%1&nbsp;</td><td align='right'>%2</td>"
                            "<td>&nbsp;&nbsp;%3&nbsp;</td><td align='right'>%4</td></tr>"
                            "<tr><td>%5&nbsp;</td><td align='right'>%6</td>"
                            "<td>&nbsp;&nbsp;%7&nbsp;</td><td align='right'>%8</td></tr>"
                            "</table>")
                            .arg(tr("Mean"), QString::number(mean, 'f', 2), tr("Median"),
                                 QString::number(median), tr("Std Dev"),
                                 QString::number(stddev, 'f', 2), tr("Pixels"),
                                 QString::number(total)));
    }

    AppState* state_;
    QComboBox* channel_ = nullptr;
    HistogramPlot* plot_ = nullptr;
    QLabel* stats_ = nullptr;
    QTimer* throttle_ = nullptr;
    pittore::Histogram256 hist_;
};

}  // namespace

QWidget* createHistogramPanel(AppState* state, QWidget* parent) {
    return new HistogramPanel(state, parent);
}

}  // namespace pittore::ui
