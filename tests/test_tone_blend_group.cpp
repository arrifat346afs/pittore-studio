// Live Tone Blend Group compositor integration: a flagged group re-grades
// its children against the composited backdrop beneath it (CPU path);
// strength 0 reproduces flat compositing; the flag rides undo snapshots.
// Runs headless (QT_QPA_PLATFORM=offscreen) with the CPU backend; the GPU
// segmentation shares the span/slice machinery and gets parity coverage
// with the device kernels (Phase D).
#include <QApplication>
#include <QCoreApplication>
#include <QDir>
#include <QImage>
#include <QSlider>
#include <QTemporaryDir>

#include <cmath>
#include <cstddef>
#include <cstring>

#include "engine/core/log.h"
#include "engine/compute/backend.h"
#include "test_util.h"
#include "ui/app_state.h"
#include "ui/tone_blend_dialog.h"

using namespace pittore::ui;
using pittore::RGBAf;

namespace {

// 64x64 doc with nothing in it yet; tests compose their own stacks with
// addBg() LAST (panel index 0 is the TOP: backgrounds go at the end).
DocumentItem* makeDoc(AppState& state, const QString& title) {
    DocumentItem* d = state.addDocument(title, QSize(64, 64), 300);
    CHECK(d != nullptr);
    if (!d) return nullptr;
    d->layers.clear();
    return d;
}

void addBg(DocumentItem* d) {
    LayerItem bg;
    bg.name = QStringLiteral("BG");
    bg.kind = LayerItem::Kind::Pixel;
    bg.pixels = std::make_shared<pittore::Image>(64, 64);
    bg.pixels->fill(RGBAf{0.2f, 0.4f, 0.8f, 1});
    d->layers.append(std::move(bg));
    d->activeLayer = 0;
    d->selectedLayers = QVector<int>{0};
    d->rebuildComposite();
}

// Append a tone-blend group header + one red square child (16x16 at
// offset (24,24)), both visible. Returns the header index.
int addToneGroup(DocumentItem* d) {
    LayerItem header;
    header.name = QStringLiteral("Tone Group");
    header.kind = LayerItem::Kind::Group;
    header.indent = 0;
    header.toneBlendGroup = true;
    const int g = d->layers.size();
    d->layers.append(std::move(header));
    LayerItem child;
    child.name = QStringLiteral("Red");
    child.kind = LayerItem::Kind::Pixel;
    child.indent = 1;
    child.pixels = std::make_shared<pittore::Image>(16, 16);
    child.pixels->fill(RGBAf{1, 0, 0, 1});
    child.offset = QPointF(24, 24);
    d->layers.append(std::move(child));
    d->activeLayer = 0;
    d->selectedLayers = QVector<int>{0};
    return g;
}

QColor compAt(DocumentItem* d, int x, int y) {
    return d->composite.pixelColor(x, y);
}

}  // namespace

// Flag on: the red square is pulled toward the blue backdrop (red channel
// drops hard: backdrop carries far less red energy than the square).
// Flag off / strength 0: flat composite, square stays pure red, backdrop
// intact where the group is transparent.
static void test_blend_against_backdrop() {
    AppState state;
    DocumentItem* d = makeDoc(state, QStringLiteral("blend"));
    if (!d) return;
    const int g = addToneGroup(d);
    addBg(d);

    d->rebuildComposite();
    const QColor on = compAt(d, 32, 32);
    CHECK(on.red() < 128);   // red pulled down toward the backdrop
    CHECK(on.alpha() == 255);
    const QColor outside = compAt(d, 4, 4);
    CHECK(outside.blue() > 150);  // transparent group area: backdrop shows

    // Same stack, flag off: pure red square, same backdrop elsewhere.
    d->layers[g].toneBlendGroup = false;
    d->rebuildComposite();
    const QColor off = compAt(d, 32, 32);
    CHECK(off.red() > 240);
    CHECK(off.green() < 16);
    CHECK(compAt(d, 4, 4) == outside);

    // Flag on but strength 0: reproduces the flat composite.
    d->layers[g].toneBlendGroup = true;
    d->layers[g].toneBlend.strength = 0.0f;
    d->rebuildComposite();
    const QColor flat = compAt(d, 32, 32);
    CHECK(std::abs(flat.red() - off.red()) < 4);
    CHECK(std::abs(flat.green() - off.green()) < 4);
    CHECK(std::abs(flat.blue() - off.blue()) < 4);
}

