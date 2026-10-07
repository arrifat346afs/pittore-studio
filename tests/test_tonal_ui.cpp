// UI coverage: tonal sessions, one-shot filters, rotate/flip,
// slices, histogram, recovery, zoom. Headless offscreen.
#include <cmath>
#include <cstdio>

#include <QApplication>
#include <QColor>
#include <QContextMenuEvent>
#include <QDir>
#include <QFile>
#include <QImage>
#include <QMouseEvent>
#include <QPointF>
#include <QTemporaryDir>
#include <QTimer>
#include <QWidget>

#include "engine/core/tonal_ops.h"
#include "test_util.h"
#include "ui/app_state.h"
#include "ui/canvas_view.h"
#include "ui/project_manager.h"

using namespace pittore::ui;

namespace {

void sendMouse(QWidget* target, QEvent::Type type, const QPointF& pos,
               Qt::MouseButton button, Qt::MouseButtons buttons,
               Qt::KeyboardModifiers mods = Qt::NoModifier) {
    const QPointF global = target->mapToGlobal(pos);
    QMouseEvent e(type, pos, global, button, buttons, mods);
    QApplication::sendEvent(target, &e);
}

// Fill active layer with color via one big dab.
void prime(AppState& state, const QColor& color) {
    DocumentItem* d = state.activeDocument();
    if (!d) return;
    state.paintDab(QPointF(d->size.width() / 2.0, d->size.height() / 2.0), 1000.0, 1.0,
                   1.0, color);
    state.flushPaint();
}

}  // namespace

