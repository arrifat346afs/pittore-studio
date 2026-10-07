// test_mask_ops.cpp — mask density / feather / apply (mask polish slice).
//
// Pure finishMaskImage checks plus AppState setter/apply behaviour through
// the live composite (CPU or GPU backend — coverage bytes are identical).
#include <QCoreApplication>
#include <QDir>

#include <cmath>
#include <cstdio>

#include "engine/compute/layer_mask.h"
#include "engine/core/log.h"
#include "test_util.h"
#include "ui/app_state.h"
#include "ui/mask_finish.h"

using namespace pittore::ui;

namespace {

std::shared_ptr<pittore::Image> halfMask() {
    auto m = std::make_shared<pittore::Image>(8, 4);
    for (int y = 0; y < 4; ++y)
        for (int x = 0; x < 8; ++x)
            m->data()[y * 8 + x] =
                pittore::compute::make_mask_pixel(x < 4 ? 1.0f : 0.0f);
    return m;
}

std::shared_ptr<pittore::Image> whiteImg() {
    auto img = std::make_shared<pittore::Image>(8, 4);
    img->fill(pittore::RGBAf{1, 1, 1, 1});
    return img;
}

}  // namespace

static void test_finish_unit() {
    auto m = halfMask();
    // Identity parameters: exact copy.
    auto same = finishMaskImage(*m, 1.0f, 0.0f);
    CHECK_EQ(same->width(), 8u);
    bool copyOk = true;
    for (int i = 0; copyOk && i < 32; ++i)
        copyOk = same->data()[i].r == m->data()[i].r;
    CHECK(copyOk);
    // Density halves coverage, alpha stays opaque.
    auto half = finishMaskImage(*m, 0.5f, 0.0f);
    CHECK_NEAR(half->data()[0].r, 0.5f, 1e-6f);
    CHECK_NEAR(half->data()[7].r, 0.0f, 1e-6f);
    CHECK_NEAR(half->data()[0].a, 1.0f, 1e-6f);
    // Feather spreads the edge both ways, far field untouched (32px wide
    // so the blur never reaches the ends).
    auto wide = std::make_shared<pittore::Image>(32, 4);
    for (int y = 0; y < 4; ++y)
        for (int x = 0; x < 32; ++x)
            wide->data()[y * 32 + x] =
                pittore::compute::make_mask_pixel(x < 16 ? 1.0f : 0.0f);
    auto soft = finishMaskImage(*wide, 1.0f, 2.0f);
    CHECK(soft->data()[15].r < 1.0f);
    CHECK(soft->data()[16].r > 0.0f);
    CHECK_NEAR(soft->data()[0].r, 1.0f, 0.01f);
    CHECK_NEAR(soft->data()[32 * 4 - 1].r, 0.0f, 0.01f);
}

static void test_density_live() {
    AppState state;
    DocumentItem* doc = state.addDocument(QStringLiteral("t"), QSize(8, 4), 72);
    CHECK(doc != nullptr);
    if (!doc) return;
    LayerItem l;
    l.name = QStringLiteral("w");
    l.kind = LayerItem::Kind::Pixel;
    l.pixels = whiteImg();
    l.sourceStamp = 1;
    l.mask = halfMask();
    l.hasMask = true;
    doc->layers = {l};

    doc->rebuildComposite();
    CHECK_EQ(doc->composite.pixelColor(1, 1).alpha(), 255);
    CHECK_EQ(doc->composite.pixelColor(6, 1).alpha(), 0);
    CHECK(state.setLayerMaskDensity(0.5f));
    CHECK_NEAR(doc->composite.pixelColor(1, 1).alpha(), 128, 2);
    CHECK_EQ(doc->composite.pixelColor(6, 1).alpha(), 0);
    CHECK(!state.setLayerMaskDensity(0.5f));  // no-op, same value
    CHECK(state.setLayerMaskDensity(1.0f));
    CHECK_EQ(doc->composite.pixelColor(1, 1).alpha(), 255);
}

static void test_feather_live() {
    AppState state;
    DocumentItem* doc = state.addDocument(QStringLiteral("t"), QSize(8, 4), 72);
    CHECK(doc != nullptr);
    if (!doc) return;
    LayerItem l;
    l.name = QStringLiteral("w");
    l.kind = LayerItem::Kind::Pixel;
    l.pixels = whiteImg();
    l.sourceStamp = 1;
    l.mask = halfMask();
    l.hasMask = true;
    doc->layers = {l};
    doc->rebuildComposite();

    CHECK(state.setLayerMaskFeather(2.0f));
    const int edge = doc->composite.pixelColor(3, 1).alpha();
    const int far = doc->composite.pixelColor(0, 1).alpha();
    // The 8px fixture is narrow, so the blur reaches everywhere: the edge
    // softens most, the far pixel only slightly.
    CHECK(edge < far && far < 255);
    CHECK(edge > 0);
    CHECK(state.setLayerMaskFeather(0.0f));
    CHECK_EQ(doc->composite.pixelColor(3, 1).alpha(), 255);
}