// The flag rides undo snapshots: set (one step) → blended; undo → clean;
// redo → blended again.
static void test_flag_undo() {
    AppState state;
    DocumentItem* d = makeDoc(state, QStringLiteral("blendundo"));
    if (!d) return;
    const int g = addToneGroup(d);
    addBg(d);
    d->layers[g].toneBlendGroup = false;

    state.beginUndoStep();
    d->layers[g].toneBlendGroup = true;
    d->rebuildComposite();
    const QColor blended = compAt(d, 32, 32);
    state.commitUndoStep(QStringLiteral("Tone Blend Group"),
                         QStringLiteral("tone-blend"));
    CHECK(blended.red() < 128);

    state.undo();
    CHECK(!d->layers[g].toneBlendGroup);
    d->rebuildComposite();
    CHECK(compAt(d, 32, 32).red() > 240);

    state.redo();
    CHECK(d->layers[g].toneBlendGroup);
    d->rebuildComposite();
    CHECK(compAt(d, 32, 32).red() < 128);
}

// Nested tone groups: an inner flagged group inside an outer flagged group
// blends both levels without crashing and changes both footprints.
static void test_nested_groups() {
    AppState state;
    DocumentItem* d = makeDoc(state, QStringLiteral("blendnest"));
    if (!d) return;
    // Outer tone group with a red square (left) nesting an inner tone group
    // with a green square (right).
    LayerItem outer;
    outer.name = QStringLiteral("Outer");
    outer.kind = LayerItem::Kind::Group;
    outer.indent = 0;
    outer.toneBlendGroup = true;
    d->layers.append(std::move(outer));
    LayerItem red;
    red.name = QStringLiteral("Red");
    red.kind = LayerItem::Kind::Pixel;
    red.indent = 1;
    red.pixels = std::make_shared<pittore::Image>(8, 8);
    red.pixels->fill(RGBAf{1, 0, 0, 1});
    red.offset = QPointF(8, 28);
    d->layers.append(std::move(red));
    LayerItem inner;
    inner.name = QStringLiteral("Inner");
    inner.kind = LayerItem::Kind::Group;
    inner.indent = 1;
    inner.toneBlendGroup = true;
    d->layers.append(std::move(inner));
    LayerItem green;
    green.name = QStringLiteral("Green");
    green.kind = LayerItem::Kind::Pixel;
    green.indent = 2;
    green.pixels = std::make_shared<pittore::Image>(8, 8);
    green.pixels->fill(RGBAf{0, 1, 0, 1});
    green.offset = QPointF(48, 28);
    d->layers.append(std::move(green));
    addBg(d);
    d->rebuildComposite();
    const QColor r = compAt(d, 12, 32);
    const QColor gr = compAt(d, 52, 32);
    // Both squares moved off their flat colours (finite, blended).
    CHECK(r.red() < 255 || r.blue() > 0);
    CHECK(gr.green() < 255 || gr.blue() > 0);
    CHECK(r.alpha() == 255);
    CHECK(gr.alpha() == 255);
}

