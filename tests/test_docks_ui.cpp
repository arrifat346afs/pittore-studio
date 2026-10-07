// Right-rail default tabs: Layers leads the top group, History the bottom
// one. Selecting Paths explicitly still wins until the next default event.
#include <cstdio>

#include <QApplication>
#include <QDockWidget>
#include <QMouseEvent>
#include <QTabBar>
#include <QWidget>

#include "test_util.h"
#include "ui/app_state.h"
#include "ui/canvas_view.h"
#include "ui/main_window.h"
#include "ui/tool_registry.h"

using namespace pittore::ui;

namespace {

// Tabbed dock pages stay "visible" even when behind another tab, so read
// the tab bar: the current tab text of the group containing `member`.
QString currentTabOf(QMainWindow& win, const QString& member) {
    for (auto* bar : win.findChildren<QTabBar*>()) {
        for (int i = 0; i < bar->count(); ++i) {
            if (bar->tabText(i).contains(member, Qt::CaseInsensitive))
                return bar->tabText(bar->currentIndex());
        }
    }
    return QString();
}

bool dockExists(QMainWindow& win, const QString& id) {
    return win.findChild<QDockWidget*>(QStringLiteral("dock.") + id) !=
           nullptr;
}

}  // namespace

int main(int argc, char** argv) {
    QApplication app(argc, argv);
    AppState state;
    DocumentItem* d =
        state.addDocument(QStringLiteral("docks"), QSize(200, 150), 300);
    CHECK(d != nullptr);
    if (!d) {
        const int rc = pittore_test::failures() == 0 ? 0 : 1;
        std::printf("  checks=%d failures=%d\n", pittore_test::checks(),
                    pittore_test::failures());
        return rc;
    }

    MainWindow win(&state);
    win.resize(1200, 800);
    win.show();
    app.processEvents();
    app.processEvents();

    // All three exist and share one tab group.
    CHECK(dockExists(win, QStringLiteral("layers")));
    CHECK(dockExists(win, QStringLiteral("paths")));
    CHECK(dockExists(win, QStringLiteral("history")));

    // Top group default: Layers surfaced, not Paths/Channels.
    CHECK(currentTabOf(win, QStringLiteral("Layers")) ==
          QStringLiteral("Layers"));

    // Bottom group default: History surfaced.
    CHECK(currentTabOf(win, QStringLiteral("History")) ==
          QStringLiteral("History"));

// Explicit selection still wins (and stays until the next default event).
    win.showPanel(QStringLiteral("paths"), true);
    app.processEvents();
    CHECK(currentTabOf(win, QStringLiteral("Paths")) ==
          QStringLiteral("Paths"));

    // Click-an-image-to-find-its-layer: bury Layers behind Paths, click the
    // small top layer on canvas with the Move tool, and the Layers tab comes
    // forward with that row active.
    {
        LayerItem bg;
        bg.name = QStringLiteral("Background");
        bg.kind = LayerItem::Kind::Pixel;
        auto bgImg = std::make_shared<pittore::Image>(200, 150);
        bgImg->fill(pittore::RGBAf{0.5f, 0.5f, 0.5f, 1});
        bg.pixels = std::move(bgImg);
        bg.sourceStamp = 1;
        LayerItem top;
        top.name = QStringLiteral("Sticker");
        top.kind = LayerItem::Kind::Pixel;
        auto topImg = std::make_shared<pittore::Image>(20, 20);
        topImg->fill(pittore::RGBAf{1, 0, 0, 1});
        top.pixels = std::move(topImg);
        top.offset = QPointF(150, 100);
        top.sourceStamp = 1;
        d->layers = {top, bg};
        d->rebuildComposite();
        state.setActiveLayerIndex(1);
        state.setActiveTool(ToolId::Move);
        state.setOption(ToolId::Move, QStringLiteral("autoselect"), true);
        app.processEvents();

        CanvasView* canvas = win.findChild<CanvasView*>();
        CHECK(canvas != nullptr);
        if (canvas) {
            QWidget* vp = canvas->viewport();
            const QPointF v = canvas->documentToView(QPointF(160, 110));
            const QPointF global = vp->mapToGlobal(v.toPoint());
            QMouseEvent press(QEvent::MouseButtonPress, v, global,
                             Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
            QApplication::sendEvent(vp, &press);
            QMouseEvent release(QEvent::MouseButtonRelease, v, global,
                               Qt::LeftButton, Qt::NoButton, Qt::NoModifier);
            QApplication::sendEvent(vp, &release);
            app.processEvents();
            app.processEvents();
            CHECK(state.activeDocument()->activeLayer == 0);
            CHECK(currentTabOf(win, QStringLiteral("Layers")) ==
                  QStringLiteral("Layers"));
        }
    }

    const int rc = pittore_test::failures() == 0 ? 0 : 1;
    std::printf("  checks=%d failures=%d\n", pittore_test::checks(),
                pittore_test::failures());
    return rc;
}
