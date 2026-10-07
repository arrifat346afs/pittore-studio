#pragma once
// Font-family preview for family combos (Type tool options bar, Character
// panel): each family renders in its own face at the Settings preview size,
// conventional. The delegate reads the size through a provider, so it
// stays live with zero update wiring; tests inject a constant.

#include <QStyledItemDelegate>

#include <functional>

class QComboBox;

namespace pittore::ui {

// Preview-size setting (Settings > Interface) to pixels. 0 is None/off;
// unknown values clamp into range.
int familyPreviewPx(int setting);

class FamilyPreviewDelegate : public QStyledItemDelegate {
    Q_OBJECT

  public:
    explicit FamilyPreviewDelegate(std::function<int()> previewPx,
                                   QObject* parent = nullptr);

    void paint(QPainter* painter, const QStyleOptionViewItem& option,
               const QModelIndex& index) const override;
    QSize sizeHint(const QStyleOptionViewItem& option,
                   const QModelIndex& index) const override;

  private:
    std::function<int()> previewPx_;
};

// Installs live family preview on a family combo (idempotent: replaces any
// previous FamilyPreviewDelegate on the box).
void applyFamilyPreview(QComboBox* box, std::function<int()> previewPx);

}  // namespace pittore::ui
