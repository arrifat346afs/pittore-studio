#include "ui/spotlight.h"

#include <QKeyEvent>
#include <QLineEdit>
#include <QListWidget>
#include <QVBoxLayout>

namespace pittore::ui {

SpotlightDialog::SpotlightDialog(QVector<SpotlightEntry> entries, QWidget *parent)
    : QDialog(parent), all_(std::move(entries))
{
    setWindowTitle(tr("Quick Search"));
    resize(560, 480);
    auto *column = new QVBoxLayout(this);
    search_ = new QLineEdit(this);
    search_->setPlaceholderText(tr("Tools, commands, filters, panels…"));
    search_->setClearButtonEnabled(true);
    column->addWidget(search_);
    list_ = new QListWidget(this);
    list_->setUniformItemSizes(true);
    column->addWidget(list_, 1);
    connect(search_, &QLineEdit::textChanged, this, [this](const QString &q) { rebuild(q); });
    connect(list_, &QListWidget::itemActivated, this, [this](QListWidgetItem *item) {
        const int idx = item->data(Qt::UserRole).toInt();
        if (idx >= 0 && idx < all_.size() && all_.at(idx).run) {
            auto fn = all_.at(idx).run;
            accept();
            fn();
        }
    });
    rebuild(QString());
}

void SpotlightDialog::setEntries(QVector<SpotlightEntry> entries)
{
    all_ = std::move(entries);
    rebuild(search_ ? search_->text() : QString());
}

void SpotlightDialog::rebuild(const QString &query)
{
    list_->clear();
    const QString q = query.trimmed().toLower();
    for (int i = 0; i < all_.size(); ++i) {
        const SpotlightEntry &e = all_.at(i);
        if (!q.isEmpty()) {
            const QString hay = (e.group + QStringLiteral(" ") + e.title + QStringLiteral(" ") + e.hint).toLower();
            bool ok = true;
            for (const QString &part : q.split(u' ', Qt::SkipEmptyParts)) {
                if (!hay.contains(part)) {
                    ok = false;
                    break;
                }
            }
            if (!ok)
                continue;
        }
        QString label = e.title;
        if (!e.hint.isEmpty())
            label += QStringLiteral("  (") + e.hint + QStringLiteral(")");
        if (!e.group.isEmpty())
            label = e.group + QStringLiteral(": ") + label;
        auto *item = new QListWidgetItem(label, list_);
        item->setData(Qt::UserRole, i);
    }
    if (list_->count() > 0)
        list_->setCurrentRow(0);
}

}
