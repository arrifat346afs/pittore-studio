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
#include "ui/persona/vector_path_ops.h"

namespace pittore::ui {
namespace {


class PathsPanel final : public QWidget {
  public:
    PathsPanel(AppState* state, QWidget* parent) : QWidget(parent), state_(state) {
        auto* column = new QVBoxLayout(this);
        column->setContentsMargins(0, 4, 0, 0);
        column->setSpacing(8);
        list_ = new QListWidget(this);
        list_->addItem(tr("Work Path"));
        column->addWidget(list_, 1);
        column->addWidget(makePanelFooter(
            state_,
            {{QStringLiteral("brush"), tr("Fill path with foreground colour")},
             {QStringLiteral("pen"), tr("Stroke path with brush")},
             {QStringLiteral("marquee-rect"), tr("Load path as selection")},
             {QStringLiteral("paths"), tr("Make work path from selection")},
             {QStringLiteral("mask"), tr("Add layer mask")},
             {QStringLiteral("newlayer"), tr("Create new path")},
             {QStringLiteral("trash"), tr("Delete path")}},
            this,
            [this](const QString& id) {
                // Shared with the task bar and runCommand (vector_path_ops):
                // fill/stroke/selection work on the editable art layer today,
                // work-path storage is still planned.
                if (id == QLatin1String("brush")) {
                    vectorFillActiveShape(state_, state_->foreground(),
                                          tr("Fill Path"));
                } else if (id == QLatin1String("pen")) {
                    vectorStrokeActiveShape(state_, state_->foreground(),
                                            tr("Stroke Path"));
                } else if (id == QLatin1String("marquee-rect")) {
                    vectorPathToSelection(state_);
                } else if (id == QLatin1String("mask")) {
                    state_->addLayerMask(1.0f);
                } else {
                    vectorPlannedHint(state_, tr("Work paths"));
                }
            }));
    }

  private:
    AppState* state_;
    QListWidget* list_ = nullptr;
};

}  // namespace

QWidget* createPathsPanel(AppState* state, QWidget* parent) {
    return new PathsPanel(state, parent);
}

}  // namespace pittore::ui
