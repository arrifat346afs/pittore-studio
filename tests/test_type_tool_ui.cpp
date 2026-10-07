// test_type_tool_ui.cpp — the canvas Type tool end to end: drag to size, the
// caret session, keyboard capture, double-click to resume, and the centre
// smart-guide lines. Runs headless (QT_QPA_PLATFORM=offscreen).
#include <cstdio>

#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QDockWidget>
#include <QDoubleSpinBox>
#include <QIcon>
#include <QKeyEvent>
#include <QMenu>
#include <QMenuBar>
#include <QMouseEvent>
#include <QPointF>
#include <QPushButton>
#include <QSpinBox>
#include <QToolButton>
#include <QWidget>

#include "engine/core/log.h"
#include "engine/text/text_engine.h"
#include "test_util.h"
#include "ui/app_state.h"
#include "ui/canvas_view.h"
#include "ui/main_window.h"
#include "ui/panels.h"

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
    CHECK(d != nullptr);
    if (!d) return 1;

    QWidget window;
    auto* canvas = new CanvasView(&state, &window);
    window.resize(900, 700);
    canvas->setGeometry(0, 0, 900, 700);
    window.show();
    canvas->zoomToFit();
    app.processEvents();

    QWidget* vp = canvas->viewport();
    state.setActiveTool(ToolId::HorizontalType);
    app.processEvents();

    // --- click = artistic text at the default size; the session is live ----
    const QPointF docPoint(80, 90);
    const QPointF viewPoint = canvas->documentToView(docPoint);
    sendMouse(vp, QEvent::MouseButtonPress, viewPoint, Qt::LeftButton,
              Qt::LeftButton);
    sendMouse(vp, QEvent::MouseButtonRelease, viewPoint, Qt::LeftButton,
              Qt::NoButton);
    app.processEvents();

    LayerItem* l = state.activeLayer();
    CHECK(l != nullptr);
    CHECK(l && l->liveText);
    CHECK(canvas->textEditing());
    const int index = d->activeLayer;

    // --- typing reaches the layer wherever Qt routes the key --------------
    QWidget* focus = QApplication::focusWidget();
    QWidget* keyTarget = focus ? focus : vp;
    sendKey(keyTarget, QEvent::KeyPress, Qt::Key_H, QStringLiteral("H"));
    sendKey(keyTarget, QEvent::KeyPress, Qt::Key_I, QStringLiteral("i"));
    app.processEvents();
    CHECK(d->layers[index].textSpec.text == QStringLiteral("Hi"));
    CHECK(d->layers[index].pixels != nullptr);   // ink re-rendered

    // --- Backspace edits, Return opens a second line -----------------------
    sendKey(keyTarget, QEvent::KeyPress, Qt::Key_Backspace, QString());
    app.processEvents();
    CHECK(d->layers[index].textSpec.text == QStringLiteral("H"));
    sendKey(keyTarget, QEvent::KeyPress, Qt::Key_Return, QStringLiteral("\r"));
    sendKey(keyTarget, QEvent::KeyPress, Qt::Key_O, QStringLiteral("o"));
    app.processEvents();
    CHECK(d->layers[index].textSpec.text == QStringLiteral("H\no"));

    // --- the caret is one line tall at the font size -----------------------
    const double advance = d->layers[index].textLineAdvance;
    CHECK(advance > 0.0);
    CHECK(advance >= d->layers[index].textSpec.size * 0.5);

    // --- Escape ends the session; the edit is one undo step ---------------
    sendKey(keyTarget, QEvent::KeyPress, Qt::Key_Escape, QString());
    app.processEvents();
    CHECK(!canvas->textEditing());
    CHECK(state.canUndo());
    state.undo();
    app.processEvents();
    // Undo removes the freshly created layer outright (the whole session was
    // one step), so it is gone rather than blanked.
    CHECK(d->layers.size() == 1);   // background only
    state.redo();
    app.processEvents();
    CHECK(d->layers.size() == 2);
    CHECK(d->layers[d->activeLayer].textSpec.text == QStringLiteral("H\no"));

    // --- double-click (Move tool) resumes the text ------------------------
    state.setActiveTool(ToolId::Move);
    app.processEvents();
    const int textIndex = d->activeLayer;
    const QRectF bounds = layerBounds(*d, d->layers[textIndex]);
    const QPointF inside = bounds.center();
    const QPointF insideView = canvas->documentToView(inside);
    sendMouse(vp, QEvent::MouseButtonDblClick, insideView, Qt::LeftButton,
              Qt::LeftButton);
    app.processEvents();
    CHECK(canvas->textEditing());
    QWidget* focus2 = QApplication::focusWidget();
    sendKey(focus2 ? focus2 : vp, QEvent::KeyPress, Qt::Key_Exclam,
            QStringLiteral("!"));
    app.processEvents();
    CHECK(d->layers[textIndex].textSpec.text == QStringLiteral("H\no!"));
    sendKey(focus2 ? focus2 : vp, QEvent::KeyPress, Qt::Key_Escape, QString());
    app.processEvents();
    CHECK(!canvas->textEditing());

    // --- selection: click-to-caret, Ctrl+A, arrows, delete ---------------
    {
        AppState sstate;
        DocumentItem* sd =
            sstate.addDocument(QStringLiteral("s"), QSize(400, 300), 300);
        QWidget sw;
        auto* scanvas = new CanvasView(&sstate, &sw);
        sw.resize(900, 700);
        scanvas->setGeometry(0, 0, 900, 700);
        sw.show();
        scanvas->zoomToFit();
        app.processEvents();
        QWidget* svp = scanvas->viewport();
        sstate.setActiveTool(ToolId::HorizontalType);
        app.processEvents();

        const QPointF sp = scanvas->documentToView(QPointF(60, 80));
        sendMouse(svp, QEvent::MouseButtonPress, sp, Qt::LeftButton, Qt::LeftButton);
        sendMouse(svp, QEvent::MouseButtonRelease, sp, Qt::LeftButton, Qt::NoButton);
        app.processEvents();
        CHECK(scanvas->textEditing());
        const int si = sd->activeLayer;
        // Park focus on this canvas explicitly: QApplication::focusWidget() can
        // still name the first block's window, which would swallow the keys.
        scanvas->viewport()->setFocus(Qt::MouseFocusReason);
        app.processEvents();
        QWidget* st = scanvas->viewport();

        const QString typed = QStringLiteral("hello world");
        for (const QChar ch : typed) {
            const int key = ch == QLatin1Char(' ')
                                ? static_cast<int>(Qt::Key_Space)
                                : ch.toUpper().unicode();
            sendKey(st, QEvent::KeyPress, key, QString(ch));
        }
        app.processEvents();
        CHECK(sd->layers[si].textSpec.text == typed);

        // Clicking left of the run places the caret at offset 0, so the next
        // character is inserted at the start rather than appended.
        const QRectF bounds = layerBounds(*sd, sd->layers[si]);
        const QPointF leftEdge =
            scanvas->documentToView(QPointF(bounds.left() - 2.0, bounds.center().y()));
        sendMouse(svp, QEvent::MouseButtonPress, leftEdge, Qt::LeftButton, Qt::LeftButton);
        sendMouse(svp, QEvent::MouseButtonRelease, leftEdge, Qt::LeftButton, Qt::NoButton);
        app.processEvents();
        sendKey(st, QEvent::KeyPress, Qt::Key_A, QStringLiteral("A"));
        app.processEvents();
        CHECK(sd->layers[si].textSpec.text == QStringLiteral("Ahello world"));

        // Right arrow then insert lands one character further on.
        sendKey(st, QEvent::KeyPress, Qt::Key_Right, QString());
        sendKey(st, QEvent::KeyPress, Qt::Key_B, QStringLiteral("B"));
        app.processEvents();
        CHECK(sd->layers[si].textSpec.text == QStringLiteral("AhBello world"));

        // Ctrl+A selects the whole run; typing replaces it.
        sendKey(st, QEvent::KeyPress, Qt::Key_A, QString(), Qt::ControlModifier);
        app.processEvents();
        sendKey(st, QEvent::KeyPress, Qt::Key_Z, QStringLiteral("z"));
        app.processEvents();
        CHECK(sd->layers[si].textSpec.text == QStringLiteral("z"));

        // Backspace on the last character clears the run; a plain printable
        // then restarts it.
        sendKey(st, QEvent::KeyPress, Qt::Key_Backspace, QString());
        app.processEvents();
        CHECK(sd->layers[si].textSpec.text.isEmpty());
        sendKey(st, QEvent::KeyPress, Qt::Key_Q, QStringLiteral("Q"));
        app.processEvents();
        CHECK(sd->layers[si].textSpec.text == QStringLiteral("Q"));

        // Escape ends the session and commits the edit as one step.
        sendKey(st, QEvent::KeyPress, Qt::Key_Escape, QString());
        app.processEvents();
        CHECK(!scanvas->textEditing());

        // Corner-resize with the Move tool: the point size changes and the run
        // is re-set at the new size; the opposite corner stays pinned.
        sstate.setActiveTool(ToolId::Move);
        app.processEvents();
        const int ti = sd->activeLayer;
        const double size0 = sd->layers[ti].textSpec.size;
        const QRectF b0 = layerBounds(*sd, sd->layers[ti]);
        const QPointF anchor = b0.topLeft();
        const QPointF handle = b0.bottomRight();
        const QPointF target = anchor + (handle - anchor) * 1.5;
        sendMouse(svp, QEvent::MouseButtonPress, scanvas->documentToView(handle),
                  Qt::LeftButton, Qt::LeftButton);
        sendMouse(svp, QEvent::MouseMove, scanvas->documentToView(target),
                  Qt::NoButton, Qt::LeftButton);
        sendMouse(svp, QEvent::MouseButtonRelease, scanvas->documentToView(target),
                  Qt::LeftButton, Qt::NoButton);
        app.processEvents();
        CHECK(sd->layers[ti].textSpec.size > size0 * 1.3);
        CHECK(sd->layers[ti].textSpec.size < size0 * 1.7);
        const QRectF b1 = layerBounds(*sd, sd->layers[ti]);
        CHECK_NEAR(b1.left(), anchor.x(), 3.0);
        CHECK_NEAR(b1.top(), anchor.y(), 3.0);
    }

    // --- the real window: the app-wide shortcut filter must not eat the run -
    {
        AppState wstate;
        DocumentItem* wd =
            wstate.addDocument(QStringLiteral("w"), QSize(400, 300), 300);
        CHECK(wd != nullptr);
        MainWindow win(&wstate);
        win.resize(1000, 760);
        win.show();
        app.processEvents();

        auto* wcanvas = win.findChild<CanvasView*>();
        CHECK(wcanvas != nullptr);
        if (wcanvas && wd) {
            wcanvas->zoomToFit();
            app.processEvents();
            QWidget* wvp = wcanvas->viewport();
            wstate.setActiveTool(ToolId::HorizontalType);
            app.processEvents();

            const QPointF wp =
                wcanvas->documentToView(QPointF(60, 70));
            sendMouse(wvp, QEvent::MouseButtonPress, wp, Qt::LeftButton,
                      Qt::LeftButton);
            sendMouse(wvp, QEvent::MouseButtonRelease, wp, Qt::LeftButton,
                      Qt::NoButton);
            app.processEvents();
            CHECK(wcanvas->textEditing());

            QWidget* wfocus = QApplication::focusWidget();
            QWidget* wtarget = wfocus ? wfocus : wvp;
            // These letters are all window shortcuts: X (swap colours), F
            // (screen mode) and a plain tool letter (V = Move). While the
            // caret is live they must be inserted as text.
            sendKey(wtarget, QEvent::KeyPress, Qt::Key_X, QStringLiteral("x"));
            sendKey(wtarget, QEvent::KeyPress, Qt::Key_F, QStringLiteral("f"));
            sendKey(wtarget, QEvent::KeyPress, Qt::Key_V, QStringLiteral("v"));
            sendKey(wtarget, QEvent::KeyPress, Qt::Key_Space, QStringLiteral(" "));
            app.processEvents();
            const int wi = wd->activeLayer;
            CHECK(wd->layers[wi].textSpec.text == QStringLiteral("xfv "));
            sendKey(wtarget, QEvent::KeyPress, Qt::Key_Escape, QString());
            app.processEvents();
            CHECK(!wcanvas->textEditing());

            // Resuming a text layer mirrors its spec into the Type options bar,
            // so the bar (and the Character panel) show what is being edited.
            wstate.setActiveTool(ToolId::HorizontalType);
            app.processEvents();
            const int li = wd->activeLayer;
            wd->layers[li].textSpec.size = 64.0;
            wd->layers[li].textSpec.bold = true;
            const QRectF lb = layerBounds(*wd, wd->layers[li]);
            const QPointF lbView = wcanvas->documentToView(lb.center());
            sendMouse(wvp, QEvent::MouseButtonPress, lbView, Qt::LeftButton,
                      Qt::LeftButton);
            sendMouse(wvp, QEvent::MouseButtonRelease, lbView, Qt::LeftButton,
                      Qt::NoButton);
            app.processEvents();
            CHECK(wcanvas->textEditing());
            CHECK_NEAR(wstate.option(ToolId::HorizontalType, QStringLiteral("size"))
                           .toDouble(),
                       64.0, 1e-9);
            CHECK_EQ(wstate.option(ToolId::HorizontalType, QStringLiteral("style"))
                         .toInt(),
                     4);
            if (auto* optSize =
                    win.findChild<QDoubleSpinBox*>(QStringLiteral("opt/size")))
                CHECK_NEAR(optSize->value(), 64.0, 1e-9);
            sendKey(wvp, QEvent::KeyPress, Qt::Key_Escape, QString());
            app.processEvents();

            // The Window menu offers the text panels under a Text submenu.
            QMenu* textMenu = nullptr;
            for (QMenu* menu : win.menuBar()->findChildren<QMenu*>())
                if (menu->title() == QStringLiteral("Text")) textMenu = menu;
            CHECK(textMenu != nullptr);
            QAction* characterAction = nullptr;
            if (textMenu) {
                QStringList titles;
                for (QAction* action : textMenu->actions()) {
                    titles << action->text();
                    if (action->text() == QStringLiteral("Character"))
                        characterAction = action;
                }
                CHECK(titles.contains(QStringLiteral("Character")));
                CHECK(titles.contains(QStringLiteral("Paragraph")));
            }

            // The Character panel is a separate top-level popup, not a dock,
            // and closing the popup unchecks the menu entry.
            CHECK(characterAction != nullptr);
            if (characterAction) {
                characterAction->setChecked(true);
                app.processEvents();
                auto* popup =
                    win.findChild<QWidget*>(QStringLiteral("panel.character"));
                CHECK(popup != nullptr);
                CHECK(popup && popup->isWindow());
                CHECK(win.findChild<QWidget*>(QStringLiteral("dock.character")) ==
                      nullptr);
                CHECK(characterAction->isChecked());
                if (popup) popup->close();
                app.processEvents();
                CHECK(!characterAction->isChecked());
            }

            // The Type options bar's button pops both text palettes.
            QPushButton* panelsButton = nullptr;
            for (QPushButton* button : win.findChildren<QPushButton*>())
                if (button->text() == QStringLiteral("Character and Paragraph Panels"))
                    panelsButton = button;
            CHECK(panelsButton != nullptr);
            if (panelsButton) {
                panelsButton->click();
                app.processEvents();
                CHECK(win.findChild<QWidget*>(QStringLiteral("panel.character")) !=
                      nullptr);
                CHECK(win.findChild<QWidget*>(QStringLiteral("panel.paragraph")) !=
                      nullptr);
                if (characterAction) CHECK(characterAction->isChecked());
            }
        }
    }

    // --- Character panel: controls drive the active live text layer --------
    {
        AppState pstate;
        DocumentItem* pd =
            pstate.addDocument(QStringLiteral("p"), QSize(400, 300), 300);
        CHECK(pd != nullptr);
        if (pd) {
            pstate.beginUndoStep();
            const int ci = pstate.addTextLayer(QPointF(20, 40), 0.0,
                                               QStringLiteral("Liberation Sans"), 36.0,
                                               QColor(0, 0, 0));
            pstate.commitUndoStep(QStringLiteral("Text"), QStringLiteral("type"));
            CHECK(ci >= 0);
            pstate.setActiveLayerIndex(ci);

            const PanelInfo* info = panelInfo(QStringLiteral("character"));
            CHECK(info != nullptr);
            if (info) {
                QWidget* panel = info->factory(&pstate, nullptr);
                CHECK(panel != nullptr);
                if (panel) {
                    auto* family = panel->findChild<QComboBox*>(
                        QStringLiteral("character.family"));
                    auto* size = panel->findChild<QDoubleSpinBox*>(
                        QStringLiteral("character.size"));
                    auto* underline = panel->findChild<QComboBox*>(
                        QStringLiteral("character.underline"));
                    auto* superSub = panel->findChild<QComboBox*>(
                        QStringLiteral("character.superSub"));
                    auto* allCaps = panel->findChild<QCheckBox*>(
                        QStringLiteral("character.allCaps"));
                    auto* liga = panel->findChild<QCheckBox*>(
                        QStringLiteral("character.ot:liga"));
                    auto* hscale = panel->findChild<QSpinBox*>(
                        QStringLiteral("character.hScale"));
                    auto* fill = panel->findChild<QToolButton*>(
                        QStringLiteral("character.fill"));
                    CHECK(family && size && underline && superSub && allCaps && liga &&
                          hscale && fill);
                    // Every installed family is offered, not a hardcoded few.
                    if (family) CHECK(family->count() > 100);
                    // The panel shows the layer it is editing.
                    if (size) CHECK_NEAR(size->value(), 36.0, 1e-9);
                    if (fill) {
                        const QIcon icon = fill->icon();
                        CHECK(!icon.isNull());
                    }

                    // A size edit is one undo step on the layer.
                    if (size) {
                        size->setValue(72.0);
                        app.processEvents();
                        CHECK_NEAR(pd->layers[ci].textSpec.size, 72.0, 1e-9);
                        CHECK(pstate.canUndo());
                        pstate.undo();
                        app.processEvents();
                        CHECK_NEAR(pd->layers[ci].textSpec.size, 36.0, 1e-9);
                    }
                    if (underline) {
                        const int single = underline->findData(1);
                        CHECK(single >= 0);
                        underline->setCurrentIndex(single);
                        app.processEvents();
                        CHECK_EQ(pd->layers[ci].textSpec.underline, 1);
                    }
                    if (superSub) {
                        superSub->setCurrentIndex(superSub->findData(1));
                        app.processEvents();
                        CHECK_EQ(pd->layers[ci].textSpec.superSub, 1);
                    }
                    if (allCaps) {
                        allCaps->setChecked(true);
                        app.processEvents();
                        CHECK(pd->layers[ci].textSpec.allCaps);
                    }
                    if (liga) {
                        liga->setChecked(true);
                        app.processEvents();
                        CHECK((pd->layers[ci].textSpec.otFeatures &
                               pittore::text::OTF_Liga) != 0);
                    }
                    if (hscale) {
                        hscale->setValue(150);
                        app.processEvents();
                        CHECK_NEAR(pd->layers[ci].textSpec.hScale, 150.0, 1e-9);
                    }
                    delete panel;
                }
            }
        }
    }

    const int rc = pittore_test::failures() == 0 ? 0 : 1;
    std::printf("  checks=%d failures=%d\n", pittore_test::checks(),
                pittore_test::failures());
    return rc;
}