// Creation through the real entry point: two squares selected → flagged
// group with default params, image content type, two undo steps, and a
// blended composite. A lone layer refuses with a hint.
static void test_make_tone_blend_group() {
    AppState state;
    DocumentItem* d = makeDoc(state, QStringLiteral("make"));
    if (!d) return;
    LayerItem red;
    red.name = QStringLiteral("Red");
    red.kind = LayerItem::Kind::Pixel;
    red.pixels = std::make_shared<pittore::Image>(16, 16);
    red.pixels->fill(RGBAf{1, 0, 0, 1});
    red.offset = QPointF(8, 8);
    d->layers.append(std::move(red));
    LayerItem green;
    green.name = QStringLiteral("Green");
    green.kind = LayerItem::Kind::Pixel;
    green.pixels = std::make_shared<pittore::Image>(16, 16);
    green.pixels->fill(RGBAf{0, 1, 0, 1});
    green.offset = QPointF(40, 40);
    d->layers.append(std::move(green));
    addBg(d);
    d->selectedLayers = QVector<int>{0, 1};
    d->activeLayer = 0;

    const int depth = state.undoDepth();
    const int g = state.makeToneBlendGroup();
    CHECK(g == 0);
    CHECK(d->layers[g].kind == LayerItem::Kind::Group);
    CHECK(d->layers[g].toneBlendGroup);
    CHECK(d->layers[g].name == QStringLiteral("Tone Blend Group"));
    CHECK_EQ(d->layers[g].toneBlend.contentType, 0);
    CHECK_EQ(state.undoDepth(), depth + 2);  // Group Layers + Tone Blend Group
    CHECK(state.undoStepName() == QStringLiteral("Tone Blend Group"));
    d->rebuildComposite();
    CHECK(compAt(d, 16, 16).red() < 200);  // blended, not flat red

    state.undo();  // flag step reverts; plain group remains
    CHECK(!d->layers[g].toneBlendGroup);
    state.undo();  // group step reverts; squares back on top level
    CHECK_EQ(d->layers.size(), 3);

    // Lone layer: wrapped on its own — the image stays visible, re-graded
    // against the composite beneath it.
    d->selectedLayers = QVector<int>{0};
    d->activeLayer = 0;
    const int depth2 = state.undoDepth();
    const int g2 = state.makeToneBlendGroup();
    CHECK(g2 == 0);
    CHECK(d->layers[g2].toneBlendGroup);
    CHECK_EQ(d->layers.size(), 4);  // header + red + green + BG
    CHECK(d->layers[1].name == QStringLiteral("Red"));
    CHECK_EQ(state.undoDepth(), depth2 + 2);
    d->rebuildComposite();
    const QColor single = compAt(d, 16, 16);
    CHECK(single.alpha() == 255);  // the image still renders through the group
    // Backdrop here is steel blue (51,102,204); the red square re-graded
    // against it must keep its red and kill green/blue — an empty group
    // would show the backdrop's 102/204 instead.
    CHECK(single.red() > 40);
    CHECK(single.green() < 10);
    CHECK(single.blue() < 10);
    state.undo();
    CHECK(!d->layers[g2].toneBlendGroup);
    state.undo();
    CHECK_EQ(d->layers.size(), 3);
}

// Content type follows the first child: shape → vector, text → text.
static void test_content_type_autodetect() {
    {
        AppState state;
        DocumentItem* d = makeDoc(state, QStringLiteral("ctshape"));
        if (!d) return;
        for (int i = 0; i < 2; ++i) {
            LayerItem s;
            s.name = QStringLiteral("Shape %1").arg(i);
            s.kind = LayerItem::Kind::Shape;
            d->layers.append(std::move(s));
        }
        addBg(d);
        d->selectedLayers = QVector<int>{0, 1};
        const int g = state.makeToneBlendGroup();
        CHECK(g >= 0);
        CHECK_EQ(d->layers[g].toneBlend.contentType, 1);
    }
    {
        AppState state;
        DocumentItem* d = makeDoc(state, QStringLiteral("cttext"));
        if (!d) return;
        for (int i = 0; i < 2; ++i) {
            LayerItem t;
            t.name = QStringLiteral("Text");
            t.kind = LayerItem::Kind::Text;
            t.isText = true;
            d->layers.append(std::move(t));
        }
        addBg(d);
        d->selectedLayers = QVector<int>{0, 1};
        const int g = state.makeToneBlendGroup();
        CHECK(g >= 0);
        CHECK_EQ(d->layers[g].toneBlend.contentType, 2);
    }
}

