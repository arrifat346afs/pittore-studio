// Object menu: transform, align, clones, symbols,
// markers, clip/mask, patterns). Actions route through runCommand ids the
// canvas/command layer already honors; panel entries reveal the new panels.
#include "ui/main_window.h"

#include <QMenu>
#include <QMenuBar>

#include "ui/window/shared/window_helpers.h"

namespace pittore::ui {

void MainWindow::buildObjectMenu() {
    QMenu* object = menuBar()->addMenu(tr("&Object"));
    makeAction(object, tr("Transform…"), QStringLiteral("Shift+Ctrl+M"),
               [this] { showPanel(QStringLiteral("transform")); });
    makeAction(object, tr("Align and Distribute…"), QStringLiteral("Shift+Ctrl+A"),
               [this] { showPanel(QStringLiteral("align")); });
    object->addSeparator();
    QMenu* clones = object->addMenu(tr("Clones"));
    for (const QString& name :
         {tr("Clone"), tr("Unlink Clone"), tr("Tiled Clones…"), tr("Symbols…"),
          tr("Recursively Unlink All")})
        makeAction(clones, name, {}, [this] { showPanel(QStringLiteral("symbols")); });
    QMenu* markers = object->addMenu(tr("Markers"));
    for (const QString& name :
         {tr("Add Start Marker"), tr("Add Mid Markers"), tr("Add End Marker"),
          tr("Remove Markers…")})
        makeAction(markers, name, {}, [this] { showPanel(QStringLiteral("markers")); });
    object->addSeparator();
    makeAction(object, tr("Fill and Stroke…"), QStringLiteral("Ctrl+Shift+F"),
               [this] { showPanel(QStringLiteral("appearance")); });
    makeAction(object, tr("Object Properties…"), {},
               [this] { showPanel(QStringLiteral("objectprops")); });
    QMenu* clip = object->addMenu(tr("Clip / Mask"));
    for (const QString& name :
         {tr("Set Clip"), tr("Release Clip"), tr("Set Mask"), tr("Release Mask"),
          tr("PowerClip"), tr("PowerMask")})
        makeAction(clip, name);
    makeAction(object, tr("Pattern…"), {}, [this] { showPanel(QStringLiteral("symbols")); });
    makeAction(object, tr("Raise"), QStringLiteral("PgUp"), [this] { runCommand("arrange-front"); });
    makeAction(object, tr("Lower"), QStringLiteral("PgDown"), [this] { runCommand("arrange-back"); });
}

}  // namespace pittore::ui
