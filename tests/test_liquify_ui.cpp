#include <QApplication>
#include <QComboBox>
#include <QImage>
#include <QMouseEvent>
#include <QPainter>
#include <QPushButton>
#include <QSpinBox>
#include <QTemporaryDir>
#include <QTimer>

#include "engine/compute/warp.h"
#include "test_util.h"
#include "ui/app_state.h"
#include "ui/liquify_dialog.h"

using namespace pittore::ui;

namespace {

QImage splitImage(int w, int h) {
    QImage img(w, h, QImage::Format_ARGB32);
    img.fill(Qt::blue);
    QPainter p(&img);
    p.fillRect(0, 0, w / 2, h, Qt::red);
    p.end();
    return img;
}

void sendMouse(QWidget *target, QEvent::Type type, const QPointF &pos,
               Qt::MouseButton button, Qt::MouseButtons buttons) {
    const QPointF global = target->mapToGlobal(pos.toPoint());
    QMouseEvent e(type, pos, global, button, buttons, Qt::NoModifier);
    QApplication::sendEvent(target, &e);
}

LiquifyCanvas *findCanvas(LiquifyDialog &dlg) {
    return dlg.findChild<LiquifyCanvas *>();
}

void testDialogAcceptCommitsOneStep() {
    AppState state;
    DocumentItem *d = state.addDocument(QStringLiteral("liq"), QSize(64, 48), 72);
    CHECK(d != nullptr);
    if (!d) return;
    state.placeImageLayer(splitImage(64, 48), QStringLiteral("split"),
                          QPointF(32, 24), 1.0);
    const QImage before = d->composite.copy();
    LiquifyDialog dlg(&state);
    dlg.show();
    QApplication::processEvents();
    LiquifyCanvas *canvas = findCanvas(dlg);
    CHECK(canvas != nullptr);
    if (!canvas) return;
    canvas->zoomToFit();
    QApplication::processEvents();
    const QPoint center = canvas->rect().center();
    sendMouse(canvas, QEvent::MouseButtonPress, QPointF(center), Qt::LeftButton,
              Qt::LeftButton);
    sendMouse(canvas, QEvent::MouseMove, QPointF(center + QPoint(60, 4)),
              Qt::NoButton, Qt::LeftButton);
    sendMouse(canvas, QEvent::MouseMove, QPointF(center + QPoint(110, 8)),
              Qt::NoButton, Qt::LeftButton);
    sendMouse(canvas, QEvent::MouseButtonRelease, QPointF(center + QPoint(110, 8)),
              Qt::LeftButton, Qt::NoButton);
    QApplication::processEvents();
    dlg.accept();
    CHECK_EQ(state.undoDepth(), 2);
    CHECK(state.undoStepName() == QStringLiteral("Liquify"));
    CHECK(d->composite != before);
    state.undo();
    CHECK(d->composite == before);
}

void testDialogCancelRestores() {
    AppState state;
    DocumentItem *d = state.addDocument(QStringLiteral("liq"), QSize(64, 48), 72);
    CHECK(d != nullptr);
    if (!d) return;
    state.placeImageLayer(splitImage(64, 48), QStringLiteral("split"),
                          QPointF(32, 24), 1.0);
    const QImage before = d->composite.copy();
    LiquifyDialog dlg(&state);
    dlg.show();
    QApplication::processEvents();
    LiquifyCanvas *canvas = findCanvas(dlg);
    CHECK(canvas != nullptr);
    if (!canvas) return;
    canvas->zoomToFit();
    QApplication::processEvents();
    const QPoint center = canvas->rect().center();
    sendMouse(canvas, QEvent::MouseButtonPress, QPointF(center), Qt::LeftButton,
              Qt::LeftButton);
    sendMouse(canvas, QEvent::MouseMove, QPointF(center + QPoint(30, 6)),
              Qt::NoButton, Qt::LeftButton);
    sendMouse(canvas, QEvent::MouseButtonRelease, QPointF(center + QPoint(30, 6)),
              Qt::LeftButton, Qt::NoButton);
    QApplication::processEvents();
    dlg.reject();
    CHECK_EQ(state.undoDepth(), 1);
    CHECK(d->composite == before);
}

void testFreezeMaskRoundTrip() {
    AppState state;
    DocumentItem *d = state.addDocument(QStringLiteral("liq"), QSize(64, 48), 72);
    CHECK(d != nullptr);
    if (!d) return;
    state.placeImageLayer(splitImage(64, 48), QStringLiteral("split"),
                          QPointF(32, 24), 1.0);
    LiquifyDialog dlg(&state);
    dlg.show();
    QApplication::processEvents();
    LiquifyCanvas *canvas = findCanvas(dlg);
    CHECK(canvas != nullptr);
    if (!canvas) return;
    canvas->brush().tool = 12;
    canvas->brush().size = 200;
    canvas->zoomToFit();
    QApplication::processEvents();
    const QPoint center = canvas->rect().center();
    sendMouse(canvas, QEvent::MouseButtonPress, QPointF(center), Qt::LeftButton,
              Qt::LeftButton);
    sendMouse(canvas, QEvent::MouseButtonRelease, QPointF(center), Qt::LeftButton,
              Qt::NoButton);
    QApplication::processEvents();
    CHECK(state.liquifyMaskPresent());
    dlg.reject();
    CHECK_EQ(state.undoDepth(), 1);
}

void testSessionMeshSaveLoad() {
    AppState state;
    DocumentItem *d = state.addDocument(QStringLiteral("liq"), QSize(64, 48), 72);
    CHECK(d != nullptr);
    if (!d) return;
    state.placeImageLayer(splitImage(64, 48), QStringLiteral("split"),
                          QPointF(32, 24), 1.0);
    CHECK(state.beginLiquifySession());
    pittore::compute::WarpMesh &mesh = state.liquifySessionMesh();
    CHECK(mesh.cols > 2);
    mesh.offsets[0] = 3.5f;
    mesh.offsets[1] = -2.5f;
    QTemporaryDir dir;
    CHECK(dir.isValid());
    const QString path = dir.filePath(QStringLiteral("mesh.iflq"));
    CHECK(state.saveLiquifyMesh(path));
    mesh.offsets[0] = 0.0f;
    mesh.offsets[1] = 0.0f;
    CHECK(state.loadLiquifyMesh(path));
    CHECK(mesh.offsets[0] == 3.5f);
    CHECK(mesh.offsets[1] == -2.5f);
    CHECK(!state.loadLiquifyMesh(dir.filePath(QStringLiteral("missing.iflq"))));
    state.resetLiquifyMesh();
    CHECK(mesh.offsets[0] == 0.0f);
    state.cancelLiquifySession();
    CHECK(!state.liquifySessionActive());
}

void testMirrorDabMovesPixels() {
    AppState state;
    DocumentItem *d = state.addDocument(QStringLiteral("liq"), QSize(64, 48), 72);
    CHECK(d != nullptr);
    if (!d) return;
    state.placeImageLayer(splitImage(64, 48), QStringLiteral("split"),
                          QPointF(32, 24), 1.0);
    CHECK(state.beginLiquifySession());
    LayerItem *l = state.activeLayer();
    CHECK(l != nullptr && l->pixels);
    if (!l || !l->pixels) {
        state.cancelLiquifySession();
        return;
    }
    const float wx = float(l->pixels->width());
    const float before = l->pixels->at(std::uint32_t(wx * 0.75f), 24).r;
    CHECK(state.liquifyMirrorDab(wx * 0.5f, 24.0f, 20.0f, 1.5707963f, false, 1.0f, 1.0f));
    state.flushPaint();
    const float after = l->pixels->at(std::uint32_t(wx * 0.75f), 24).r;
    CHECK(after != before);
    state.cancelLiquifySession();
}

void testSidePanelNothingClipped() {
    AppState state;
    DocumentItem *d = state.addDocument(QStringLiteral("liq"), QSize(64, 48), 72);
    CHECK(d != nullptr);
    if (!d) return;
    state.placeImageLayer(splitImage(64, 48), QStringLiteral("split"),
                          QPointF(32, 24), 1.0);
    LiquifyDialog dlg(&state);
    dlg.show();
    QApplication::processEvents();
    QApplication::processEvents();
    const int right = dlg.width();
    for (QWidget *w : dlg.findChildren<QWidget *>()) {
        if (qobject_cast<QSpinBox *>(w) || qobject_cast<QPushButton *>(w) ||
            qobject_cast<QComboBox *>(w)) {
            if (!w->isVisible()) continue;
            const QPoint tl = w->mapTo(&dlg, QPoint(0, 0));
            CHECK(tl.x() >= 0);
            CHECK(tl.x() + w->width() <= right);
            CHECK(w->width() + 1 >= w->minimumSizeHint().width());
        }
    }
    dlg.reject();
}

void testZoomDefaultsToFit() {
    AppState state;
    DocumentItem *d = state.addDocument(QStringLiteral("liq"), QSize(64, 48), 72);
    CHECK(d != nullptr);
    if (!d) return;
    state.placeImageLayer(splitImage(64, 48), QStringLiteral("split"),
                          QPointF(32, 24), 1.0);
    LiquifyDialog dlg(&state);
    dlg.show();
    QApplication::processEvents();
    auto *zoomCombo = dlg.findChild<QComboBox *>(QStringLiteral("liquifyZoomCombo"));
    CHECK(zoomCombo != nullptr);
    if (zoomCombo) CHECK(zoomCombo->currentText() == QStringLiteral("Fit"));
    LiquifyCanvas *canvas = findCanvas(dlg);
    CHECK(canvas != nullptr);
    if (canvas) {
        canvas->zoomToWidth();
        CHECK(canvas->zoom() > 0.0);
        canvas->zoomIn();
        const double zIn = canvas->zoom();
        canvas->zoomOut();
        CHECK(canvas->zoom() < zIn);
    }
    dlg.reject();
}

}

static void testLiquifyUi() {
    int fakeargc = 1;
    char *fakeargv[] = {(char *)"test_liquify_ui", nullptr};
    QApplication app(fakeargc, fakeargv);
    testDialogAcceptCommitsOneStep();
    testDialogCancelRestores();
    testFreezeMaskRoundTrip();
    testSessionMeshSaveLoad();
    testMirrorDabMovesPixels();
    testSidePanelNothingClipped();
    testZoomDefaultsToFit();
}

#ifndef PITTORE_TEST_NO_MAIN
TEST_MAIN_CALL(testLiquifyUi)
#endif
