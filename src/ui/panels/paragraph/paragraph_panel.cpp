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


class ParagraphPanel final : public QWidget {
  public:
    ParagraphPanel(AppState* state, QWidget* parent) : QWidget(parent), state_(state) {
        auto* column = new QVBoxLayout(this);
        column->setContentsMargins(12, 12, 12, 12);
        column->setSpacing(8);

        auto* alignRow = new QHBoxLayout;
        alignRow->setSpacing(4);
        const ThemeColors c = colorsFor(state_->theme());
        auto* group = new QButtonGroup(this);
        for (const char* key : {"paragraph", "paragraph", "paragraph", "paragraph",
                                "paragraph", "paragraph", "paragraph"}) {
            auto* b = new QToolButton(this);
            b->setAutoRaise(true);
            b->setCheckable(true);
            b->setIcon(chromeIcon(QString::fromUtf8(key), c.textDim, c.text));
            b->setIconSize(QSize(16, 16));
            group->addButton(b);
            alignRow->addWidget(b);
        }
        alignRow->addStretch(1);
        column->addLayout(alignRow);

        auto* form = new QFormLayout;
        form->setSpacing(6);
        for (auto&& entry : {std::pair<const char*, double>{"Indent left", 0},
                             {"Indent right", 0},
                             {"First line indent", 0},
                             {"Space before", 0},
                             {"Space after", 0}}) {
            auto* spin = new QDoubleSpinBox(this);
            spin->setRange(-10000, 10000);
            spin->setValue(entry.second);
            spin->setSuffix(QStringLiteral(" pt"));
            form->addRow(tr(entry.first), spin);
        }
        column->addLayout(form);

        auto* hyphenate = new QCheckBox(tr("Hyphenate"), this);
        column->addWidget(hyphenate);
        column->addStretch(1);
    }

  private:
    AppState* state_;
};

}  // namespace

QWidget* createParagraphPanel(AppState* state, QWidget* parent) {
    return new ParagraphPanel(state, parent);
}

}  // namespace pittore::ui
