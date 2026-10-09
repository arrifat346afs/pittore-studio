// test_layers_drag.cpp — the Layers panel multi-selection drag contract: a
// plain press on a member of a multi-selection must keep the set (so the
// drag carries every selected row into the group), and only isolate the row
// on release when no drag happened. Runs headless (QT_QPA_PLATFORM=offscreen).
#include <cstdio>

#include <QApplication>
#include <QDir>
#include <QImage>
#include <QMouseEvent>
#include <QScrollArea>
#include <QSet>
#include <QVBoxLayout>
#include <QWidget>

#include "engine/core/log.h"
#include "test_util.h"
#include "ui/app_state.h"
#include "ui/panels/registry/panel_creators.h"

using namespace pittore::ui;

namespace {

QImage solidImage(int w, int h, QRgb c) {
    QImage img(w, h, QImage::Format_ARGB32_Premultiplied);
    img.fill(c);
    return img;
}

void sendMouse(QWidget* target, QEvent::Type type, const QPointF& pos,
               Qt::MouseButton button, Qt::MouseButtons buttons,
               Qt::KeyboardModifiers mods = Qt::NoModifier) {
    const QPointF global = target->mapToGlobal(pos.toPoint());
    QMouseEvent e(type, pos, global, button, buttons, mods);
    QApplication::sendEvent(target, &e);
}

// Row widgets live in a pool covering the visible window: find the row
// currently bound to a document layer index via its layerIndex property.
// Returns null when the index is off-screen (no widget by design).
QWidget* rowWidget(QWidget* panel, int index) {
    auto* scroll = panel->findChild<QScrollArea*>();
    if (!scroll || !scroll->widget()) return nullptr;
    const QList<QWidget*> rows = scroll->widget()->findChildren<QWidget*>(
        Qt::FindDirectChildrenOnly);
    for (QWidget* r : rows) {
        const QVariant v = r->property("layerIndex");
        if (v.isValid() && v.toInt() == index) return r;
    }
    return nullptr;
}

void pressRow(QWidget* panel, int index) {
    QWidget* row = rowWidget(panel, index);
    CHECK(row != nullptr);
    if (!row) return;
    // Body of the row: right of the eye, with no mask thumb or chevron on a
    // plain pixel row.
    sendMouse(row, QEvent::MouseButtonPress, QPointF(100, 20), Qt::LeftButton,
              Qt::LeftButton);
}

void releaseRow(QWidget* panel, int index) {
    QWidget* row = rowWidget(panel, index);
    CHECK(row != nullptr);
    if (!row) return;
    sendMouse(row, QEvent::MouseButtonRelease, QPointF(100, 20),
              Qt::LeftButton, Qt::NoButton);
}

}  // namespace

int main(int argc, char** argv) {
    QApplication app(argc, argv);
    const QString logDir = QDir::tempPath() + QStringLiteral("/pittore-layers-drag-log");
    QDir().mkpath(logDir);
    ::pittore::core::log::set_log_dir(logDir.toStdString().c_str());

    AppState state;
    DocumentItem* d = state.addDocument(QStringLiteral("drag"), QSize(64, 64), 300);
    CHECK(d != nullptr);
    if (!d) return 1;
    state.placeImageLayer(solidImage(8, 8, 0xFFFF0000), QStringLiteral("A"),
                          QPointF(16, 16), 1.0);
    state.placeImageLayer(solidImage(8, 8, 0xFF0000FF), QStringLiteral("B"),
                          QPointF(48, 48), 1.0);
    state.placeImageLayer(solidImage(8, 8, 0xFF00FF00), QStringLiteral("C"),
                          QPointF(48, 16), 1.0);
    CHECK(d->layers.size() == 4);

    QWidget window;
    QWidget* panel = createLayersPanel(&state, &window);
    window.resize(300, 400);
    window.show();
    app.processEvents();
    CHECK(rowWidget(panel, 0) != nullptr);
    CHECK(rowWidget(panel, 3) != nullptr);
    // Sanity: the pool covers every layer of this small document, each
    // bound exactly once.
    {
        auto* scroll = panel->findChild<QScrollArea*>();
        CHECK(scroll != nullptr && scroll->widget() != nullptr);
        if (scroll && scroll->widget()) {
            const QList<QWidget*> rows =
                scroll->widget()->findChildren<QWidget*>(
                    Qt::FindDirectChildrenOnly);
            QSet<int> bound;
            for (QWidget* r : rows) {
                const QVariant v = r->property("layerIndex");
                if (v.isValid() && v.toInt() >= 0) bound.insert(v.toInt());
            }
            CHECK_EQ(bound.size(), d->layers.size());
        }
    }

    // Multi-select the top three rows; pressing one of them must keep the set
    // (the drag payload is built from it) while moving the active layer.
    d->selectedLayers = QVector<int>{0, 1, 2};
    d->activeLayer = 2;
    pressRow(panel, 0);
    CHECK_EQ(d->selectedLayers.size(), 3);
    CHECK(d->selectedLayers.contains(0));
    CHECK(d->selectedLayers.contains(1));
    CHECK(d->selectedLayers.contains(2));
    CHECK_EQ(d->activeLayer, 0);

    // Releasing without a drag isolates the released row (plain click).
    releaseRow(panel, 0);
    CHECK_EQ(d->selectedLayers.size(), 1);
    CHECK_EQ(d->selectedLayers.first(), 0);
    CHECK_EQ(d->activeLayer, 0);

    // Same from another member of the set.
    d->selectedLayers = QVector<int>{0, 1, 2};
    d->activeLayer = 0;
    pressRow(panel, 1);
    CHECK_EQ(d->selectedLayers.size(), 3);
    CHECK_EQ(d->activeLayer, 1);
    releaseRow(panel, 1);
    CHECK_EQ(d->selectedLayers.size(), 1);
    CHECK_EQ(d->selectedLayers.first(), 1);

    // Pressing an unselected row still isolates it immediately.
    d->selectedLayers = QVector<int>{0, 1, 2};
    d->activeLayer = 0;
    pressRow(panel, 3);
    CHECK_EQ(d->selectedLayers.size(), 1);
    CHECK_EQ(d->selectedLayers.first(), 3);
    CHECK_EQ(d->activeLayer, 3);

    const int rc = pittore_test::failures() == 0 ? 0 : 1;
    std::printf("  checks=%d failures=%d\n", pittore_test::checks(),
                pittore_test::failures());
    return rc;
}
