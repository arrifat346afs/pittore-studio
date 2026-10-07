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


class ActionsPanel final : public QWidget {
  public:
    ActionsPanel(AppState* state, QWidget* parent) : QWidget(parent), state_(state) {
        auto* column = new QVBoxLayout(this);
        column->setContentsMargins(0, 4, 0, 0);
        auto* tree = new QTreeWidget(this);
        tree->setHeaderHidden(true);
        auto* set = new QTreeWidgetItem(tree, {tr("Default Actions")});
        for (const char* name : {"Vignette (selection)", "Frame Channel — 50 pixel",
                                 "Wood Frame — 50 pixel", "Cast Shadow (type)",
                                 "Water Reflection (type)", "Custom RGB to Grayscale"})
            new QTreeWidgetItem(set, {QString::fromUtf8(name)});
        set->setExpanded(true);
        column->addWidget(tree, 1);
        column->addWidget(makePanelFooter(
            state_,
            {{QStringLiteral("close"), tr("Stop")},
             {QStringLiteral("count"), tr("Record")},
             {QStringLiteral("actions"), tr("Play")},
             {QStringLiteral("group"), tr("New set")},
             {QStringLiteral("newlayer"), tr("New action")},
             {QStringLiteral("trash"), tr("Delete")}},
            this, [](const QString&) {}));
    }

  private:
    AppState* state_;
};

}  // namespace

QWidget* createActionsPanel(AppState* state, QWidget* parent) {
    return new ActionsPanel(state, parent);
}

}  // namespace pittore::ui
