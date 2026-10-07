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
// AI Models
// ---------------------------------------------------------------------------
// Catalogue + download manager for the on-device background-removal models.
// Clicking a row makes it the active model (persisted in Settings); the footer
// buttons download or delete weights. Nothing is bundled with the build, so a
// fresh install starts with an empty cache and downloads on demand.
class AiModelsPanel final : public QWidget {
  public:
    AiModelsPanel(AppState* state, QWidget* parent) : QWidget(parent), state_(state) {
        store_ = &aiModelStore();  // shared with the Preferences AI tab

        auto* column = new QVBoxLayout(this);
        column->setContentsMargins(0, 4, 0, 0);
        column->setSpacing(8);
        column->addWidget(sectionLabel(
            tr("Model used by Remove Background. Click a model to select it."),
            state_, this));

        tree_ = new QTreeWidget(this);
        tree_->setColumnCount(4);
        tree_->setHeaderLabels({tr("Model"), tr("Size"), tr("License"), tr("Status")});
        tree_->setRootIsDecorated(false);
        tree_->setUniformRowHeights(true);
        tree_->setSelectionMode(QAbstractItemView::SingleSelection);
        tree_->header()->setStretchLastSection(true);
        tree_->setColumnWidth(0, 180);
        tree_->setColumnWidth(1, 62);
        tree_->setColumnWidth(2, 82);
        column->addWidget(tree_, 1);

        QString category;
        for (const AiModel& m : allAiModels()) {
            if (m.category != category) {
                category = m.category;
                auto* heading = new QTreeWidgetItem(tree_, {category});
                heading->setFirstColumnSpanned(true);
                heading->setFlags(Qt::ItemIsEnabled);
                QFont f = heading->font(0);
                f.setBold(true);
                heading->setFont(0, f);
                heading->setForeground(0, colorsFor(state_->theme()).textDim);
            }
            auto* item = new QTreeWidgetItem(tree_, {m.name});
            item->setData(0, Qt::UserRole, m.id);
            item->setToolTip(0, m.description);
            const qint64 total = m.decoderBytes > 0
                                     ? m.bytes + m.decoderBytes
                                     : m.bytes;
            item->setText(1, formatModelSize(total));
            item->setText(2, m.license);
            rows_.insert(m.id, item);
        }

        column->addWidget(makePanelFooter(
            state_,
            {{QStringLiteral("check"), tr("Use selected")},
             {QStringLiteral("import"), tr("Download selected")},
             {QStringLiteral("sparkle"), tr("Download all")},
             {QStringLiteral("trash"), tr("Delete selected")}},
            this, [this](const QString& id) { onFooter(id); }));

        connect(tree_, &QTreeWidget::itemClicked, this,
                [this](QTreeWidgetItem* item, int) {
                    const QString id = item->data(0, Qt::UserRole).toString();
                    if (!id.isEmpty()) setActive(id);
                });
        connect(store_, &AiModelStore::changed, this, [this] { refresh(); });
        connect(store_, &AiModelStore::progress, this,
                [this](const QString& id, qint64 received, qint64 total) {
                    QTreeWidgetItem* item = rows_.value(id, nullptr);
                    if (!item) return;
                    const int pct = total > 0 ? int(received * 100 / total) : 0;
                    item->setText(3, tr("Downloading %1%").arg(pct));
                });
        connect(state_, &AppState::settingsChanged, this, [this] { refresh(); });

        refresh();
    }

  private:
    QString currentId() const {
        QTreeWidgetItem* item = tree_->currentItem();
        return item ? item->data(0, Qt::UserRole).toString() : QString();
    }

    void setActive(const QString& id) {
        if (state_->settings().bgModel == id) return;
        AppSettings next = state_->settings();
        next.bgModel = id;
        state_->applySettings(next);
        state_->setStatusHint(tr("Background removal model: %1")
                                  .arg(aiModel(id) ? aiModel(id)->name : id));
    }

    void onFooter(const QString& action) {
        const QString id = currentId();
        if (action == QStringLiteral("check")) {
            if (!id.isEmpty()) setActive(id);
        } else if (action == QStringLiteral("import")) {
            if (id.isEmpty()) return;
            const AiModel* m = aiModel(id);
            if (m && m->url.isEmpty()) {
                if (store_->state(id) == AiModelState::Present) {
                    store_->remove(id);
                    return;
                }
                const QString enc = QFileDialog::getOpenFileName(
                    this, tr("Select the SAM encoder model (.onnx)"),
                    QDir::homePath(), tr("ONNX model (*.onnx)"));
                if (enc.isEmpty()) return;
                const QString err = store_->importPairFiles(id, enc);
                if (!err.isEmpty())
                    QMessageBox::warning(this, tr("Object Select model"), err);
            } else {
                store_->download(id);
            }
        } else if (action == QStringLiteral("sparkle")) {
            store_->downloadAll();
        } else if (action == QStringLiteral("trash")) {
            if (!id.isEmpty()) store_->remove(id);
        }
    }

    QString statusText(const QString& id) const {
        const AiModel* m = aiModel(id);
        if (m && m->url.isEmpty()) {
            switch (store_->state(id)) {
                case AiModelState::Present: return tr("Ready (local)");
                case AiModelState::Absent:
                    return tr("Local only — copy .onnx files in");
                default: break;
            }
        }
        switch (store_->state(id)) {
            case AiModelState::Downloading: return tr("Downloading…");
            case AiModelState::Present: return tr("Ready");
            case AiModelState::Error: return tr("Error: %1").arg(store_->errorText(id));
            case AiModelState::Absent: return tr("Not downloaded");
        }
        return QString();
    }

    void refresh() {
        const QString active = state_->settings().bgModel;
        for (auto it = rows_.begin(); it != rows_.end(); ++it) {
            QTreeWidgetItem* item = it.value();
            const bool isActive = it.key() == active;
            QFont f = item->font(0);
            f.setBold(isActive);
            item->setFont(0, f);
            const QString status = statusText(it.key());
            item->setText(3, isActive ? tr("In use · %1").arg(status) : status);
        }
    }

    AppState* state_;
    AiModelStore* store_ = nullptr;
    QTreeWidget* tree_ = nullptr;
    QHash<QString, QTreeWidgetItem*> rows_;
};

}  // namespace

QWidget* createAiModelsPanel(AppState* state, QWidget* parent) {
    return new AiModelsPanel(state, parent);
}

}  // namespace pittore::ui