// Param edits re-render live and undo cleanly (what the dialog drives).
static void test_param_edit_undo() {
    AppState state;
    DocumentItem* d = makeDoc(state, QStringLiteral("params"));
    if (!d) return;
    const int g = addToneGroup(d);
    addBg(d);

    state.beginUndoStep();
    d->layers[g].toneBlend.strength = 0.0f;
    d->rebuildComposite();
    const QColor flat = compAt(d, 32, 32);
    CHECK(flat.red() > 240);  // flat red square, blend off
    d->layers[g].toneBlend.strength = 1.0f;
    d->layers[g].toneBlend.contrast = 1.0f;
    d->rebuildComposite();
    const QColor punchy = compAt(d, 32, 32);
    CHECK(punchy.red() != flat.red() || punchy.blue() != flat.blue());
    state.commitUndoStep(QStringLiteral("Tone Blend"),
                         QStringLiteral("tone-blend"));
    state.undo();
    CHECK_EQ(d->layers[g].toneBlend.strength, 1.0f);
    CHECK_EQ(d->layers[g].toneBlend.contrast, 0.0f);
    state.redo();
    CHECK_EQ(d->layers[g].toneBlend.contrast, 1.0f);
}

// IFP round trip: flag + all five params + pixels survive save/open.
static void test_ifp_round_trip() {
    AppState state;
    DocumentItem* d = makeDoc(state, QStringLiteral("persist"));
    if (!d) return;
    const int g = addToneGroup(d);
    addBg(d);
    d->layers[g].toneBlend.strength = 0.75f;
    d->layers[g].toneBlend.color = 0.5f;
    d->layers[g].toneBlend.contrast = -0.25f;
    d->layers[g].toneBlend.lowPass = 0.8f;
    d->layers[g].toneBlend.contentType = 2;
    d->rebuildComposite();
    const QColor before = compAt(d, 32, 32);

    QTemporaryDir tmp;
    CHECK(tmp.isValid());
    const QString path = tmp.filePath(QStringLiteral("tone.ifp"));
    QString error;
    CHECK(state.saveProject(path, &error));
    CHECK(state.openProject(path, &error));
    DocumentItem* d2 = state.activeDocument();
    CHECK(d2 != nullptr);
    if (!d2) return;
    int g2 = -1;
    for (int i = 0; i < d2->layers.size(); ++i)
        if (d2->layers[i].kind == LayerItem::Kind::Group &&
            d2->layers[i].toneBlendGroup)
            g2 = i;
    CHECK(g2 >= 0);
    CHECK_NEAR(d2->layers[g2].toneBlend.strength, 0.75f, 1e-6f);
    CHECK_NEAR(d2->layers[g2].toneBlend.color, 0.5f, 1e-6f);
    CHECK_NEAR(d2->layers[g2].toneBlend.contrast, -0.25f, 1e-6f);
    CHECK_NEAR(d2->layers[g2].toneBlend.lowPass, 0.8f, 1e-6f);
    CHECK_EQ(d2->layers[g2].toneBlend.contentType, 2);
    const QColor after = compAt(d2, 32, 32);
    CHECK(std::abs(after.red() - before.red()) < 4);
    CHECK(std::abs(after.green() - before.green()) < 4);
    CHECK(std::abs(after.blue() - before.blue()) < 4);
}

// The real dialog, offscreen: scrubbing Strength previews live, Cancel
// reverts, OK lands exactly one undo step.
static void test_dialog_accept_reject(QApplication& app) {
    AppState state;
    DocumentItem* d = makeDoc(state, QStringLiteral("dlg"));
    if (!d) return;
    const int g = addToneGroup(d);
    addBg(d);
    d->rebuildComposite();
    const QColor blended = compAt(d, 32, 32);
    CHECK(blended.red() < 128);

    {
        ToneBlendDialog dlg(&state, g);
        auto sliders = dlg.findChildren<QSlider*>();
        CHECK(!sliders.isEmpty());
        // Strength is the first slider: drag it to 0, preview coalesces
        // through the event loop, then Cancel must restore the blend.
        sliders.first()->setValue(0);
        app.processEvents();
        CHECK(compAt(d, 32, 32).red() > 240);
        static_cast<QDialog*>(&dlg)->reject();
        CHECK_EQ(d->layers[g].toneBlend.strength, 1.0f);
        CHECK(compAt(d, 32, 32).red() < 128);
    }
    {
        const int before = state.undoDepth();
        ToneBlendDialog dlg(&state, g);
        auto sliders = dlg.findChildren<QSlider*>();
        CHECK(!sliders.isEmpty());
        sliders.first()->setValue(50);
        app.processEvents();
        CHECK_NEAR(d->layers[g].toneBlend.strength, 0.5f, 1e-6f);
        static_cast<QDialog*>(&dlg)->accept();
        CHECK_EQ(state.undoDepth(), before + 1);
        CHECK(state.undoStepName() == QStringLiteral("Tone Blend Group"));
        state.undo();
        CHECK_EQ(d->layers[g].toneBlend.strength, 1.0f);
        state.redo();
        CHECK_NEAR(d->layers[g].toneBlend.strength, 0.5f, 1e-6f);
    }
}

