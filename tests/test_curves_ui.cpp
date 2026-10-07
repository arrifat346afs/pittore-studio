// test_curves_ui.cpp — live Curves + Levels through AppState.
//
// A black pixel layer under a Curves adjustment with an R-only lift:
// red rises, green/blue stay put. Covers the channel setter guards, the
// folded 768-entry LUT and the live composite. Plus a grey layer under a
// Levels adjustment: the 256-entry table builds, rebuilds on param edits,
// and composites within table quantization.
#include <QCoreApplication>
#include <QDir>

#include <cmath>
#include <cstdio>

#include "engine/compute/adjust.h"
#include "engine/core/log.h"
#include "test_util.h"
#include "ui/app_state.h"

using namespace pittore::ui;

static void test_curves_channels_ui() {
    AppState state;
    DocumentItem* doc = state.addDocument(QStringLiteral("t"), QSize(8, 4), 72);
    CHECK(doc != nullptr);
    if (!doc) return;

    LayerItem px;
    px.name = QStringLiteral("black");
    px.kind = LayerItem::Kind::Pixel;
    auto img = std::make_shared<pittore::Image>(8, 4);
    img->fill(pittore::RGBAf{0, 0, 0, 1});
    px.pixels = std::move(img);
    px.sourceStamp = 1;

    LayerItem cu;
    cu.name = QStringLiteral("curves");
    cu.kind = LayerItem::Kind::Adjustment;
    cu.adjustmentKind =
        static_cast<int>(pittore::compute::AdjustmentKind::Curves);
    cu.adjustmentCurve = {QPointF(0, 0), QPointF(1, 1)};

    // Panel order, top first.
    doc->layers = {cu, px};
    state.setActiveLayerIndex(0);

    // Guards: bad channel, and pixel layers take no curves.
    CHECK(!state.setAdjustmentCurveForChannel(-1, {{0, 0}, {1, 1}}));
    CHECK(!state.setAdjustmentCurveForChannel(3, {{0, 0}, {1, 1}}));
    state.setActiveLayerIndex(1);
    CHECK(!state.setAdjustmentCurveForChannel(0, {{0, 0}, {1, 1}}));
    state.setActiveLayerIndex(0);

    // R-only lift of the shadows.
    CHECK(state.setAdjustmentCurveForChannel(
        0, {QPointF(0, 0.5), QPointF(1, 1)}));
    DocumentItem* d = state.activeDocument();
    CHECK(d != nullptr);
    if (!d) return;
    CHECK_EQ(d->layers[0].adjustmentLUT.size(), 768u);
    const QColor c = d->composite.pixelColor(2, 2);
    CHECK(std::abs(c.red() - 128) <= 2);
    CHECK_EQ(c.green(), 0);
    CHECK_EQ(c.blue(), 0);

    // Master still applies on top: halve everything.
    CHECK(state.setAdjustmentCurve({QPointF(0, 0), QPointF(1, 0.5)}));
    const QColor c2 = state.activeDocument()->composite.pixelColor(2, 2);
    CHECK(std::abs(c2.red() - 64) <= 2);
    CHECK_EQ(c2.green(), 0);
    CHECK_EQ(c2.blue(), 0);
}

static void test_levels_live_ui() {
    // Grey pixel layer under a Levels adjustment: the 256-entry table builds
    // on creation, rebuilds on param edits, and the composite matches a
    // direct evaluation within table quantization.
    AppState state;
    DocumentItem* doc = state.addDocument(QStringLiteral("t"), QSize(8, 4), 72);
    CHECK(doc != nullptr);
    if (!doc) return;

    LayerItem px;
    px.name = QStringLiteral("grey");
    px.kind = LayerItem::Kind::Pixel;
    auto img = std::make_shared<pittore::Image>(8, 4);
    img->fill(pittore::RGBAf{0.5f, 0.5f, 0.5f, 1});
    px.pixels = std::move(img);
    px.sourceStamp = 1;

    LayerItem lv;
    lv.name = QStringLiteral("levels");
    lv.kind = LayerItem::Kind::Adjustment;
    lv.adjustmentKind =
        static_cast<int>(pittore::compute::AdjustmentKind::Levels);
    lv.adjustmentParams[0] = 0.0f;
    lv.adjustmentParams[1] = 1.0f;
    lv.adjustmentParams[2] = 2.0f;  // gamma 2 lightens mid grey (1/gamma)
    lv.adjustmentParams[3] = 0.0f;
    lv.adjustmentParams[4] = 1.0f;
    rebuildAdjustmentLUT(lv);

    doc->layers = {lv, px};
    state.setActiveLayerIndex(0);
    DocumentItem* d = state.activeDocument();
    CHECK(d != nullptr);
    if (!d) return;
    d->rebuildComposite();
    CHECK_EQ(d->layers[0].adjustmentLUT.size(), 256u);
    // levels(0.5, gamma 2) = sqrt(0.5) ~= 0.707 -> ~180.
    const QColor c = d->composite.pixelColor(2, 2);
    CHECK(std::abs(c.red() - 180) <= 2);
    CHECK(std::abs(c.green() - 180) <= 2);
    CHECK(std::abs(c.blue() - 180) <= 2);
    // Param edits rebuild the table live: gamma 1 restores mid grey.
    CHECK(state.setAdjustmentParam(2, 1.0f));
    CHECK_EQ(state.activeDocument()->layers[0].adjustmentLUT.size(), 256u);
    const QColor c2 = state.activeDocument()->composite.pixelColor(2, 2);
    CHECK(std::abs(c2.red() - 128) <= 2);
}

