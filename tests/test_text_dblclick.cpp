// test_text_dblclick.cpp — double-click a live text layer to edit it: the
// real Press/Release/DblClick/Release sequence with the Move tool (which
// must promote itself to the Type tool), plus a double-click with a
// non-move/type tool (Brush), which must start the session in place without
// switching tools. Runs headless offscreen.
#include <cstdio>

#include <QApplication>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QWidget>

#include "engine/core/log.h"
#include "test_util.h"
#include "ui/app_state.h"
#include "ui/canvas_view.h"

using namespace pittore::ui;

namespace {

void sendMouse(QWidget* target, QEvent::Type type, const QPointF& pos,
               Qt::MouseButton button, Qt::MouseButtons buttons,
               Qt::KeyboardModifiers mods = Qt::NoModifier) {
    const QPointF global = target->mapToGlobal(pos);
    QMouseEvent e(type, pos, global, button, buttons, mods);
    QApplication::sendEvent(target, &e);
}

void sendKey(QWidget* target, QEvent::Type type, int key, const QString& text,
             Qt::KeyboardModifiers mods = Qt::NoModifier) {
    QKeyEvent e(type, key, mods, text);
    QApplication::sendEvent(target, &e);
}

}  // namespace

int main(int argc, char** argv) {
    QApplication app(argc, argv);
    AppState state;
    DocumentItem* d = state.addDocument(QStringLiteral("t"), QSize(400, 300), 300);
    if (!d) return 1;

    QWidget window;
    auto* canvas = new CanvasView(&state, &window);
    window.resize(900, 700);
    canvas->setGeometry(0, 0, 900, 700);
    window.show();
    canvas->zoomToFit();
    app.processEvents();
    QWidget* vp = canvas->viewport();

    // Create a text layer through the Type tool.
    state.setActiveTool(ToolId::HorizontalType);
    app.processEvents();
    const QPointF tp = canvas->documentToView(QPointF(60, 80));
    sendMouse(vp, QEvent::MouseButtonPress, tp, Qt::LeftButton, Qt::LeftButton);
    sendMouse(vp, QEvent::MouseButtonRelease, tp, Qt::LeftButton, Qt::NoButton);
    app.processEvents();
    std::printf("after type click: editing=%d layers=%d\n",
                canvas->textEditing() ? 1 : 0, d->layers.size());
    QWidget* focus = QApplication::focusWidget();
    sendKey(focus ? focus : vp, QEvent::KeyPress, Qt::Key_H, QStringLiteral("H"));
    sendKey(focus ? focus : vp, QEvent::KeyPress, Qt::Key_I, QStringLiteral("i"));
    app.processEvents();
    std::printf("after typing: text='%s' pixels=%d\n",
                d->layers[d->activeLayer].textSpec.text.toUtf8().constData(),
                d->layers[d->activeLayer].pixels ? 1 : 0);
    sendKey(focus ? focus : vp, QEvent::KeyPress, Qt::Key_Escape, QString());
    app.processEvents();
    std::printf("after escape: editing=%d layers=%d\n",
                canvas->textEditing() ? 1 : 0, d->layers.size());

    // Move tool + the real double-click sequence on the text.
    state.setActiveTool(ToolId::Move);
    app.processEvents();
    const int textIndex = d->activeLayer;
    const QRectF bounds = layerBounds(*d, d->layers[textIndex]);
    const QPointF insideView =
        canvas->documentToView(bounds.center());
    std::printf("textIndex=%d bounds=(%.1f,%.1f %.1fx%.1f)\n", textIndex,
                bounds.x(), bounds.y(), bounds.width(), bounds.height());
    sendMouse(vp, QEvent::MouseButtonPress, insideView, Qt::LeftButton,
              Qt::LeftButton);
    app.processEvents();
    std::printf("after press1: active=%d editing=%d\n", d->activeLayer,
                canvas->textEditing() ? 1 : 0);
    sendMouse(vp, QEvent::MouseButtonRelease, insideView, Qt::LeftButton,
              Qt::NoButton);
    app.processEvents();
    std::printf("after release1: active=%d editing=%d\n", d->activeLayer,
                canvas->textEditing() ? 1 : 0);
    std::printf("textLayerAt=%d liveText=%d hasPixels=%d\n",
                state.textLayerAt(canvas->viewToDocument(insideView)),
                d->layers[textIndex].liveText ? 1 : 0,
                d->layers[textIndex].pixels ? 1 : 0);
    sendMouse(vp, QEvent::MouseButtonDblClick, insideView, Qt::LeftButton,
              Qt::LeftButton);
    app.processEvents();
    std::printf("after dblclick: active=%d editing=%d\n", d->activeLayer,
                canvas->textEditing() ? 1 : 0);
    sendMouse(vp, QEvent::MouseButtonRelease, insideView, Qt::LeftButton,
              Qt::NoButton);
    app.processEvents();
    std::printf("after release2: active=%d editing=%d\n", d->activeLayer,
                canvas->textEditing() ? 1 : 0);
    CHECK(canvas->textEditing());
    // Move promoted itself to the Type tool on the double-click: the options
    // bar, Character panel and cursor now belong to the edited run.
    CHECK(state.activeTool() == ToolId::HorizontalType);
    // End the session, then double-click with a NON-move/type tool (Brush):
    // the session must start in place without switching tools.
    sendKey(QApplication::focusWidget() ? QApplication::focusWidget() : vp,
            QEvent::KeyPress, Qt::Key_Escape, QString());
    app.processEvents();
    CHECK(!canvas->textEditing());
    state.setActiveTool(ToolId::Brush);
    app.processEvents();
    sendMouse(vp, QEvent::MouseButtonPress, insideView, Qt::LeftButton,
              Qt::LeftButton);
    sendMouse(vp, QEvent::MouseButtonRelease, insideView, Qt::LeftButton,
              Qt::NoButton);
    app.processEvents();
    sendMouse(vp, QEvent::MouseButtonDblClick, insideView, Qt::LeftButton,
              Qt::LeftButton);
    app.processEvents();
    std::printf("brush dblclick: tool=%d editing=%d\n",
                int(state.activeTool()), canvas->textEditing() ? 1 : 0);
    CHECK(canvas->textEditing());
    CHECK(state.activeTool() == ToolId::Brush);
    // End the session, then simulate a setup where Qt never delivers the
    // DblClick event: two plain presses in quick succession on the text must
    // still start editing (manual second-press detector), never a move drag.
    sendKey(QApplication::focusWidget() ? QApplication::focusWidget() : vp,
            QEvent::KeyPress, Qt::Key_Escape, QString());
    app.processEvents();
    CHECK(!canvas->textEditing());
    state.setActiveTool(ToolId::Move);
    app.processEvents();
    sendMouse(vp, QEvent::MouseButtonPress, insideView, Qt::LeftButton,
              Qt::LeftButton);
    sendMouse(vp, QEvent::MouseButtonRelease, insideView, Qt::LeftButton,
              Qt::NoButton);
    app.processEvents();
    CHECK(!canvas->textEditing());
    sendMouse(vp, QEvent::MouseButtonPress, insideView, Qt::LeftButton,
              Qt::LeftButton);
    app.processEvents();
    std::printf("manual second press: editing=%d\n",
                canvas->textEditing() ? 1 : 0);
    CHECK(canvas->textEditing());
    // The manual detector path promotes Move the same way the real DblClick
    // event does.
    CHECK(state.activeTool() == ToolId::HorizontalType);
    sendMouse(vp, QEvent::MouseButtonRelease, insideView, Qt::LeftButton,
              Qt::NoButton);
    app.processEvents();
    // Whitespace-only run: empty raster bakes no pixels, but the run must
    // stay double-clickable via the font-metric fallback box.
    sendKey(QApplication::focusWidget() ? QApplication::focusWidget() : vp,
            QEvent::KeyPress, Qt::Key_Escape, QString());
    app.processEvents();
    CHECK(!canvas->textEditing());
    state.setActiveTool(ToolId::Move);
    app.processEvents();
    const int wsIndex = state.addTextLayer(QPointF(40, 200), 0.0, QString(),
                                           0.0, QColor());
    CHECK(wsIndex >= 0);
    d->layers[wsIndex].textSpec.text = QStringLiteral("   ");
    state.refreshTextLayer(wsIndex);
    app.processEvents();
    CHECK(d->layers[wsIndex].liveText);
    // Force the unrendered state (pixels lost: project from a build that
    // never baked, font missing at load): the estimate box must still hit.
    d->layers[wsIndex].pixels.reset();
    const QPointF wsDoc(44, 220);  // inside the estimated run box
    std::printf("whitespace textLayerAt=%d (want %d)\n",
                state.textLayerAt(wsDoc), wsIndex);
    CHECK(state.textLayerAt(wsDoc) == wsIndex);
    const QPointF wsView = canvas->documentToView(wsDoc);
    sendMouse(vp, QEvent::MouseButtonPress, wsView, Qt::LeftButton,
              Qt::LeftButton);
    sendMouse(vp, QEvent::MouseButtonRelease, wsView, Qt::LeftButton,
              Qt::NoButton);
    app.processEvents();
    sendMouse(vp, QEvent::MouseButtonDblClick, wsView, Qt::LeftButton,
              Qt::LeftButton);
    app.processEvents();
    std::printf("whitespace dblclick: editing=%d\n",
                canvas->textEditing() ? 1 : 0);
    CHECK(canvas->textEditing());
    sendMouse(vp, QEvent::MouseButtonRelease, wsView, Qt::LeftButton,
              Qt::NoButton);
    app.processEvents();
    sendKey(QApplication::focusWidget() ? QApplication::focusWidget() : vp,
            QEvent::KeyPress, Qt::Key_Escape, QString());
    app.processEvents();
    std::printf("  checks=%d failures=%d\n", pittore_test::checks(),
                pittore_test::failures());
    return pittore_test::failures() == 0 ? 0 : 1;
}
