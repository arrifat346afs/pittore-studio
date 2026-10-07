// test_live_filter.cpp — live (Smart) filter layers.
//
// A pixel layer with a box_blur recipe composites exactly what the
// destructive engine produces; enable/disable/remove/undo behave; the
// recipe (not pixels) round-trips through .ifp; PSD export bakes the
// filtered render.
//
// AppState harness like test_place_ui (headless, scratch XDG_CONFIG_HOME).
#include <QCoreApplication>
#include <QDir>
#include <QTemporaryDir>

#include <cmath>
#include <cstdio>

#include "engine/core/log.h"
#include "engine/filter/filters.h"
#include "engine/filter/registry/filter_registry.h"
#include "engine/io/psd.h"
#include "test_util.h"
#include "ui/app_state.h"
#include "ui/live_filter.h"
#include "ui/psd_export.h"

using namespace pittore::ui;

namespace {

std::shared_ptr<pittore::Image> gradientImg(int w, int h) {
    // Deliberately non-linear so different blur radii land on different
    // values (a linear ramp is preserved by any box blur).
    auto img = std::make_shared<pittore::Image>(
        static_cast<std::uint32_t>(w), static_cast<std::uint32_t>(h));
    for (int y = 0; y < h; ++y)
        for (int x = 0; x < w; ++x) {
            const float v =
                static_cast<float>((x * x * 7 + y * y * 13 + x * y * 3) % 251) /
                251.0f;
            img->data()[y * w + x] = {v, 1.0f - v, v * v, 1.0f};
        }
    return img;
}

DocumentItem* makeDoc(AppState& state) {
    DocumentItem* doc = state.addDocument(QStringLiteral("t"), QSize(8, 4), 72);
    if (!doc) return nullptr;
    LayerItem l;
    l.name = QStringLiteral("photo");
    l.kind = LayerItem::Kind::Pixel;
    l.pixels = gradientImg(8, 4);
    l.sourceStamp = 1;
    doc->layers = {l};
    state.setActiveLayerIndex(0);
    return doc;
}

bool nearPx(const pittore::RGBAf& a, const pittore::RGBAf& b, float eps) {
    return std::abs(a.r - b.r) <= eps && std::abs(a.g - b.g) <= eps &&
           std::abs(a.b - b.b) <= eps && std::abs(a.a - b.a) <= eps;
}

}  // namespace

static void test_render_equivalence() {
    // Live recipe renders exactly what the destructive engine produces.
    AppState state;
    DocumentItem* doc = makeDoc(state);
    CHECK(doc != nullptr);
    if (!doc) return;
    CHECK(state.convertToLiveFilter(QStringLiteral("box_blur")));
    const LayerItem* l = state.activeLayer();
    CHECK(l && l->hasLiveFilter);
    if (!l) return;
    ensureLayerFilter(*l);
    auto ref = std::make_shared<pittore::Image>(*l->pixels);
    pittore::filter::applyFilter(*ref, "box_blur", l->liveFilterParams);
    const pittore::Image* got = liveFilterBase(*l);
    CHECK(got != nullptr && got != l->pixels.get());
    if (!got) return;
    bool same = got->width() == ref->width() && got->height() == ref->height();
    for (std::uint32_t i = 0; same && i < got->width() * got->height(); ++i)
        same = nearPx(got->data()[i], ref->data()[i], 1e-6f);
    CHECK(same);
    // ... and the composite shows filtered, not native, content.
    doc->rebuildComposite();
    const QColor c = doc->composite.pixelColor(2, 2);
    const pittore::RGBAf& f = got->data()[2 * 8 + 2];
    CHECK(std::abs(c.red() / 255.0f - f.r * f.a) < 0.01f);
}