static LayerItem greyPixelLayer() {
    LayerItem px;
    px.name = QStringLiteral("grey");
    px.kind = LayerItem::Kind::Pixel;
    auto img = std::make_shared<pittore::Image>(8, 4);
    img->fill(pittore::RGBAf{0.5f, 0.5f, 0.5f, 1});
    px.pixels = std::move(img);
    px.sourceStamp = 1;
    return px;
}

static void test_photo_filter_live_ui() {
    // Grey pixels under warming Photo Filter 25%: composite ~(148,125,96).
    AppState state;
    DocumentItem* doc = state.addDocument(QStringLiteral("t"), QSize(8, 4), 72);
    CHECK(doc != nullptr);
    if (!doc) return;

    LayerItem pf;
    pf.name = QStringLiteral("photo filter");
    pf.kind = LayerItem::Kind::Adjustment;
    pf.adjustmentKind =
        static_cast<int>(pittore::compute::AdjustmentKind::PhotoFilter);
    pf.adjustmentParams[0] = 1.0f;
    pf.adjustmentParams[1] = 0.55f;
    pf.adjustmentParams[2] = 0.0f;
    pf.adjustmentParams[3] = 0.25f;
    pf.adjustmentParams[4] = 0.0f;

    doc->layers = {pf, greyPixelLayer()};
    state.setActiveLayerIndex(0);
    DocumentItem* d = state.activeDocument();
    CHECK(d != nullptr);
    if (!d) return;
    d->rebuildComposite();
    const QColor c = d->composite.pixelColor(2, 2);
    CHECK(std::abs(c.red() - 148) <= 2);
    CHECK(std::abs(c.green() - 125) <= 2);
    CHECK(std::abs(c.blue() - 96) <= 2);
}

