// test_infinite_zoom.cpp — ladder-free zoom: setZoom passes through
// geometrically to the sanity rails, keys continue past the ladder ends,
// garbage (zero/negative/inf/NaN) is rejected, and the scroll ranges stay
// in int range at the extremes. Runs headless offscreen.
#include <cmath>
#include <cstdio>
#include <limits>

#include <QApplication>
#include <QScrollBar>
#include <QWidget>

#include "engine/core/log.h"
#include "test_util.h"
#include "ui/app_state.h"
#include "ui/canvas/shared/canvas_helpers.h"
#include "ui/canvas_view.h"

using namespace pittore::ui;

int main(int argc, char** argv) {
    QApplication app(argc, argv);
    AppState state;
    DocumentItem* d = state.addDocument(QStringLiteral("z"), QSize(400, 300), 300);
    if (!d) return 1;

    QWidget window;
    auto* canvas = new CanvasView(&state, &window);
    window.resize(900, 700);
    canvas->setGeometry(0, 0, 900, 700);
    window.show();
    canvas->zoomToFit();
    app.processEvents();

    // Below the old ladder floor (0.000833) and above its ceiling (32).
    canvas->setZoom(1e-6);
    CHECK_NEAR(canvas->zoom(), 1e-6, 1e-12);
    canvas->setZoom(1e6);
    CHECK_NEAR(canvas->zoom(), 1e6, 1.0);
    // Wheel-style geometric sets pass through between the rails.
    canvas->setZoom(0.123456);
    CHECK_NEAR(canvas->zoom(), 0.123456, 1e-9);

    // Garbage is rejected, never clamped into a surprise value.
    canvas->setZoom(0.0);
    CHECK_NEAR(canvas->zoom(), 0.123456, 1e-9);
    canvas->setZoom(-2.0);
    CHECK_NEAR(canvas->zoom(), 0.123456, 1e-9);
    canvas->setZoom(std::numeric_limits<double>::infinity());
    CHECK_NEAR(canvas->zoom(), 0.123456, 1e-9);
    canvas->setZoom(std::numeric_limits<double>::quiet_NaN());
    CHECK_NEAR(canvas->zoom(), 0.123456, 1e-9);

    // Rails hold at the extremes.
    canvas->setZoom(1e30);
    CHECK(canvas->zoom() == kMaxZoom);
    canvas->setZoom(1e-30);
    CHECK(canvas->zoom() == kMinZoom);

    // Keys continue geometrically past the ladder ends.
    canvas->setZoom(32.0);
    canvas->zoomIn();
    CHECK_NEAR(canvas->zoom(), 64.0, 1e-9);
    canvas->setZoom(zoomSteps().first());
    canvas->zoomOut();
    CHECK_NEAR(canvas->zoom(), zoomSteps().first() * 0.5, 1e-12);
    // Inside the ladder the steps are unchanged.
    canvas->setZoom(1.0);
    canvas->zoomIn();
    CHECK_NEAR(canvas->zoom(), 1.5, 1e-9);
    canvas->zoomOut();
    CHECK_NEAR(canvas->zoom(), 1.0, 1e-9);

    // Scroll ranges stay valid at the extremes (saturated, never overflowed).
    canvas->setZoom(kMaxZoom);
    app.processEvents();
    CHECK(canvas->horizontalScrollBar()->maximum() == 2147483647);
    CHECK(canvas->verticalScrollBar()->maximum() == 2147483647);
    canvas->setZoom(kMinZoom);
    app.processEvents();
    CHECK(canvas->horizontalScrollBar()->maximum() == 0);
    CHECK(canvas->verticalScrollBar()->maximum() == 0);

    // Doc<->view round-trips deep past the old ceiling.
    canvas->setZoom(1e6);
    const QPointF doc(10.25, 20.5);
    const QPointF back = canvas->viewToDocument(canvas->documentToView(doc));
    CHECK_NEAR(back.x(), doc.x(), 1e-6);
    CHECK_NEAR(back.y(), doc.y(), 1e-6);

    // Fit still frames the document.
    canvas->zoomToFit();
    CHECK(canvas->zoom() > 0.0 && std::isfinite(canvas->zoom()));

    std::printf("  checks=%d failures=%d\n", pittore_test::checks(),
                pittore_test::failures());
    return pittore_test::failures() == 0 ? 0 : 1;
}