int main(int argc, char** argv) {
    QApplication app(argc, argv);
    AppState state;

    // Tonal session: preview lives, commit is one step, cancel restores.
    {
        DocumentItem* d = state.addDocument(QStringLiteral("tonal"), QSize(16, 16), 300);
        CHECK(d != nullptr);
        if (!d) return 1;
        prime(state, QColor(128, 128, 128));
        const int hist = d->history.size();

        LayerItem* layer = state.beginTonalEdit();
        CHECK(layer && layer->pixels);
        pittore::applyLevels(*layer->pixels, 3, 0.0, 1.0, 2.0, 0.0, 1.0);
        state.applyTonalEditPreview();
        const double lifted = d->composite.pixelColor(8, 8).redF();
        CHECK(lifted > 0.5);
        state.commitTonalEdit(QStringLiteral("Levels"), QStringLiteral("levels"));
        CHECK_EQ(d->history.size(), hist + 1);

        // Cancel restores the pixels the session started from and drops the step.
        layer = state.beginTonalEdit();
        CHECK(layer && layer->pixels);
        pittore::applyLevels(*layer->pixels, 3, 0.0, 1.0, 4.0, 0.0, 1.0);
        state.applyTonalEditPreview();
        state.cancelTonalEdit();
        CHECK_NEAR(d->composite.pixelColor(8, 8).redF(), lifted, 0.02);
        CHECK_EQ(d->history.size(), hist + 1);
    }

    // ---- Tonal previews re-apply from pristine, never compound ------------
    {
        DocumentItem* d = state.addDocument(QStringLiteral("tonal2"), QSize(16, 16), 300);
        CHECK(d != nullptr);
        if (!d) return 1;
        prime(state, QColor(128, 128, 128));
        LayerItem* layer = state.beginTonalEdit();
        CHECK(layer && layer->pixels);
        // Reference: a single gamma-4.0 apply on a pristine clone.
        pittore::Image ref(layer->pixels->clone());
        pittore::applyLevels(ref, 3, 0.0, 1.0, 4.0, 0.0, 1.0);
        // Two previews with a reset between collapse to the latest value.
        pittore::applyLevels(*layer->pixels, 3, 0.0, 1.0, 2.0, 0.0, 1.0);
        state.resetTonalEditPixels();
        pittore::applyLevels(*layer->pixels, 3, 0.0, 1.0, 4.0, 0.0, 1.0);
        CHECK(layer->pixels->width() == ref.width());
        bool same = layer->pixels->width() == ref.width() &&
                    layer->pixels->height() == ref.height();
        if (same) {
            const pittore::RGBAf* a = layer->pixels->data();
            const pittore::RGBAf* b = ref.data();
            const std::size_t n =
                std::size_t(ref.width()) * ref.height();
            for (std::size_t i = 0; i < n && same; ++i)
                same = a[i].r == b[i].r && a[i].g == b[i].g &&
                       a[i].b == b[i].b && a[i].a == b[i].a;
        }
        CHECK(same);
        state.cancelTonalEdit();
    }

    // ---- One-shot filter edit: a single undoable step ---------------------
    {
        DocumentItem* d = state.addDocument(QStringLiteral("sharp"), QSize(16, 16), 300);
        CHECK(d != nullptr);
        prime(state, QColor(120, 120, 120));
        const int hist = d->history.size();
        CHECK(state.applyLayerEditOneShot(
            [](pittore::Image& img) { pittore::applyUnsharpMask(img, 0.8, 1, 0.0); },
            QStringLiteral("Sharpen"), QStringLiteral("sharpen")));
        CHECK_EQ(d->history.size(), hist + 1);
        state.undo();
        CHECK_EQ(d->history.size(), hist);
    }

    // ---- Image Rotation / Flip Canvas: pixel grids + undo ----------------
    {
        DocumentItem* d = state.addDocument(QStringLiteral("rotate"), QSize(8, 6), 300);
        CHECK(d != nullptr);
        prime(state, QColor(10, 10, 10));
        LayerItem* l = state.activeLayer();
        CHECK(l && l->pixels);
        if (l && l->pixels) {
            for (int y = 0; y < 2; ++y)
                for (int x = 0; x < 2; ++x)
                    l->pixels->at(x, y) = pittore::RGBAf{1, 1, 1, 1};
        }
        const auto w0 = static_cast<std::uint32_t>(d->size.width());
        const auto h0 = static_cast<std::uint32_t>(d->size.height());

        CHECK(state.transformDocumentImage(QStringLiteral("cw90")));
        LayerItem* r = state.activeLayer();
        CHECK(r && r->pixels);
        if (r && r->pixels) {
            CHECK_EQ(r->pixels->width(), h0);
            CHECK_EQ(r->pixels->height(), w0);
            // The source top-left block moves to the destination top-right.
            CHECK_NEAR(r->pixels->at(r->pixels->width() - 1, 0).r, 1.0, 0.01);
        }
        state.undo();
        LayerItem* u = state.activeLayer();
        CHECK(u && u->pixels);
        if (u && u->pixels) {
            CHECK_EQ(u->pixels->width(), w0);
            CHECK_EQ(u->pixels->height(), h0);
            CHECK_NEAR(u->pixels->at(0, 0).r, 1.0, 0.01);
        }

        // Flip Canvas Horizontal mirrors about the document's centre.
        CHECK(state.transformDocumentImage(QStringLiteral("flipH")));
        LayerItem* f = state.activeLayer();
        CHECK(f && f->pixels);
        if (f && f->pixels)
            CHECK_NEAR(f->pixels->at(f->pixels->width() - 1, 0).r, 1.0, 0.01);
        CHECK(!state.transformDocumentImage(QStringLiteral("nonsense")));
    }

    // ---- Histogram data source: exact bins from a known composite ---------
    {
        DocumentItem* d = state.addDocument(QStringLiteral("hist"), QSize(8, 8), 300);
        CHECK(d != nullptr);
        if (!d) return 1;
        d->composite = QImage(8, 8, QImage::Format_ARGB32);
        d->composite.fill(QColor(10, 200, 30));
        pittore::Histogram256 h;
        CHECK(state.activeHistogram(h));
        CHECK_EQ(static_cast<long long>(h.rgb[0][10]), 64LL);
        CHECK_EQ(static_cast<long long>(h.rgb[1][200]), 64LL);
        CHECK_EQ(static_cast<long long>(h.rgb[2][30]), 64LL);
        CHECK_EQ(static_cast<long long>(h.luma[qRound(0.2126 * 10 + 0.7152 * 200 + 0.0722 * 30)]),
                 64LL);
    }

    // ---- Recovery snapshot: writes a loadable project, is not a Save ------
    {
        QTemporaryDir tmp;
        CHECK(tmp.isValid());
        const QString path = tmp.filePath(QStringLiteral("recovery.ifp"));
        DocumentItem* d = state.addDocument(QStringLiteral("recover"), QSize(12, 10), 300);
        CHECK(d != nullptr);
        prime(state, QColor(50, 60, 70));
        d->dirty = true;

        QString err;
        CHECK(state.writeRecoverySnapshot(path, &err));
        CHECK(QFile::exists(path));
        CHECK(d->dirty);                 // a snapshot must not clear the Save flag
        CHECK(d->filePath.isEmpty());    // ...nor claim the document's project file

        ProjectFileData data;
        CHECK(loadProjectFile(path, &data, &err));
        CHECK_EQ(data.size.width(), 12);
        CHECK_EQ(data.size.height(), 10);
        CHECK(data.layers.size() >= 1);
    }

    // ---- Canvas: Fit to Width / Print Size / right-click must not zoom ----
    {
        state.addDocument(QStringLiteral("zoom"), QSize(400, 300), 300);
        QWidget window;
        auto* canvas = new CanvasView(&state, &window);
        window.resize(800, 600);
        canvas->setGeometry(0, 0, 800, 600);
        window.show();
        canvas->zoomToFit();
        app.processEvents();

        canvas->zoomToWidth();
        app.processEvents();
        const double expected = (canvas->viewport()->width() - 16.0) / 400.0;
        CHECK_NEAR(canvas->zoom(), expected, 0.05);

        canvas->zoomPrintSize();
        app.processEvents();
        // A 300-DPI document shown at a typical <300 screen DPI is below 100%.
        CHECK(canvas->zoom() > 0.05);
        CHECK(canvas->zoom() < 1.0);

        state.setActiveTool(ToolId::Zoom);
        canvas->setZoom(1.0);
        const double before = canvas->zoom();
        sendMouse(canvas->viewport(), QEvent::MouseButtonPress, QPointF(100, 100),
                  Qt::RightButton, Qt::RightButton);
        app.processEvents();
        CHECK_NEAR(canvas->zoom(), before, 1e-9);   // right-click opened the menu instead

        // A right-click context-menu event reaches CanvasView and opens the
        // navigation menu; dismiss it from a zero-timer so exec() can return.
        bool menuAppeared = false;
        QTimer::singleShot(0, [&menuAppeared] {
            if (QWidget* popup = QApplication::activePopupWidget()) {
                menuAppeared = true;
                popup->close();
            }
        });
        const QPoint at(120, 120);
        QContextMenuEvent ctx(QContextMenuEvent::Mouse, at,
                              canvas->viewport()->mapToGlobal(at));
        QApplication::sendEvent(canvas->viewport(), &ctx);
        app.processEvents();
        CHECK(menuAppeared);
        CHECK_NEAR(canvas->zoom(), before, 1e-9);   // the menu itself does not zoom
    }

    // ---- Slice tool: draw a slice, move it with Slice Select, undo -------
    {
        DocumentItem* d = state.addDocument(QStringLiteral("slice"), QSize(200, 150), 300);
        CHECK(d != nullptr);
        QWidget window;
        auto* canvas = new CanvasView(&state, &window);
        window.resize(800, 600);
        canvas->setGeometry(0, 0, 800, 600);
        window.show();
        canvas->zoomToFit();
        app.processEvents();
        QWidget* vp = canvas->viewport();

        state.setActiveTool(ToolId::Slice);
        app.processEvents();
        const QPointF a = canvas->documentToView(QPointF(20, 20));
        const QPointF b = canvas->documentToView(QPointF(120, 100));
        sendMouse(vp, QEvent::MouseButtonPress, a, Qt::LeftButton, Qt::LeftButton);
        sendMouse(vp, QEvent::MouseMove, b, Qt::NoButton, Qt::LeftButton);
        sendMouse(vp, QEvent::MouseButtonRelease, b, Qt::LeftButton, Qt::NoButton);
        app.processEvents();
        CHECK_EQ(d->slices.size(), 1);
        if (d->slices.size() == 1) {
            CHECK_NEAR(d->slices[0].x(), 20, 2.0);
            CHECK_NEAR(d->slices[0].width(), 100, 2.0);
        }

        // Slice Select drags the slice; the move is one history step.
        state.setActiveTool(ToolId::SliceSelect);
        app.processEvents();
        const QPointF grab = canvas->documentToView(QPointF(70, 60));
        const QPointF drop = grab + QPointF(150, 100);
        const QPointF docDelta = canvas->viewToDocument(drop) - canvas->viewToDocument(grab);
        const int startX = d->slices.isEmpty() ? 0 : d->slices[0].x();
        sendMouse(vp, QEvent::MouseButtonPress, grab, Qt::LeftButton, Qt::LeftButton);
        sendMouse(vp, QEvent::MouseMove, drop, Qt::NoButton, Qt::LeftButton);
        sendMouse(vp, QEvent::MouseButtonRelease, drop, Qt::LeftButton, Qt::NoButton);
        app.processEvents();
        CHECK_EQ(d->slices.size(), 1);
        if (d->slices.size() == 1)
            CHECK_NEAR(d->slices[0].x(), startX + docDelta.x(), 2.0);

        state.undo();   // undo the move
        if (d->slices.size() == 1) CHECK_NEAR(d->slices[0].x(), startX, 2.0);
        state.undo();   // undo the draw
        CHECK_EQ(d->slices.size(), 0);
    }

    std::printf("  checks=%d failures=%d\n", pittore_test::checks(),
                pittore_test::failures());
    return pittore_test::failures() == 0 ? 0 : 1;
}
