// test_font_preview.cpp — family preview delegate: setting-to-pixel mapping,
// delegate installation, and row geometry with preview on/off. Headless
// (QT_QPA_PLATFORM=offscreen for QFont use).
#include <QApplication>
#include <QComboBox>
#include <QImage>
#include <QPainter>
#include <QStandardItemModel>

#include "test_util.h"
#include "ui/font_preview.h"

using namespace pittore::ui;

int main(int argc, char** argv) {
    QApplication app(argc, argv);

    // Mapping (+ clamping of garbage).
    CHECK(familyPreviewPx(0) == 0);
    CHECK(familyPreviewPx(1) > 0);
    CHECK(familyPreviewPx(2) > familyPreviewPx(1));
    CHECK(familyPreviewPx(3) > familyPreviewPx(2));
    CHECK(familyPreviewPx(99) == familyPreviewPx(3));
    CHECK(familyPreviewPx(-4) == 0);

    // Installation replaces any previous preview delegate.
    QComboBox box;
    box.addItems({QStringLiteral("DejaVu Sans"),
                  QStringLiteral("No Such Face Anywhere")});
    CHECK(qobject_cast<FamilyPreviewDelegate*>(box.itemDelegate()) == nullptr);
    int live = 2;
    applyFamilyPreview(&box, [&] { return live; });
    CHECK(qobject_cast<FamilyPreviewDelegate*>(box.itemDelegate()) != nullptr);
    applyFamilyPreview(&box, [&] { return live; });
    CHECK(qobject_cast<FamilyPreviewDelegate*>(box.itemDelegate()) != nullptr);

    // Geometry: preview rows are taller than plain rows.
    auto* delegate =
        qobject_cast<FamilyPreviewDelegate*>(box.itemDelegate());
    CHECK(delegate != nullptr);
    QStyleOptionViewItem option;
    option.rect = QRect(0, 0, 200, 16);
    const QModelIndex row0 = box.model()->index(0, 0);
    live = 0;
    const int plainH = delegate->sizeHint(option, row0).height();
    live = 3;
    const int previewH = delegate->sizeHint(option, row0).height();
    CHECK(previewH > plainH);

    // Paint runs clean on and off (missing faces fall back silently).
    for (int setting : {0, 3}) {
        live = setting;
        QImage canvas(200, 64, QImage::Format_ARGB32_Premultiplied);
        canvas.fill(0);
        QPainter painter(&canvas);
        for (int row = 0; row < box.count(); ++row) {
            QStyleOptionViewItem rowOpt(option);
            rowOpt.rect = QRect(0, row * 32, 200, 32);
            delegate->paint(&painter, rowOpt, box.model()->index(row, 0));
        }
        painter.end();
        CHECK(!canvas.isNull());
    }

    std::printf("  checks=%d failures=%d\n", pittore_test::checks(),
                pittore_test::failures());
    return pittore_test::failures() == 0 ? 0 : 1;
}