static void test_adjustment_reset_preview_ui() {
    // Reset restores creation defaults; press-hold compare hides/restores
    // the row and leaves visibility and history unchanged.
    AppState state;
    DocumentItem* doc = state.addDocument(QStringLiteral("t"), QSize(8, 4), 72);
    CHECK(doc != nullptr);
    if (!doc) return;

    LayerItem pf;
    pf.name = QStringLiteral("photo filter");
    pf.kind = LayerItem::Kind::Adjustment;
    pf.adjustmentKind =
        static_cast<int>(pittore::compute::AdjustmentKind::PhotoFilter);
    doc->layers = {pf, greyPixelLayer()};
    state.setActiveLayerIndex(0);

    // Junk the density, then reset: Warming-85 @ 25%, luminosity on.
    CHECK(state.setAdjustmentParam(0, 0.0f));
    CHECK(state.setAdjustmentParam(3, 1.0f));
    CHECK(state.resetAdjustmentToDefaults());
    {
        const LayerItem& l = state.activeDocument()->layers[0];
        CHECK(std::abs(l.adjustmentParams[0] - 236.0f / 255.0f) < 1e-6f);
        CHECK(std::abs(l.adjustmentParams[3] - 0.25f) < 1e-6f);
        CHECK(std::abs(l.adjustmentParams[4] - 1.0f) < 1e-6f);
    }

    // Compare hides the row (bare grey shows) and restores it on release.
    state.beginAdjustmentPreview();
    CHECK(!state.activeDocument()->layers[0].visible);
    const QColor bare = state.activeDocument()->composite.pixelColor(2, 2);
    CHECK(std::abs(bare.red() - 128) <= 2);
    state.endAdjustmentPreview();
    CHECK(state.activeDocument()->layers[0].visible);
    const QColor graded =
        state.activeDocument()->composite.pixelColor(2, 2);
    CHECK(std::abs(graded.red() - bare.red()) > 2);  // warming moved it

    // Guards: pixel layer active -> begin is a no-op; end without
    // begin is a no-op.
    state.setActiveLayerIndex(1);
    state.beginAdjustmentPreview();
    CHECK(state.activeDocument()->layers[0].visible);
    state.setActiveLayerIndex(0);
    state.endAdjustmentPreview();  // no begin outstanding: no-op
    CHECK(state.activeDocument()->layers[0].visible);
    // Guard: reset on a pixel layer fails cleanly.
    state.setActiveLayerIndex(1);
    CHECK(!state.resetAdjustmentToDefaults());
    // White Balance defaults land on 6500K / 0 tint.
    state.activeDocument()->layers[0].adjustmentKind =
        static_cast<int>(pittore::compute::AdjustmentKind::WhiteBalance);
    state.setActiveLayerIndex(0);
    CHECK(state.resetAdjustmentToDefaults());
    {
        const LayerItem& l = state.activeDocument()->layers[0];
        CHECK(std::abs(l.adjustmentParams[0] - 6500.0f) < 1e-6f);
        CHECK(std::abs(l.adjustmentParams[1] - 0.0f) < 1e-6f);
    }
    // Black & White defaults land on 40/60/40/60/20/80.
    state.activeDocument()->layers[0].adjustmentKind =
        static_cast<int>(pittore::compute::AdjustmentKind::BlackWhite);
    CHECK(state.resetAdjustmentToDefaults());
    {
        const LayerItem& l = state.activeDocument()->layers[0];
        const float want[6] = {40.0f, 60.0f, 40.0f, 60.0f, 20.0f, 80.0f};
        for (int i = 0; i < 6; ++i)
            CHECK(std::abs(l.adjustmentParams[i] - want[i]) < 1e-6f);
    }
    // Channel Mixer defaults land on the identity matrix.
    state.activeDocument()->layers[0].adjustmentKind =
        static_cast<int>(pittore::compute::AdjustmentKind::ChannelMixer);
    CHECK(state.resetAdjustmentToDefaults());
    {
        const LayerItem& l = state.activeDocument()->layers[0];
        CHECK(std::abs(l.adjustmentParams[0] - 1.0f) < 1e-6f);
        CHECK(std::abs(l.adjustmentParams[4] - 1.0f) < 1e-6f);
        CHECK(std::abs(l.adjustmentParams[8] - 1.0f) < 1e-6f);
        CHECK(std::abs(l.adjustmentParams[1] - 0.0f) < 1e-6f);
        CHECK(std::abs(l.adjustmentParams[12] - 0.0f) < 1e-6f);
    }
    // Color Balance defaults land neutral with luminosity preserved.
    state.activeDocument()->layers[0].adjustmentKind =
        static_cast<int>(pittore::compute::AdjustmentKind::ColorBalance);
    CHECK(state.resetAdjustmentToDefaults());
    {
        const LayerItem& l = state.activeDocument()->layers[0];
        for (int i = 0; i < 9; ++i)
            CHECK(std::abs(l.adjustmentParams[i] - 0.0f) < 1e-6f);
        CHECK(std::abs(l.adjustmentParams[9] - 1.0f) < 1e-6f);
    }
    // Midtones Magenta-Green -100 turns the grey pixels green.
    CHECK(state.setAdjustmentParam(4, -100.0f));
    {
        const QColor c =
            state.activeDocument()->composite.pixelColor(2, 2);
        CHECK(std::abs(c.red() - 0) <= 2);
        CHECK(std::abs(c.green() - 255) <= 2);
        CHECK(std::abs(c.blue() - 0) <= 2);
    }
    // Red under B&W defaults composites to grey 102.
    state.activeDocument()->layers[0].adjustmentKind =
        static_cast<int>(pittore::compute::AdjustmentKind::BlackWhite);
    CHECK(state.resetAdjustmentToDefaults());
    {
        pittore::Image* img =
            state.activeDocument()->layers[1].pixels.get();
        for (std::uint32_t i = 0; i < img->width() * img->height(); ++i)
            img->data()[i] = pittore::RGBAf{1.0f, 0.0f, 0.0f, 1.0f};
        state.activeDocument()->layers[1].sourceStamp++;
        state.activeDocument()->rebuildComposite();
        const QColor c =
            state.activeDocument()->composite.pixelColor(2, 2);
        CHECK(std::abs(c.red() - 102) <= 2);
        CHECK(std::abs(c.green() - 102) <= 2);
        CHECK(std::abs(c.blue() - 102) <= 2);
    }
}

int main(int argc, char** argv) {
    QCoreApplication app(argc, argv);
    const QString logDir =
        QDir::tempPath() + QStringLiteral("/pittore-curves-ui-log");
    QDir().mkpath(logDir);
    ::pittore::core::log::set_log_dir(logDir.toStdString().c_str());
    test_curves_channels_ui();
    test_levels_live_ui();
    test_photo_filter_live_ui();
    test_adjustment_reset_preview_ui();
    const int rc = pittore_test::failures() == 0 ? 0 : 1;
    std::printf("  checks=%d failures=%d\n", pittore_test::checks(),
                pittore_test::failures());
    return rc;
}
