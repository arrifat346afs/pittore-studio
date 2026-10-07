#include <QApplication>
#include <QComboBox>
#include <QCheckBox>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QEventLoop>
#include <QImage>
#include <QLabel>
#include <QPainter>
#include <QSlider>
#include <QTabWidget>
#include <QTimer>

#include <algorithm>
#include <cmath>

#include "engine/filter/filters.h"
#include "test_util.h"
#include "ui/app_state.h"
#include "ui/filter_dialog.h"

using namespace pittore::ui;
using namespace pittore::filter;

namespace {

int countWidgets(const QObject *root, const char *className) {
    int n = 0;
    for (const QObject *c : root->findChildren<const QObject *>()) {
        if (c->inherits(className)) ++n;
    }
    return n;
}

void testEveryPopupBuilds() {
    AppState state;
    for (const auto &d : allFilterDefs()) {
        const QString id = QString::fromUtf8(d.id);
        FilterDialog dlg(&state, id);
        CHECK(!dlg.windowTitle().isEmpty());
        int sliders = 0, combos = 0, checks = 0;
        for (const auto &p : d.params) {
            if (p.kind == 1) ++combos;
            else if (p.kind == 2) ++checks;
            else ++sliders;
        }
        CHECK(countWidgets(&dlg, "QSlider") == sliders);
        CHECK(countWidgets(&dlg, "QComboBox") == combos + 1);
        // Preview + Live-filter boxes on top of check-type params.
        CHECK(countWidgets(&dlg, "QCheckBox") == checks + 2);
        CHECK(dlg.findChild<QDialogButtonBox *>() != nullptr);
        for (const QObject *c : dlg.findChildren<const QObject *>()) {
            const auto *combo = qobject_cast<const QComboBox *>(c);
            if (combo) CHECK(combo->count() > 0);
        }
        QTimer::singleShot(0, &dlg, &QDialog::reject);
        CHECK(dlg.exec() == QDialog::Rejected);
    }
}

void testPopupAcceptsLivePreview() {
    AppState state;
    state.addDocument(QStringLiteral("probe"), QSize(48, 36), 72);
    {
        FilterDialog dlg(&state, QStringLiteral("gaussian_blur"));
        auto *spin = dlg.findChild<QDoubleSpinBox *>();
        CHECK(spin != nullptr);
        if (spin) spin->setValue(3.0);
        QTimer::singleShot(0, &dlg, &QDialog::accept);
        CHECK(dlg.exec() == QDialog::Accepted);
        CHECK(dlg.appliedParams().size() == 1);
    }
    {
        FilterDialog dlg(&state, QStringLiteral("polar"));
        auto *combo = dlg.findChild<QComboBox *>();
        CHECK(combo != nullptr);
        if (combo) combo->setCurrentIndex(0);
        QTimer::singleShot(0, &dlg, &QDialog::accept);
        CHECK(dlg.exec() == QDialog::Accepted);
    }
    {
        QImage solid(48, 36, QImage::Format_ARGB32);
        solid.fill(QColor(120, 140, 160));
        state.placeImageLayer(solid, QStringLiteral("solid"), QPointF(24, 18), 1.0);
        FilterDialog dlg(&state, QStringLiteral("sharpen"));
        CHECK(dlg.findChild<QLabel *>() != nullptr);
        const int depthBefore = state.undoDepth();
        QTimer::singleShot(0, &dlg, &QDialog::accept);
        CHECK(dlg.exec() == QDialog::Accepted);
        CHECK(dlg.appliedParams().size() == 1);
        CHECK_EQ(state.undoDepth(), depthBefore + 1);
    }
    {
        FilterGalleryDialog gallery(&state, QStringLiteral("Blur"));
        CHECK(!gallery.windowTitle().isEmpty());
        QTimer::singleShot(0, &gallery, &QDialog::reject);
        CHECK(gallery.exec() == QDialog::Rejected);
    }
}

void testPreviewToggleAndFitDefault() {    AppState state;
    state.addDocument(QStringLiteral("probe"), QSize(48, 36), 72);
    QImage solid(48, 36, QImage::Format_ARGB32);
    solid.fill(QColor(120, 140, 160));
    state.placeImageLayer(solid, QStringLiteral("solid"), QPointF(24, 18), 1.0);
    FilterDialog dlg(&state, QStringLiteral("gaussian_blur"));
    QCheckBox *preview = nullptr;
    QComboBox *zoom = nullptr;
    for (QWidget *w : dlg.findChildren<QWidget *>()) {
        if (auto *c = qobject_cast<QCheckBox *>(w)) {
            if (c->text() == QStringLiteral("Preview")) preview = c;
        }
        if (auto *cb = qobject_cast<QComboBox *>(w)) {
            for (int i = 0; i < cb->count(); ++i) {
                if (cb->itemText(i) == QStringLiteral("Fit")) zoom = cb;
            }
        }
    }
    CHECK(preview != nullptr);
    CHECK(zoom != nullptr);
    if (zoom) CHECK(zoom->currentText() == QStringLiteral("Fit"));
    if (!preview) return;
    const int depthBefore = state.undoDepth();
    preview->setChecked(false);
    QApplication::processEvents();
    CHECK_EQ(state.undoDepth(), depthBefore);
    preview->setChecked(true);
    QApplication::processEvents();
    QTimer::singleShot(0, &dlg, &QDialog::accept);
    CHECK(dlg.exec() == QDialog::Accepted);
    CHECK_EQ(state.undoDepth(), depthBefore + 1);
}

}