// 256x256 doc: blue backdrop + tone group holding a 16x16 red square at
// (100,100). Big enough that a dab-sized dirty rect + 32px halo stays far
// below the 75% full-rebuild fallback, so recompositeRegion exercises the
// CPU region-tone path instead of falling back.
DocumentItem* makeRegionDoc(AppState& state, const QString& title,
                            int* childOut = nullptr, bool pinCpu = true) {
    DocumentItem* d = state.addDocument(title, QSize(256, 256), 300);
    CHECK(d != nullptr);
    if (!d) return nullptr;
    d->layers.clear();
    LayerItem header;
    header.name = QStringLiteral("Tone Group");
    header.kind = LayerItem::Kind::Group;
    header.indent = 0;
    header.toneBlendGroup = true;
    d->layers.append(std::move(header));
    LayerItem child;
    child.name = QStringLiteral("Red");
    child.kind = LayerItem::Kind::Pixel;
    child.indent = 1;
    child.pixels = std::make_shared<pittore::Image>(16, 16);
    child.pixels->fill(RGBAf{1, 0, 0, 1});
    child.offset = QPointF(100, 100);
    d->layers.append(std::move(child));
    if (childOut) *childOut = 1;
    LayerItem bg;
    bg.name = QStringLiteral("BG");
    bg.kind = LayerItem::Kind::Pixel;
    bg.pixels = std::make_shared<pittore::Image>(256, 256);
    bg.pixels->fill(RGBAf{0.2f, 0.4f, 0.8f, 1});
    d->layers.append(std::move(bg));
    d->activeLayer = 0;
    d->selectedLayers = QVector<int>{0};
    // Pin the CPU backend (unless the caller wants the live one): the
    // region-tone tests below validate the host path (recomputeRegion ->
    // renderRegionTone CPU branch). setBackend rebuilds on the CPU, which
    // also warms its level/pivot caches.
    if (pinCpu) d->setBackend(nullptr);
    return d;
}

// Region re-grade of unchanged content must reproduce the full rebuild
// bit-for-bit inside the dirty rect (warm cache: same level texels, same
// upsample formula, same pivot) and preserve every pixel outside it.
static void test_region_tone_matches_rebuild() {
    AppState state;
    DocumentItem* d = makeRegionDoc(state, QStringLiteral("regiontone"));
    if (!d) return;
    const QImage full = d->composite.copy();
    d->recompositeRegion(QRect(100, 100, 16, 16));
    CHECK(d->composite.size() == full.size());
    CHECK(d->composite.format() == full.format());
    CHECK(std::memcmp(d->composite.constBits(), full.constBits(),
                      std::size_t(full.sizeInBytes())) == 0);
}

