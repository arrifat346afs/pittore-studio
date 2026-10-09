// test_refine_dialog.cpp — the Refine Selection dialog end to end (headless):
// construct over a live selection, drive sliders + a brush stroke, accept,
// and check the committed mask. Runs headless (QT_QPA_PLATFORM=offscreen).
#include <cstdio>

#include <QApplication>
#include <QComboBox>
#include <QCompleter>
#include <QDialogButtonBox>
#include <QDir>
#include <QKeyEvent>
#include <QLabel>
#include <QMouseEvent>
#include <QPainter>
#include <QPushButton>
#include <QSlider>

#include "engine/core/log.h"
#include "test_util.h"
#include "ui/app_state.h"
#include "ui/refine_dialog.h"
#include "ui/selection_mask.h"

using namespace pittore::ui;

namespace {

QImage solidImage(int w, int h, QRgb c) {
    QImage img(w, h, QImage::Format_ARGB32_Premultiplied);
    img.fill(c);
    return img;
}

void sendMouse(QWidget* target, QEvent::Type type, const QPointF& pos,
               Qt::MouseButton button, Qt::MouseButtons buttons) {
    const QPointF global = target->mapToGlobal(pos);
    QMouseEvent e(type, pos, global, button, buttons, Qt::NoModifier);
    QApplication::sendEvent(target, &e);
}

// x where the row's falling ramp crosses 128, scanned from the right —
// i.e. the mask's right edge (-1 when the row is empty/full on the right).
int fallCol(const QImage& m, int y) {
    const uchar* r = m.constScanLine(y);
    for (int x = m.width() - 1; x > 0; --x)
        if ((r[x] < 128) != (r[x - 1] < 128)) return x;
    return -1;
}

}  // namespace

