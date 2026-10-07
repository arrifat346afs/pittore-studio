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
// History
// ---------------------------------------------------------------------------
class HistoryPanel final : public QWidget {
  public:
    HistoryPanel(AppState* state, QWidget* parent) : QWidget(parent), state_(state) {
        auto* column = new QVBoxLayout(this);
        column->setContentsMargins(0, 4, 0, 0);
        column->setSpacing(8);
        list_ = new QListWidget(this);
        list_->setIconSize(QSize(16, 16));
        column->addWidget(list_, 1);
        column->addWidget(makePanelFooter(
            state_,
            {{QStringLiteral("history"), tr("Create snapshot")},
             {QStringLiteral("newlayer"), tr("Create new document from current state")},
             {QStringLiteral("trash"), tr("Delete state")}},
            this, [](const QString&) {}));

        connect(state_, &AppState::historyChanged, this, [this] { rebuild(); });
        connect(state_, &AppState::activeDocumentChanged, this, [this] { rebuild(); });
        connect(list_, &QListWidget::currentRowChanged, this, [this](int row) {
            if (DocumentItem* d = state_->activeDocument())
                if (row >= 0) d->historyPosition = row;
        });
        rebuild();
    }

  private:
    void rebuild() {
        list_->clear();
        DocumentItem* d = state_->activeDocument();
        if (!d) return;
        const ThemeColors c = colorsFor(state_->theme());
        for (const HistoryItem& item : d->history) {
            auto* row = new QListWidgetItem(
                chromeIcon(item.iconKey, c.text, c.accentText), item.name);
            list_->addItem(row);
        }
        QSignalBlocker block(list_);
        list_->setCurrentRow(d->historyPosition);
    }

  protected:
    void contextMenuEvent(QContextMenuEvent* event) override {
        QMenu menu(this);
        QAction* undoA = menu.addAction(tr("Undo"));
        undoA->setShortcut(QKeySequence(QStringLiteral("Ctrl+Z")));
        undoA->setEnabled(state_->canUndo());
        QAction* redoA = menu.addAction(tr("Redo"));
        redoA->setShortcut(QKeySequence(QStringLiteral("Ctrl+Shift+Z")));
        redoA->setEnabled(state_->canRedo());
        QAction* chosen = menu.exec(event->globalPos());
        if (chosen == undoA)
            state_->undo();
        else if (chosen == redoA)
            state_->redo();
    }

  private:
    AppState* state_;
    QListWidget* list_ = nullptr;
};

}  // namespace

QWidget* createHistoryPanel(AppState* state, QWidget* parent) {
    return new HistoryPanel(state, parent);
}

}  // namespace pittore::ui