static void test_ops_undo() {
    AppState state;
    DocumentItem* doc = makeDoc(state);
    CHECK(doc != nullptr);
    if (!doc) return;
    auto native = std::make_shared<pittore::Image>(*doc->layers[0].pixels);

    CHECK(state.convertToLiveFilter(QStringLiteral("box_blur")));
    CHECK(doc->layers[0].hasLiveFilter);
    // Natives untouched by conversion.
    bool pristine = true;
    for (int i = 0; pristine && i < 32; ++i)
        pristine = nearPx(doc->layers[0].pixels->data()[i], native->data()[i],
                          0.0f);
    CHECK(pristine);
    // Disable reveals natives in the composite.
    CHECK(state.setLiveFilterEnabled(false));
    doc->rebuildComposite();
    const QColor c = doc->composite.pixelColor(2, 2);
    const pittore::RGBAf& n = native->data()[2 * 8 + 2];
    CHECK(std::abs(c.red() / 255.0f - n.r) < 0.01f);
    CHECK(state.setLiveFilterEnabled(true));
    // Param edits apply live.
    const QColor before = doc->composite.pixelColor(2, 2);
    CHECK(state.setLiveFilterParam(0, 3.0));
    doc->rebuildComposite();
    const QColor after = doc->composite.pixelColor(2, 2);
    CHECK(before.red() != after.red() || before.green() != after.green() ||
          before.blue() != after.blue());
    // Remove reveals natives; undo restores the recipe.
    CHECK(state.removeLiveFilter());
    CHECK(!doc->layers[0].hasLiveFilter);
    CHECK(!state.removeLiveFilter());  // twice refuses
    state.undo();
    CHECK(state.activeDocument()->layers[0].hasLiveFilter);
}

static void test_ifp_roundtrip() {
    AppState state;
    DocumentItem* doc = makeDoc(state);
    CHECK(doc != nullptr);
    if (!doc) return;
    CHECK(state.convertToLiveFilter(QStringLiteral("box_blur")));
    CHECK(state.setLiveFilterParam(0, 2.0));
    QTemporaryDir tmp;
    CHECK(tmp.isValid());
    if (!tmp.isValid()) return;
    const QString path = tmp.filePath(QStringLiteral("live.ifp"));
    QString error;
    CHECK(state.saveProject(path, &error));
    if (!error.isEmpty())
        std::fprintf(stderr, "    save error: %s\n",
                     error.toLocal8Bit().constData());
    CHECK(state.openProject(path, &error));
    if (!error.isEmpty())
        std::fprintf(stderr, "    open error: %s\n",
                     error.toLocal8Bit().constData());
    DocumentItem* re = state.activeDocument();
    CHECK(re != nullptr);
    if (!re || re->layers.isEmpty()) return;
    const LayerItem& l = re->layers[0];
    CHECK(l.hasLiveFilter);
    CHECK(l.liveFilterEnabled);
    CHECK(l.liveFilterId == QStringLiteral("box_blur"));
    CHECK(l.liveFilterParams.size() == 1);
    if (!l.liveFilterParams.empty())
        CHECK(std::abs(l.liveFilterParams[0] - 2.0) < 1e-9);
    CHECK(l.pixels);
}

static void test_psd_bake() {
    // PSD has no live-filter record: the filtered render bakes to pixels.
    AppState state;
    DocumentItem* doc = makeDoc(state);
    CHECK(doc != nullptr);
    if (!doc) return;
    CHECK(state.convertToLiveFilter(QStringLiteral("box_blur")));
    const pittore::io::PsdLayersDoc out = buildLayeredPsdDoc(*doc);
    CHECK(!out.layers.empty());
    if (out.layers.empty()) return;
    const pittore::io::PsdLayerFile& f = out.layers[0];
    CHECK(!f.isAdjustment && !f.rgba.empty());
    const LayerItem* l = state.activeLayer();
    ensureLayerFilter(*l);
    const pittore::Image* rendered = liveFilterBase(*l);
    CHECK(rendered != nullptr);
    if (!rendered || f.rgba.empty()) return;
    // u16 quantization: compare at 8-bit tolerance.
    bool same = f.width == rendered->width() && f.height == rendered->height();
    for (std::uint32_t i = 0; same && i < f.width * f.height; ++i) {
        const float rr = f.rgba[i * 4] / 65535.0f;
        if (std::abs(rr - rendered->data()[i].r) > 2.0f / 255.0f) same = false;
    }
    CHECK(same);
}

int main(int argc, char** argv) {
    QCoreApplication app(argc, argv);
    const QString logDir =
        QDir::tempPath() + QStringLiteral("/pittore-live-filter-log");
    QDir().mkpath(logDir);
    ::pittore::core::log::set_log_dir(logDir.toStdString().c_str());
    test_render_equivalence();
    test_ops_undo();
    test_ifp_roundtrip();
    test_psd_bake();
    const int rc = pittore_test::failures() == 0 ? 0 : 1;
    std::printf("  checks=%d failures=%d\n", pittore_test::checks(),
                pittore_test::failures());
    return rc;
}