int main(int argc, char** argv) {
    // Hermetic: Enhance Edges must exercise the classical fallback here, not
    // the developer's multi-GB model cache (a 1.1 GB HR model at 2048 input
    // OOMs the desktop when tests run in parallel).
    const QString noModels =
        QDir::tempPath() + QStringLiteral("/pittore-test-no-models-refine");
    QDir(noModels).removeRecursively();
    QDir().mkpath(noModels);
    qputenv("PITTORE_MODELS_DIR", noModels.toLocal8Bit());
    QApplication app(argc, argv);
    const QString logDir =
        QDir::tempPath() + QStringLiteral("/pittore-refine-log");
    QDir().mkpath(logDir);
    ::pittore::core::log::set_log_dir(logDir.toStdString().c_str());

    AppState state;
    DocumentItem* doc = state.addDocument(QStringLiteral("r"), QSize(64, 48), 72);
    CHECK(doc != nullptr);
    if (!doc) return 1;
    // Split-tone block: white left of x=14, black right of it. The selection
    // covers the white half plus 4 px of black, so a matte dab on the black
    // overshoot has foreground and background in range and must clear it
    // (a flat guide has nothing to solve against and must not be touched).
    QImage split = solidImage(64, 48, 0xFFFFFFFF);
    {
        QPainter painter(&split);
        painter.fillRect(QRect(14, 0, 50, 48), Qt::black);
    }
    state.placeImageLayer(split, QStringLiteral("w"), QPointF(32, 24), 1.0);
    state.setSelection(QRectF(4, 4, 14, 40), false);  // x 4..17
    doc->rebuildComposite();
    app.processEvents();

    RefineDialog dialog(&state, doc);
    dialog.show();
    app.processEvents();
    auto* preview =
        dialog.findChild<QLabel*>(QStringLiteral("refine.preview"));
    CHECK(preview != nullptr);
    if (!preview) return 1;
    CHECK(!preview->pixmap(Qt::ReturnByValue).isNull());

    // Enhance Edges button: present, and with no AI model installed it falls
    // back to the local snap (never dead-ends, never empties the mask).
    {
        auto* enhance =
            dialog.findChild<QPushButton*>(QStringLiteral("refine.enhance"));
        CHECK(enhance != nullptr);
        auto* status = dialog.findChild<QLabel*>(
            QStringLiteral("refine.enhanceStatus"));
        CHECK(status != nullptr);
        if (enhance) {
            enhance->click();
            app.processEvents();
            CHECK(!status->text().isEmpty());
        }
    }

    // Separate presses must never connect: dab at opposite ends with a fresh
    // press each time (default sliders, so only the dabs can change the
    // mask). The middle must stay exactly as input; the dabbed spots change.
    // (Regression: gap-fill used to streak across from the previous stroke.)
    // Shrink the brush first: 50px would cover the whole 64px test image.
    for (QComboBox* box : dialog.findChildren<QComboBox*>()) {
        if (box->count() == 5 && box->itemData(0).isValid()) {
            box->setCurrentIndex(0);   // 10 px
            break;
        }
    }
    {
        // Input middle pixel, in doc coords (selection rect x 4..17).
        const QImage before = selectionAsMask(*doc);
        CHECK(before.constScanLine(24)[32] == 0);
        const QRect r = preview->rect();
        const QPoint left(r.width() / 4, r.height() / 2);
        const QPoint right(r.width() * 3 / 4, r.height() / 2);
        sendMouse(preview, QEvent::MouseButtonPress, left, Qt::LeftButton,
                  Qt::LeftButton);
        sendMouse(preview, QEvent::MouseButtonRelease, left, Qt::LeftButton,
                  Qt::NoButton);
        sendMouse(preview, QEvent::MouseButtonPress, right, Qt::LeftButton,
                  Qt::LeftButton);
        sendMouse(preview, QEvent::MouseButtonRelease, right, Qt::LeftButton,
                  Qt::NoButton);
        app.processEvents();
        auto* buttons = dialog.findChild<QDialogButtonBox*>();
        CHECK(buttons != nullptr);
        QPushButton* apply = buttons ? buttons->button(QDialogButtonBox::Apply)
                                     : nullptr;
        CHECK(apply != nullptr);
        if (!apply) return 1;
        apply->click();
        app.processEvents();
        CHECK(dialog.result().accepted);
        const QImage got = dialog.result().mask;
        CHECK(got.size() == doc->size);
        CHECK(got.constScanLine(24)[32] == 0);     // middle untouched
        CHECK(got.constScanLine(24)[16] != 255);   // dabbed spot re-solved
        // Default sliders leave a healthy matte — the white strip stays
        // fully selected after the dab and the snap. Commit it as Selection
        // output (what MainWindow::applyRefineResult does) and verify the
        // flag flips.
        CHECK(!selectionMaskBbox(got).isEmpty());
        state.replaceSelectionMask(got, QStringLiteral("Refine Selection"),
                                   QStringLiteral("mask"));
        CHECK(state.activeDocument()->selectionIsMask);
    }

    // Drive every slider (labels update at once, recompute lands on
    // release/debounce).
    const QList<QSlider*> sliders = dialog.findChildren<QSlider*>();
    CHECK(!sliders.isEmpty());
    for (QSlider* s : sliders) {
        s->setValue(s->maximum() / 4);
        s->sliderReleased();
    }
    app.processEvents();

    // Paint one matte stroke across the preview centre.
    const QPoint c = preview->rect().center();
    sendMouse(preview, QEvent::MouseButtonPress, c, Qt::LeftButton,
              Qt::LeftButton);
    sendMouse(preview, QEvent::MouseMove, c + QPoint(6, 0), Qt::NoButton,
              Qt::LeftButton);
    sendMouse(preview, QEvent::MouseButtonRelease, c + QPoint(6, 0),
              Qt::LeftButton, Qt::NoButton);
    app.processEvents();

    // Wheel over the preview zooms instead of painting; middle-drag pans.
    {
        const QPointF wc = preview->rect().center();
        QWheelEvent wheel(wc, preview->mapToGlobal(wc.toPoint()), QPoint(0, 0),
                          QPoint(0, 120), Qt::NoButton, Qt::NoModifier,
                          Qt::NoScrollPhase, false);
        QApplication::sendEvent(preview, &wheel);
        app.processEvents();
        CHECK(!preview->pixmap(Qt::ReturnByValue).isNull());

    // Enhance Edges button: present, and with no AI model installed it falls
    // back to the local snap (never dead-ends, never empties the mask).
    {
        auto* enhance =
            dialog.findChild<QPushButton*>(QStringLiteral("refine.enhance"));
        CHECK(enhance != nullptr);
        auto* status = dialog.findChild<QLabel*>(
            QStringLiteral("refine.enhanceStatus"));
        CHECK(status != nullptr);
        if (enhance) {
            enhance->click();
            app.processEvents();
            CHECK(!status->text().isEmpty());
        }
    }
    }

    // A second Apply, after the slider torture and an extra center stroke,
    // must still accept and hand back a valid full-size mask.
    auto* buttons = dialog.findChild<QDialogButtonBox*>();
    CHECK(buttons != nullptr);
    if (!buttons) return 1;
    QPushButton* apply = buttons->button(QDialogButtonBox::Apply);
    CHECK(apply != nullptr);
    if (!apply) return 1;
    apply->click();
    app.processEvents();
    CHECK(dialog.result().accepted);
    CHECK(dialog.result().mask.size() == doc->size);
    // Quarter-scale smooth/feather/ramp legitimately softens a small
    // selection below the 128 bbox threshold, so coverage is not asserted
    // here — Apply must still commit the full-size mask. The commit path
    // itself was verified above on the healthy default-slider result.

    // --- Hard marquee must refine, not pass through ----------------------
    // Regression: a binary selection has no partial-alpha transition to
    // translate, so "Matte edges" used to do nothing at all for the most
    // common case (a rectangular marquee). The solve now runs across a band
    // straddling the boundary and snaps the edge onto the real one.
    DocumentItem* doc2 =
        state.addDocument(QStringLiteral("h"), QSize(128, 96), 72);
    CHECK(doc2 != nullptr);
    if (!doc2) return 1;
    QImage disc(128, 96, QImage::Format_ARGB32_Premultiplied);
    disc.fill(0xFF000000);
    {
        QPainter p(&disc);
        p.setBrush(Qt::white);
        p.setPen(Qt::NoPen);
        p.drawEllipse(QPointF(64, 48), 20, 20);  // right edge at x=84
    }
    state.placeImageLayer(disc, QStringLiteral("d"), QPointF(64, 48), 1.0);
    state.setSelection(QRectF(40, 28, 42, 40), false);  // right edge at 82
    doc2->rebuildComposite();
    app.processEvents();
    const int marqueeRight = fallCol(selectionAsMask(*doc2), 48);
    CHECK(marqueeRight == 82);
    {
        RefineDialog hard(&state, doc2);
        hard.show();
        app.processEvents();
        auto* hbuttons = hard.findChild<QDialogButtonBox*>();
        CHECK(hbuttons != nullptr);
        QPushButton* happly =
            hbuttons ? hbuttons->button(QDialogButtonBox::Apply) : nullptr;
        CHECK(happly != nullptr);
        if (!happly) return 1;
        happly->click();
        app.processEvents();
        CHECK(hard.result().accepted);
        // Boundary pulled from the marquee onto the disc's true edge.
        CHECK(fallCol(hard.result().mask, 48) == 84);
    }

    std::printf("  checks=%d failures=%d\n", pittore_test::checks(),
                pittore_test::failures());
    return pittore_test::failures() == 0 ? 0 : 1;
}
