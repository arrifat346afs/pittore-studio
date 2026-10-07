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
// Info / Navigator
// ---------------------------------------------------------------------------
class InfoPanel final : public QWidget {
  public:
    InfoPanel(AppState* state, QWidget* parent) : QWidget(parent), state_(state) {
        auto* grid = new QGridLayout(this);
        grid->setContentsMargins(12, 12, 12, 12);
        grid->setSpacing(6);
        const char* labels[] = {"R", "G", "B", "X", "Y", "W", "H"};
        for (int i = 0; i < 7; ++i) {
            grid->addWidget(sectionLabel(QString::fromUtf8(labels[i]), state_, this), i / 4,
                            (i % 4) * 2);
            values_[i] = new QLabel(QStringLiteral("—"), this);
            grid->addWidget(values_[i], i / 4, (i % 4) * 2 + 1);
        }
        // Color Sampler readouts: one row per pin, live from the composite.
        grid->addWidget(sectionLabel(tr("Samples"), state_, this), 2, 0, 1, 4);
        for (int i = 0; i < 4; ++i) {
            grid->addWidget(sectionLabel(QString::number(i + 1), state_, this), 3 + i, 0);
            sampleLabels_[i] = new QLabel(QStringLiteral("—"), this);
            grid->addWidget(sampleLabels_[i], 3 + i, 1, 1, 3);
        }
        grid->setRowStretch(7, 1);

        connect(state_, &AppState::colorsChanged, this, [this] { update(); });
        connect(state_, &AppState::cursorInfoChanged, this,
                [this](QPointF pos, const QColor& color) { setCursorInfo(pos, color); });
        auto refresh = [this] { refreshSamples(); };
        connect(state_, &AppState::annotationsChanged, this, refresh);
        connect(state_, &AppState::documentModified, this, refresh);
        connect(state_, &AppState::activeDocumentChanged, this, refresh);
        refreshSamples();
    }

    void setCursorInfo(QPointF documentPosition, const QColor& color) {
        const int rgb[3] = {color.red(), color.green(), color.blue()};
        for (int i = 0; i < 3; ++i) values_[i]->setText(QString::number(rgb[i]));
        values_[3]->setText(QString::number(int(documentPosition.x())));
        values_[4]->setText(QString::number(int(documentPosition.y())));
        if (DocumentItem* d = state_->activeDocument()) {
            values_[5]->setText(QString::number(int(d->selection.width())));
            values_[6]->setText(QString::number(int(d->selection.height())));
        }
    }

  private:
    // Re-read the Color Sampler pins from the active document and composite.
    void refreshSamples() {
        DocumentItem* d = state_->activeDocument();
        const int sizeIdx =
            state_->option(ToolId::ColorSampler, QStringLiteral("sample_size")).toInt();
        for (int i = 0; i < 4; ++i) {
            if (!d || i >= d->colorSamples.size()) {
                sampleLabels_[i]->setText(QStringLiteral("—"));
                continue;
            }
            const QColor c = sampleCompositeColor(*d, d->colorSamples[i], sizeIdx);
            if (!c.isValid()) {
                sampleLabels_[i]->setText(QStringLiteral("—"));
                continue;
            }
            sampleLabels_[i]->setText(
                QStringLiteral("%1, %2, %3   (%4, %5)")
                    .arg(c.red())
                    .arg(c.green())
                    .arg(c.blue())
                    .arg(int(d->colorSamples[i].x()))
                    .arg(int(d->colorSamples[i].y())));
        }
    }

    AppState* state_;
    QLabel* values_[7]{};
    QLabel* sampleLabels_[4]{};
};

}  // namespace

QWidget* createInfoPanel(AppState* state, QWidget* parent) {
    return new InfoPanel(state, parent);
}

}  // namespace pittore::ui