// Erase-like dab inside the group: re-grade the dirty rect, then compare
// against a full rebuild — close inside (stale-field approximation, one dab
// moves the blurred field negligibly), bit-exact outside (the blit covers
// the dirty rect only).
static void test_region_tone_dab_close_to_rebuild() {
    AppState state;
    int child = -1;
    DocumentItem* d = makeRegionDoc(state, QStringLiteral("regiondab"), &child);
    if (!d || child < 0) return;
    const QImage before = d->composite.copy();
    // Dab: punch a 4x4 green block into the red square.
    pittore::Image* img = d->layers[child].pixels.get();
    CHECK(img != nullptr);
    if (!img) return;
    for (int y = 4; y < 8; ++y)
        for (int x = 4; x < 8; ++x)
            img->data()[std::size_t(y) * 16 + x] = RGBAf{0, 1, 0, 1};
    const QRect dirty(104, 104, 4, 4);
    d->recompositeRegion(dirty);
    const QImage regional = d->composite.copy();
    d->rebuildComposite();
    const QImage& full = d->composite;
    // Outside the dirty rect: untouched (exact vs pre-dab canvas).
    for (int y = 0; y < 256; ++y)
        for (int x = 0; x < 256; ++x) {
            if (dirty.contains(x, y)) continue;
            CHECK(regional.pixelColor(x, y) == before.pixelColor(x, y));
        }
    // Inside: close to the full rebuild (8-bit channels; a seam or stale
    // math bug would read dozens of levels off, not a handful).
    int maxDiff = 0;
    for (int y = dirty.top(); y <= dirty.bottom(); ++y)
        for (int x = dirty.left(); x <= dirty.right(); ++x) {
            const QColor a = regional.pixelColor(x, y);
            const QColor b = full.pixelColor(x, y);
            maxDiff = std::max(maxDiff, std::abs(a.red() - b.red()));
            maxDiff = std::max(maxDiff, std::abs(a.green() - b.green()));
            maxDiff = std::max(maxDiff, std::abs(a.blue() - b.blue()));
            maxDiff = std::max(maxDiff, std::abs(a.alpha() - b.alpha()));
        }
    CHECK_NEAR(maxDiff, 0, 12);
}

// Device region path (default backend): same dab scenario as above. The
// device pyramid is staged per region so its cells sit on a shifted grid —
// same blur scale as full frame since the level-select fix, but not the same
// texels — so this pins closeness, not bit-identity. Skipped (not failed)
// where no GPU backend exists.
static void test_region_tone_gpu_close_to_rebuild() {
    AppState state;
    int child = -1;
    DocumentItem* d =
        makeRegionDoc(state, QStringLiteral("regiongpu"), &child, false);
    if (!d || child < 0) return;
    if (!d->backend ||
        d->backend->type() == pittore::compute::BackendType::CPU)
        return;
    const QImage before = d->composite.copy();
    pittore::Image* img = d->layers[child].pixels.get();
    CHECK(img != nullptr);
    if (!img) return;
    for (int y = 4; y < 8; ++y)
        for (int x = 4; x < 8; ++x)
            img->data()[std::size_t(y) * 16 + x] = RGBAf{0, 1, 0, 1};
    const QRect dirty(104, 104, 4, 4);
    d->recompositeRegion(dirty);
    const QImage regional = d->composite.copy();
    d->rebuildComposite();
    const QImage& full = d->composite;
    for (int y = 0; y < 256; ++y)
        for (int x = 0; x < 256; ++x) {
            if (dirty.contains(x, y)) continue;
            CHECK(regional.pixelColor(x, y) == before.pixelColor(x, y));
        }
    int maxDiff = 0;
    for (int y = dirty.top(); y <= dirty.bottom(); ++y)
        for (int x = dirty.left(); x <= dirty.right(); ++x) {
            const QColor a = regional.pixelColor(x, y);
            const QColor b = full.pixelColor(x, y);
            maxDiff = std::max(maxDiff, std::abs(a.red() - b.red()));
            maxDiff = std::max(maxDiff, std::abs(a.green() - b.green()));
            maxDiff = std::max(maxDiff, std::abs(a.blue() - b.blue()));
            maxDiff = std::max(maxDiff, std::abs(a.alpha() - b.alpha()));
        }
    CHECK_NEAR(maxDiff, 0, 12);
}

int main(int argc, char** argv) {
    QApplication app(argc, argv);
    const QString logDir =
        QDir::tempPath() + QStringLiteral("/pittore-tone-group-log");
    QDir().mkpath(logDir);
    ::pittore::core::log::set_log_dir(logDir.toStdString().c_str());
    test_blend_against_backdrop();
    test_flag_undo();
    test_nested_groups();
    test_make_tone_blend_group();
    test_content_type_autodetect();
    test_param_edit_undo();
    test_ifp_round_trip();
    test_dialog_accept_reject(app);
    test_region_tone_matches_rebuild();
    test_region_tone_dab_close_to_rebuild();
    test_region_tone_gpu_close_to_rebuild();
    const int rc = pittore_test::failures() == 0 ? 0 : 1;
    std::printf("  checks=%d failures=%d\n", pittore_test::checks(),
                pittore_test::failures());
    return rc;
}