static void test_apply() {
    AppState state;
    DocumentItem* doc = state.addDocument(QStringLiteral("t"), QSize(8, 4), 72);
    CHECK(doc != nullptr);
    if (!doc) return;
    LayerItem l;
    l.name = QStringLiteral("w");
    l.kind = LayerItem::Kind::Pixel;
    l.pixels = whiteImg();
    l.sourceStamp = 1;
    l.mask = halfMask();
    l.hasMask = true;
    l.maskDensity = 0.5f;
    doc->layers = {l};
    state.setActiveLayerIndex(0);

    CHECK(state.applyLayerMask());
    DocumentItem* d = state.activeDocument();
    CHECK(d != nullptr);
    if (!d) return;
    CHECK(!d->layers[0].hasMask);
    CHECK(!d->layers[0].mask);
    CHECK(d->layers[0].pixels);
    if (d->layers[0].pixels) {
        // Density baked: left alpha ~0.5, right 0.
        CHECK_NEAR(d->layers[0].pixels->data()[0].a, 0.5f, 0.01f);
        CHECK_NEAR(d->layers[0].pixels->data()[7].a, 0.0f, 1e-6f);
    }
    // One undo step restores the mask.
    state.undo();
    CHECK(state.activeDocument()->layers[0].hasMask);
}

static void test_patch_region() {
    // 64x32 step mask, feathered + density: patch must equal a full finish
    // inside the dirty rect (halo-exact), and leave the rest untouched.
    auto m = std::make_shared<pittore::Image>(64, 32);
    for (int y = 0; y < 32; ++y)
        for (int x = 0; x < 64; ++x)
            m->data()[y * 64 + x] =
                pittore::compute::make_mask_pixel(x < 32 ? 1.0f : 0.0f);
    auto finished = finishMaskImage(*m, 0.7f, 5.0f);
    // Perturb the raw mask in a small rect, as a dab would.
    for (int y = 10; y < 18; ++y)
        for (int x = 20; x < 30; ++x)
            m->data()[y * 64 + x] = pittore::compute::make_mask_pixel(0.2f);
    CHECK(patchFinishedMaskRegion(*m, 0.7f, 5.0f, 20, 10, 30, 18, *finished));
    auto expect = finishMaskImage(*m, 0.7f, 5.0f);
    bool exact = true;
    for (int y = 10; y < 18 && exact; ++y)
        for (int x = 20; x < 30 && exact; ++x) {
            const auto& a = finished->data()[y * 64 + x];
            const auto& b = expect->data()[y * 64 + x];
            exact = a.r == b.r && a.g == b.g && a.b == b.b && a.a == b.a;
        }
    CHECK(exact);
    // Empty rect: true, finished untouched.
    auto before = *finished;
    CHECK(patchFinishedMaskRegion(*m, 0.7f, 5.0f, 5, 5, 5, 9, *finished));
    bool same = true;
    for (std::size_t i = 0; same && i < before.pixel_count(); ++i)
        same = before.data()[i].r == finished->data()[i].r;
    CHECK(same);
    // Size mismatch: false (caller falls back to full finish).
    pittore::Image small(8, 8);
    CHECK(!patchFinishedMaskRegion(*m, 0.7f, 5.0f, 0, 0, 8, 8, small));
    // Density-only (no blur) patch path.
    auto d0 = finishMaskImage(*m, 0.5f, 0.0f);
    for (int y = 2; y < 6; ++y)
        for (int x = 2; x < 6; ++x)
            m->data()[y * 64 + x] = pittore::compute::make_mask_pixel(0.9f);
    CHECK(patchFinishedMaskRegion(*m, 0.5f, 0.0f, 2, 2, 6, 6, *d0));
    auto d1 = finishMaskImage(*m, 0.5f, 0.0f);
    exact = true;
    for (int y = 2; y < 6 && exact; ++y)
        for (int x = 2; x < 6 && exact; ++x)
            exact = d0->data()[y * 64 + x].r == d1->data()[y * 64 + x].r;
    CHECK(exact);
}

int main(int argc, char** argv) {
    QCoreApplication app(argc, argv);
    const QString logDir =
        QDir::tempPath() + QStringLiteral("/pittore-mask-ops-log");
    QDir().mkpath(logDir);
    ::pittore::core::log::set_log_dir(logDir.toStdString().c_str());
    test_finish_unit();
    test_patch_region();
    test_density_live();
    test_feather_live();
    test_apply();
    const int rc = pittore_test::failures() == 0 ? 0 : 1;
    std::printf("  checks=%d failures=%d\n", pittore_test::checks(),
                pittore_test::failures());
    return rc;
}