void testAveragePreviewKeepsSilhouette() {
    AppState state;
    DocumentItem *d = state.addDocument(QStringLiteral("probe"), QSize(200, 200), 72);
    if (!d) {
        CHECK(false);
        return;
    }
    QImage red(100, 100, QImage::Format_ARGB32);
    red.fill(Qt::transparent);
    QPainter p(&red);
    p.fillRect(10, 10, 80, 80, Qt::red);
    p.end();
    state.placeImageLayer(red, QStringLiteral("cutout"), QPointF(50, 30), 1.0);
    LayerItem *l = state.activeLayer();
    CHECK(l != nullptr && l->pixels);
    if (!l || !l->pixels) return;
    l->scaleX = 2.0;
    l->scaleY = 0.5;
    FilterDialog dlg(&state, QStringLiteral("average"));
    dlg.show();
    QApplication::processEvents();
    QApplication::processEvents();
    FilterPreviewCanvas *canvas = dlg.findChild<FilterPreviewCanvas *>();
    CHECK(canvas != nullptr);
    if (!canvas) return;
    const QImage shot = canvas->grab().toImage();
    int nonBg = 0;
    double sumX = 0, sumY = 0;
    int redCount = 0;
    for (int y = 0; y < shot.height(); y += 2) {
        for (int x = 0; x < shot.width(); x += 2) {
            const QColor c = shot.pixelColor(x, y);
            if (c.alpha() > 0 && c != QColor(43, 43, 43)) ++nonBg;
            if (c.red() > 150 && c.green() < 100 && c.blue() < 100 && c.alpha() > 128) {
                ++redCount;
                sumX += x;
                sumY += y;
            }
        }
    }
    CHECK(nonBg > 10);
    CHECK(redCount > 10);
    if (redCount > 0) {
        const QPointF expect(shot.width() * 0.5, shot.height() * 0.5);
        CHECK(std::fabs(sumX / redCount - expect.x()) < 5.0);
        CHECK(std::fabs(sumY / redCount - expect.y()) < 5.0);
    }
    {
        int minX = shot.width(), maxX = -1, minY = shot.height(), maxY = -1;
        for (int y = 0; y < shot.height(); y += 2) {
            for (int x = 0; x < shot.width(); x += 2) {
                const QColor c = shot.pixelColor(x, y);
                if (c.red() > 150 && c.green() < 100 && c.blue() < 100 && c.alpha() > 128) {
                    minX = std::min(minX, x);
                    maxX = std::max(maxX, x);
                    minY = std::min(minY, y);
                    maxY = std::max(maxY, y);
                }
            }
        }
        CHECK(maxX > minX && maxY > minY);
        if (maxX > minX && maxY > minY) {
            const double ratio = double(maxX - minX) / double(maxY - minY);
            CHECK(ratio > 3.0 && ratio < 5.0);
        }
    }
    CHECK(l->pixels->at(0, 0).a == 0.0f);
    CHECK(l->pixels->at(50, 50).a == 1.0f);
    CHECK(l->pixels->at(50, 50).r > 0.9f);
    dlg.reject();
}

