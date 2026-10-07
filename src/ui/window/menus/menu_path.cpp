// Path menu: path verbs + live path-effect stack + trace. Verbs map to
// the engine/vector/path_ops + boolean kernels; the LPE submenu enumerates
// the registered effect catalogue so new effects appear without menu edits.
#include "ui/main_window.h"

#include <QMenu>
#include <QMenuBar>

#include "engine/vector/lpe/lpe.h"
#include "ui/window/shared/window_helpers.h"

namespace pittore::ui {

void MainWindow::buildPathMenu() {
    QMenu* path = menuBar()->addMenu(tr("&Path"));
    for (const QString& name :
         {tr("Union"), tr("Difference"), tr("Intersection"), tr("Exclusion"),
          tr("Division"), tr("Cut Path"), tr("Combine"), tr("Break Apart"),
          tr("Stroke to Path"), tr("Inset"), tr("Outset"), tr("Dynamic Offset"),
          tr("Linked Offset"), tr("Simplify")})
        makeAction(path, name);
    path->addSeparator();
    QMenu* lpe = path->addMenu(tr("Path Effects…"));
    makeAction(lpe, tr("Open Path Effects Panel"), {},
               [this] { showPanel(QStringLiteral("lpe")); });
    lpe->addSeparator();
    for (auto& info : vector::lpe::allEffects()) {
        if (info.experimental) continue;
        makeAction(lpe, QString::fromUtf8(info.label));
    }
    QMenu* lpeExp = lpe->addMenu(tr("Experimental"));
    for (auto& info : vector::lpe::allEffects()) {
        if (!info.experimental) continue;
        makeAction(lpeExp, QString::fromUtf8(info.label));
    }
    path->addSeparator();
    makeAction(path, tr("Trace Bitmap…"), {}, [this] { showPanel(QStringLiteral("trace")); });
    makeAction(path, tr("XML Editor…"), {}, [this] { showPanel(QStringLiteral("xml")); });
}

}  // namespace pittore::ui
