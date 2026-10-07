// Clean-room owner-draw for font-family combos. When previewing, each row
// paints the family name in its own face (missing faces fall back silently,
// like every Qt font request); otherwise rows render exactly as before.

#include "ui/font_preview.h"

#include <QApplication>
#include <QComboBox>
#include <QCoreApplication>
#include <QFont>
#include <QFontMetrics>
#include <QPainter>
#include <QStyle>

namespace pittore::ui {

int familyPreviewPx(int setting) {
    switch (qBound(0, setting, 3)) {
        case 1: return 13;  // Small
        case 2: return 16;  // Medium
        case 3: return 22;  // Large
        case 0:
        default: return 0;  // None
    }
}

FamilyPreviewDelegate::FamilyPreviewDelegate(std::function<int()> previewPx,
                                             QObject* parent)
    : QStyledItemDelegate(parent), previewPx_(std::move(previewPx)) {}

void FamilyPreviewDelegate::paint(QPainter* painter,
                                  const QStyleOptionViewItem& option,
                                  const QModelIndex& index) const {
    QStyleOptionViewItem opt(option);
    initStyleOption(&opt, index);
    const int px = previewPx_ ? familyPreviewPx(previewPx_()) : 0;
    if (px > 0 && !opt.text.isEmpty()) {
        QFont face(opt.text);
        face.setPixelSize(px);
        opt.font = face;
        opt.text = QFontMetrics(face).elidedText(opt.text, Qt::ElideRight,
                                                 opt.rect.width());
    }
    QStyle* style = opt.widget
                        ? opt.widget->style()
                        : (QCoreApplication::instance()
                               ? QApplication::style()
                               : nullptr);
    if (style)
        style->drawControl(QStyle::CE_ItemViewItem, &opt, painter, opt.widget);
    else
        QStyledItemDelegate::paint(painter, option, index);
}

QSize FamilyPreviewDelegate::sizeHint(const QStyleOptionViewItem& option,
                                      const QModelIndex& index) const {
    QSize hint = QStyledItemDelegate::sizeHint(option, index);
    const int px = previewPx_ ? familyPreviewPx(previewPx_()) : 0;
    if (px > 0) hint.setHeight(qMax(hint.height(), px + 8));
    return hint;
}

void applyFamilyPreview(QComboBox* box, std::function<int()> previewPx) {
    if (!box) return;
    if (auto* old = qobject_cast<FamilyPreviewDelegate*>(box->itemDelegate()))
        old->deleteLater();
    box->setItemDelegate(new FamilyPreviewDelegate(std::move(previewPx), box));
}

}  // namespace pittore::ui