void testGpuRoutedFiltersMatchCpu() {
    AppState state;
    DocumentItem *d = state.addDocument(QStringLiteral("probe"), QSize(64, 48), 72);
    CHECK(d != nullptr);
    if (!d) return;
    QImage grad(64, 48, QImage::Format_ARGB32);
    for (int y = 0; y < 48; ++y) {
        for (int x = 0; x < 64; ++x)
            grad.setPixel(x, y, qRgba(x * 4 % 256, y * 5 % 256, (x + y * 3) % 256, 255));
    }
    state.placeImageLayer(grad, QStringLiteral("grad"), QPointF(32, 24), 1.0);
    LayerItem *l = state.activeLayer();
    CHECK(l != nullptr && l->pixels);
    if (!l || !l->pixels) return;
    const char *ids[] = {"gaussian_blur", "box_blur",  "blur",       "blur_more",
                         "median",        "despeckle", "unsharp_mask", "sharpen",
                         "sharpen_more",  "motion_blur"};
    for (const char *id : ids) {
        const std::vector<double> params =
            pittore::filter::defaultParams(id);
        pittore::Image ref = l->pixels->clone();
        pittore::filter::applyFilter(ref, id, params);
        CHECK(state.applyFilterToActiveLayer(id, params));
        bool same = l->pixels->width() == ref.width() &&
                    l->pixels->height() == ref.height();
        if (same) {
            for (std::size_t i = 0; i < ref.pixel_count(); ++i) {
                for (int c = 0; c < 4; ++c) {
                    if (std::fabs(l->pixels->data()[i][c] - ref.data()[i][c]) > 1e-3f) {
                        same = false;
                        break;
                    }
                }
                if (!same) break;
            }
        }
        CHECK(same);
        *l->pixels = ref;
    }
    CHECK(state.applyFilterToActiveLayer("no_such_filter_xyz", {}));
}

void testColoredPencilIsVisible() {
    AppState state;
    DocumentItem *d = state.addDocument(QStringLiteral("probe"), QSize(64, 48), 72);
    CHECK(d != nullptr);
    if (!d) return;
    QImage grad(64, 48, QImage::Format_ARGB32);
    for (int y = 0; y < 48; ++y) {
        for (int x = 0; x < 64; ++x)
            grad.setPixel(x, y, qRgba(x * 4 % 256, y * 5 % 256, (x * 7 + y * 3) % 256, 255));
    }
    state.placeImageLayer(grad, QStringLiteral("grad"), QPointF(32, 24), 1.0);
    LayerItem *l = state.activeLayer();
    CHECK(l != nullptr && l->pixels);
    if (!l || !l->pixels) return;
    pittore::Image before = l->pixels->clone();
    CHECK(state.applyFilterToActiveLayer(
        "colored_pencil", pittore::filter::defaultParams("colored_pencil")));
    double diff = 0;
    for (std::size_t i = 0; i < before.pixel_count(); ++i) {
        diff += std::fabs(l->pixels->data()[i].r - before.data()[i].r) +
                std::fabs(l->pixels->data()[i].g - before.data()[i].g) +
                std::fabs(l->pixels->data()[i].b - before.data()[i].b);
    }
    diff /= before.pixel_count() * 3;
    CHECK(diff > 0.02);
}

