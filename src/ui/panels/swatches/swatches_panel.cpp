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
// Swatches
// ---------------------------------------------------------------------------
class SwatchesPanel final : public QWidget {
  public:
    SwatchesPanel(AppState* state, QWidget* parent) : QWidget(parent), state_(state) {
        setMinimumHeight(90);
        setMouseTracking(true);
        connect(state_, &AppState::swatchesChanged, this, qOverload<>(&QWidget::update));
        connect(state_, &AppState::themeChanged, this, qOverload<>(&QWidget::update));
    }

  protected:
    void paintEvent(QPaintEvent*) override {
        QPainter p(this);
        const ThemeColors c = colorsFor(state_->theme());
        p.fillRect(rect(), c.chrome);
        const auto& swatches = state_->swatches();
        const int cell = 19;
        const int columns = qMax(1, (width() - 8) / cell);
        for (int i = 0; i < swatches.size(); ++i) {
            const int x = 4 + (i % columns) * cell;
            const int y = 4 + (i / columns) * cell;
            p.fillRect(QRect(x, y, cell - 2, cell - 2), swatches[i]);
            p.setPen(QPen(c.border, 1));
            p.drawRect(QRect(x, y, cell - 2, cell - 2));
        }
    }

    void mousePressEvent(QMouseEvent* event) override {
        const int cell = 19;
        const int columns = qMax(1, (width() - 8) / cell);
        const int column = (int(event->position().x()) - 4) / cell;
        const int row = (int(event->position().y()) - 4) / cell;
        const int index = row * columns + column;
        if (index < 0 || index >= state_->swatches().size()) return;
        const QColor picked = state_->swatches().at(index);
        if (event->modifiers().testFlag(Qt::AltModifier)) state_->setBackground(picked);
        else state_->setForeground(picked);
    }

  private:
    AppState* state_;
};

}  // namespace

QWidget* createSwatchesPanel(AppState* state, QWidget* parent) {
    return new SwatchesPanel(state, parent);
}

}  // namespace pittore::ui