void testProxyPreviewLargeAndNoCompound() {
    AppState state;
    state.addDocument(QStringLiteral("big"), QSize(1200, 900), 72);
    QImage big(1200, 900, QImage::Format_ARGB32);
    for (int y = 0; y < 900; ++y)
        for (int x = 0; x < 1200; ++x)
            big.setPixel(x, y, qRgba(x % 256, y % 256, (x * 7 + y * 3) % 256, 255));
    state.placeImageLayer(big, QStringLiteral("big"), QPointF(600, 450), 1.0);
    LayerItem *l = state.activeLayer();
    CHECK(l != nullptr && l->pixels);
    if (!l || !l->pixels) return;
    pittore::Image before = l->pixels->clone();
    const int depthBefore = state.undoDepth();
    {
        FilterDialog dlg(&state, QStringLiteral("colored_pencil"));
        CHECK(dlg.showingProxyPreview());
        auto *spin = dlg.findChild<QDoubleSpinBox *>();
        if (spin) spin->setValue(10.0);
        QApplication::processEvents();
        QTimer::singleShot(0, &dlg, &QDialog::accept);
        CHECK(dlg.exec() == QDialog::Accepted);
    }
    CHECK_EQ(state.undoDepth(), depthBefore + 1);
    double diff = 0;
    for (std::size_t i = 0; i < before.pixel_count(); ++i) {
        diff += std::fabs(l->pixels->data()[i].r - before.data()[i].r) +
                std::fabs(l->pixels->data()[i].g - before.data()[i].g) +
                std::fabs(l->pixels->data()[i].b - before.data()[i].b);
    }
    l = state.activeLayer();
    CHECK(l != nullptr && l->pixels);
    diff = 0;
    if (l && l->pixels) {
        for (std::size_t i = 0; i < before.pixel_count(); ++i) {
            diff += std::fabs(l->pixels->data()[i].r - before.data()[i].r) +
                    std::fabs(l->pixels->data()[i].g - before.data()[i].g) +
                    std::fabs(l->pixels->data()[i].b - before.data()[i].b);
        }
    }
    CHECK(diff / (before.pixel_count() * 3) > 0.02);
    AppState small;
    small.addDocument(QStringLiteral("probe"), QSize(48, 36), 72);
    QImage grad(48, 36, QImage::Format_ARGB32);
    for (int y = 0; y < 36; ++y)
        for (int x = 0; x < 48; ++x)
            grad.setPixel(x, y, qRgba(x * 5 % 256, y * 7 % 256, (x * 7 + y * 3) % 256, 255));
    small.placeImageLayer(grad, QStringLiteral("grad"), QPointF(24, 18), 1.0);
    LayerItem *sl = small.activeLayer();
    CHECK(sl != nullptr && sl->pixels);
    if (!sl || !sl->pixels) return;
    pittore::Image sbefore = sl->pixels->clone();
    {
        FilterDialog dlg(&small, QStringLiteral("colored_pencil"));
        CHECK(!dlg.showingProxyPreview());
        auto *spin = dlg.findChild<QDoubleSpinBox *>();
        if (spin) spin->setValue(10.0);
        QEventLoop w1;
        QTimer::singleShot(150, &w1, &QEventLoop::quit);
        w1.exec();
        if (spin) spin->setValue(14.0);
        QEventLoop w2;
        QTimer::singleShot(150, &w2, &QEventLoop::quit);
        w2.exec();
        pittore::Image ref = sbefore.clone();
        pittore::filter::applyFilter(ref, "colored_pencil", dlg.appliedParams());
        float mx = 0;
        for (std::size_t i = 0; i < ref.pixel_count(); ++i)
            for (int c = 0; c < 3; ++c)
                mx = std::max(mx, std::fabs(sl->pixels->data()[i][c] - ref.data()[i][c]));
        sl = small.activeLayer();
        mx = 0;
        if (sl && sl->pixels) {
            for (std::size_t i = 0; i < ref.pixel_count(); ++i)
                for (int c = 0; c < 3; ++c)
                    mx = std::max(mx, std::fabs(sl->pixels->data()[i][c] - ref.data()[i][c]));
        }
        CHECK(mx < 1e-4f);
        QTimer::singleShot(0, &dlg, &QDialog::reject);
        CHECK(dlg.exec() == QDialog::Rejected);
    }
}

static void testFilterUi() {
    int fakeargc = 1;
    char *fakeargv[] = {(char *)"test_filter_ui", nullptr};
    QApplication app(fakeargc, fakeargv);
    testEveryPopupBuilds();
    testPopupAcceptsLivePreview();
    testPreviewToggleAndFitDefault();
    testAveragePreviewKeepsSilhouette();
    testGpuRoutedFiltersMatchCpu();
    testColoredPencilIsVisible();
    testProxyPreviewLargeAndNoCompound();
}

#ifndef PITTORE_TEST_NO_MAIN
TEST_MAIN_CALL(testFilterUi)
#endif
