// Replace Color brush: tolerance, limits, recolor modes, sampler, undo.
// Headless AppState test, CPU backend.
#include <QCoreApplication>
#include <QDir>

#include <cmath>
#include <cstddef>

#include "engine/compute/paint.h"
#include "engine/core/log.h"
#include "test_util.h"
#include "ui/app_state.h"

using namespace pittore::ui;
using pittore::RGBAf;

namespace {

// 16x16 pixel layer on a fresh doc; layer 0 is the test layer.
DocumentItem* makeDoc(AppState& state, const QString& title) {
    DocumentItem* d = state.addDocument(title, QSize(64, 64), 300);
    CHECK(d != nullptr);
    if (!d) return nullptr;
    d->layers.clear();
    LayerItem l;
    l.name = QStringLiteral("T");
    l.kind = LayerItem::Kind::Pixel;
    l.pixels = std::make_shared<pittore::Image>(16, 16);
    l.pixels->fill(RGBAf{0, 0, 0, 0});
    l.offset = QPointF(0, 0);
    l.scaleX = l.scaleY = 1.0;
    d->layers.append(std::move(l));
    d->activeLayer = 0;
    d->selectedLayers = QVector<int>{0};
    d->rebuildComposite();
    return d;
}

void fillRect(DocumentItem& d, int x0, int y0, int x1, int y1, RGBAf c) {
    auto& img = *d.layers[0].pixels;
    for (int y = y0; y <= y1; ++y)
        for (int x = x0; x <= x1; ++x) img.at(x, y) = c;
}

float luma(const RGBAf& c) { return 0.30f * c.r + 0.59f * c.g + 0.11f * c.b; }

// Single-target shorthand.
std::vector<RGBAf> tgt(float r, float g, float b) {
    return {RGBAf{r, g, b, 1.0f}};
}

}  // namespace

// Color mode keeps luma, takes foreground hue: red -> blue of same lightness.
static void test_color_mode_keeps_luminance() {
    AppState state;
    DocumentItem* d = makeDoc(state, QStringLiteral("colormode"));
    if (!d) return;
    fillRect(*d, 0, 0, 15, 15, RGBAf{1, 0, 0, 1});
    state.setForeground(QColor(0, 0, 255));

    const bool ok = state.replaceColorDab(
        QPointF(8, 8), 20.0, 1.0, /*mode=*/2, tgt(1, 0, 0),
        /*tolerance=*/0.5, /*limits=*/0, /*antialias=*/false,
        /*harmony=*/0.0);
    CHECK(ok);
    const RGBAf c = d->layers[0].pixels->at(8, 8);
    CHECK_EQ(c.a, 1.0f);
    CHECK_NEAR(luma(c), 0.30f, 0.02f);  // red luma survives
    CHECK(c.b > 0.9f);                    // re-hued blue
    CHECK(c.r < 0.3f);
}

// No match changes nothing, reports false.
static void test_no_match_no_op() {
    AppState state;
    DocumentItem* d = makeDoc(state, QStringLiteral("nomatch"));
    if (!d) return;
    fillRect(*d, 0, 0, 15, 15, RGBAf{0, 1, 0, 1});  // green layer...
    state.setForeground(QColor(0, 0, 255));

    const int depth = state.undoDepth();
    const bool changed = state.replaceColorDab(
        QPointF(8, 8), 20.0, 1.0, 2, tgt(1, 0, 0), 0.05, 0, false, 0.0);
    CHECK(!changed);
    CHECK_EQ(state.undoDepth(), depth);
    CHECK_EQ(d->layers[0].pixels->at(8, 8).g, 1.0f);
}

// Tolerance is a per-channel ceiling: just outside kept, inside recolored.
static void test_tolerance_boundary() {
    AppState state;
    DocumentItem* d = makeDoc(state, QStringLiteral("tol"));
    if (!d) return;
    fillRect(*d, 0, 0, 7, 15, RGBAf{1, 0, 0, 1});          // exact red
    fillRect(*d, 8, 0, 15, 15, RGBAf{0.85f, 0, 0, 1});     // 0.15 away
    state.setForeground(QColor(0, 0, 255));

    CHECK(state.replaceColorDab(QPointF(8, 8), 20.0, 1.0, 2, tgt(1, 0, 0),
                                0.10, 0, false, 0.0));
    CHECK(d->layers[0].pixels->at(4, 8).b > 0.9f);
    CHECK(d->layers[0].pixels->at(4, 8).r < 0.3f);
    CHECK_EQ(d->layers[0].pixels->at(12, 8).r, 0.85f);  // outside tol: kept
    CHECK_EQ(d->layers[0].pixels->at(12, 8).b, 0.0f);
}

// Contiguous floods the connected region; Discontiguous hits all matches.
static void test_contiguous_vs_discontiguous() {
    for (int limits = 0; limits <= 1; ++limits) {
        AppState state;
        DocumentItem* d = makeDoc(
            state, QStringLiteral("limits%1").arg(limits));
        if (!d) return;
        fillRect(*d, 0, 0, 15, 15, RGBAf{0, 1, 0, 1});  // green field
        fillRect(*d, 2, 2, 5, 13, RGBAf{1, 0, 0, 1});   // left red bar
        fillRect(*d, 10, 2, 13, 13, RGBAf{1, 0, 0, 1});  // right red bar
        state.setForeground(QColor(0, 0, 255));

        // Wide brush centred left, reaching right.
        CHECK(state.replaceColorDab(QPointF(3.5, 8), 9.0, 1.0, 2,
                                    tgt(1, 0, 0), 0.3, limits, false, 0.0));
        CHECK(d->layers[0].pixels->at(3, 8).b > 0.9f);
        if (limits == 0) {
            CHECK(d->layers[0].pixels->at(11, 8).b >
                  0.9f);  // discontiguous jumps gap
        } else {
            CHECK_EQ(d->layers[0].pixels->at(11, 8).r,
                     1.0f);  // contiguous blocked
            CHECK_EQ(d->layers[0].pixels->at(11, 8).b, 0.0f);
        }
        CHECK_EQ(d->layers[0].pixels->at(7, 8).g, 1.0f);  // gap never matched
    }
}

// Find Edges stops at a luma step; plain Contiguous crosses it.
static void test_find_edges() {
    for (int limits = 1; limits <= 2; ++limits) {
        AppState state;
        DocumentItem* d =
            makeDoc(state, QStringLiteral("edges%1").arg(limits));
        if (!d) return;
        fillRect(*d, 0, 0, 15, 15, RGBAf{1, 0, 0, 1});       // red field
        fillRect(*d, 8, 0, 15, 15, RGBAf{1, 0.5f, 0, 1});    // orange half
        state.setForeground(QColor(0, 0, 255));
        // In tolerance but over the luma edge trip point.
        CHECK(state.replaceColorDab(QPointF(4, 8), 12.0, 1.0, 2,
                                    tgt(1, 0, 0), 0.6, limits, false, 0.0));
        CHECK(d->layers[0].pixels->at(4, 8).b > 0.9f);
        if (limits == 1) {
            CHECK(d->layers[0].pixels->at(12, 8).b > 0.5f);  // crossed
        } else {
            CHECK_EQ(d->layers[0].pixels->at(12, 8).g, 0.5f);  // held edge
            CHECK_EQ(d->layers[0].pixels->at(12, 8).b, 0.0f);
        }
    }
}

// Other modes: Hue keeps sat/luma, grey Saturation drains, white Luminosity blows out.
static void test_hue_saturation_luminosity() {
    {
        AppState state;
        DocumentItem* d = makeDoc(state, QStringLiteral("hue"));
        if (!d) return;
        fillRect(*d, 0, 0, 15, 15, RGBAf{1, 0, 0, 1});
        state.setForeground(QColor(0, 0, 255));
        CHECK(state.replaceColorDab(QPointF(8, 8), 20.0, 1.0, 0,
                                    tgt(1, 0, 0), 0.5, 0, false, 0.0));
        const RGBAf c = d->layers[0].pixels->at(8, 8);
        CHECK_NEAR(luma(c), 0.30f, 0.02f);
        CHECK(c.b > c.r);  // hue rotated toward blue
    }
    {
        AppState state;
        DocumentItem* d = makeDoc(state, QStringLiteral("sat"));
        if (!d) return;
        fillRect(*d, 0, 0, 15, 15, RGBAf{1, 0, 0, 1});
        state.setForeground(QColor(128, 128, 128));  // zero saturation
        CHECK(state.replaceColorDab(QPointF(8, 8), 20.0, 1.0, 1,
                                    tgt(1, 0, 0), 0.5, 0, false, 0.0));
        const RGBAf c = d->layers[0].pixels->at(8, 8);
        CHECK_NEAR(c.r, 0.30f, 0.03f);
        CHECK_NEAR(c.g, 0.30f, 0.03f);
        CHECK_NEAR(c.b, 0.30f, 0.03f);
    }
    {
        AppState state;
        DocumentItem* d = makeDoc(state, QStringLiteral("lum"));
        if (!d) return;
        fillRect(*d, 0, 0, 15, 15, RGBAf{1, 0, 0, 1});
        state.setForeground(QColor(255, 255, 255));
        CHECK(state.replaceColorDab(QPointF(8, 8), 20.0, 1.0, 3,
                                    tgt(1, 0, 0), 0.5, 0, false, 0.0));
        const RGBAf c = d->layers[0].pixels->at(8, 8);
        CHECK(c.r > 0.95f && c.g > 0.95f && c.b > 0.95f);
    }
}

// Anti-alias off: hard stop at tolerance edge.
static void test_antialias_flag_roundtrip() {
    AppState state;
    DocumentItem* d = makeDoc(state, QStringLiteral("aa"));
    if (!d) return;
    fillRect(*d, 0, 0, 15, 15, RGBAf{1, 0, 0, 1});
    state.setForeground(QColor(0, 0, 255));
    // Hard brush, exact target, zero tol: on/off must agree.
    CHECK(state.replaceColorDab(QPointF(8, 8), 4.0, 1.0, 2, tgt(1, 0, 0),
                                0.0, 0, false, 0.0));
    CHECK(d->layers[0].pixels->at(8, 8).b > 0.9f);
}

// Sampler reads the active layer texel, refuses outside.
static void test_sampler() {
    AppState state;
    DocumentItem* d = makeDoc(state, QStringLiteral("sample"));
    if (!d) return;
    fillRect(*d, 0, 0, 15, 15, RGBAf{0, 1, 0, 1});
    bool ok = false;
    const RGBAf c = state.sampleActiveLayerAt(QPointF(3, 9), &ok);
    CHECK(ok);
    CHECK_EQ(c.g, 1.0f);
    CHECK_EQ(c.r, 0.0f);
    const RGBAf o = state.sampleActiveLayerAt(QPointF(99, 99), &ok);
    CHECK(!ok);
    CHECK_EQ(o.a, 0.0f);
}

// One stroke = one undo step.
static void test_undo_single_step() {
    AppState state;
    DocumentItem* d = makeDoc(state, QStringLiteral("undo"));
    if (!d) return;
    fillRect(*d, 0, 0, 15, 15, RGBAf{1, 0, 0, 1});
    state.setForeground(QColor(0, 0, 255));

    const int depth = state.undoDepth();
    state.beginUndoStep();
    state.copyOnWriteActiveLayer();
    const bool changed = state.replaceColorDab(QPointF(8, 8), 20.0, 1.0, 2,
                                               tgt(1, 0, 0), 0.5, 0,
                                               false, 0.0);
    CHECK(changed);
    state.commitUndoStep(QStringLiteral("Replace Color"),
                         QStringLiteral("replace"));
    CHECK_EQ(state.undoDepth(), depth + 1);
    CHECK(d->layers[0].pixels->at(8, 8).b > 0.9f);
    state.undo();
    CHECK_EQ(d->layers[0].pixels->at(8, 8).r, 1.0f);
    CHECK_EQ(d->layers[0].pixels->at(8, 8).b, 0.0f);
    state.redo();
    CHECK(d->layers[0].pixels->at(8, 8).b > 0.9f);
}

// Half-alpha texel recolors with alpha kept; fully clear stays untouched.
static void test_transparency_untouched() {
    AppState state;
    DocumentItem* d = makeDoc(state, QStringLiteral("alpha"));
    if (!d) return;
    fillRect(*d, 0, 0, 15, 15, RGBAf{1, 0, 0, 0});  // transparent "red"
    d->layers[0].pixels->at(8, 8) = RGBAf{1, 0, 0, 0.5f};
    state.setForeground(QColor(0, 0, 255));
    CHECK(state.replaceColorDab(QPointF(8, 8), 20.0, 1.0, 2,
                                tgt(1, 0, 0), 0.5, 0, false, 0.0));
    CHECK_EQ(d->layers[0].pixels->at(8, 8).a, 0.5f);  // alpha kept
    CHECK(d->layers[0].pixels->at(8, 8).b > 0.4f);    // recolored
    CHECK_EQ(d->layers[0].pixels->at(0, 0).a, 0.0f);  // clear ignored
    CHECK_EQ(d->layers[0].pixels->at(0, 0).r, 1.0f);
}

// Multi-target spans a gradient: centre+ring matches the whole span.
static void test_multitarget_gradient() {
    AppState state;
    DocumentItem* d = makeDoc(state, QStringLiteral("multi"));
    if (!d) return;
    for (int x = 0; x < 16; ++x) {
        const float t = x / 15.0f;  // red -> orange across the layer
        fillRect(*d, x, 0, x, 15, RGBAf{1.0f, 0.5f * t, 0, 1});
    }
    state.setForeground(QColor(0, 0, 255));

    const auto targets = state.replaceTargetsAt(QPointF(8, 8), 10.0, 0, 0.15);
    CHECK(targets.size() > 1);
    CHECK(state.replaceColorDab(QPointF(8, 8), 10.0, 1.0, 2, targets, 0.15, 0,
                                false, 0.0));
    CHECK(d->layers[0].pixels->at(1, 8).b > 0.5f);
    CHECK(d->layers[0].pixels->at(14, 8).b > 0.5f);
}

// Sample size: point reads one texel, 3x3 returns the mean.
static void test_sample_size_averaging() {
    AppState state;
    DocumentItem* d = makeDoc(state, QStringLiteral("samplesize"));
    if (!d) return;
    for (int y = 0; y < 16; ++y)
        for (int x = 0; x < 16; ++x)
            d->layers[0].pixels->at(x, y) =
                ((x + y) & 1) ? RGBAf{1, 0, 0, 1} : RGBAf{0.6f, 0, 0, 1};

    const auto point = state.replaceTargetsAt(QPointF(8, 8), 10.0, 0, 0.5);
    CHECK(!point.empty());
    // (8+8) is even on this checker.
    CHECK_EQ(point.front().r, 0.6f);
    const auto avg = state.replaceTargetsAt(QPointF(8, 8), 10.0, 1, 0.5);
    CHECK(!avg.empty());
    // 3x3 over checker mixes both colours (~0.8 mean).
    CHECK_NEAR(avg.front().r, 0.8f, 0.05f);
    CHECK_EQ(avg.front().g, 0.0f);
}

// Harmony fixed point: on uniform field harmony 1 == harmony 0.
static void test_harmony_uniform_fixedpoint() {
    RGBAf plain{0, 0, 0, 1}, adapted{0, 0, 0, 1};
    for (double harmony : {0.0, 1.0}) {
        AppState state;
        DocumentItem* d = makeDoc(
            state, QStringLiteral("harm%1").arg(harmony));
        if (!d) return;
        fillRect(*d, 0, 0, 15, 15, RGBAf{1, 0, 0, 1});
        state.setForeground(QColor(0, 0, 255));
        CHECK(state.replaceColorDab(QPointF(8, 8), 20.0, 1.0, 2, tgt(1, 0, 0),
                                    0.5, 0, false, harmony));
        RGBAf& slot = harmony == 0.0 ? plain : adapted;
        slot = d->layers[0].pixels->at(8, 8);
    }
    CHECK_NEAR(plain.r, adapted.r, 1e-5f);
    CHECK_NEAR(plain.g, adapted.g, 1e-5f);
    CHECK_NEAR(plain.b, adapted.b, 1e-5f);
}

// On texture, harmony keeps local deviation better than plain transfer.
static void test_harmony_preserves_texture() {
    float diffPlain = 0, diffAdapted = 0;
    for (double harmony : {0.0, 1.0}) {
        AppState state;
        DocumentItem* d = makeDoc(
            state, QStringLiteral("tex%1").arg(harmony));
        if (!d) return;
        fillRect(*d, 0, 0, 15, 15, RGBAf{1, 0, 0, 1});
        fillRect(*d, 6, 0, 9, 15, RGBAf{0.8f, 0, 0, 1});  // darker stripe
        state.setForeground(QColor(0, 0, 255));
        CHECK(state.replaceColorDab(QPointF(8, 8), 20.0, 1.0, 2, tgt(1, 0, 0),
                                    0.5, 0, false, harmony));
        const float stripe =
            d->layers[0].pixels->at(7, 8).r;
        const float field =
            d->layers[0].pixels->at(2, 8).r;
        float& slot = harmony == 0.0 ? diffPlain : diffAdapted;
        slot = field - stripe;  // >0 means stripe stayed darker
    }
    CHECK(diffPlain > 0.0f);
    CHECK(diffAdapted > 0.0f);
    CHECK(diffAdapted > diffPlain);
}

// Empty targets paint nothing, report false.
static void test_empty_targets_no_op() {
    AppState state;
    DocumentItem* d = makeDoc(state, QStringLiteral("emptyd"));
    if (!d) return;
    fillRect(*d, 0, 0, 15, 15, RGBAf{1, 0, 0, 1});
    state.setForeground(QColor(0, 0, 255));
    CHECK(!state.replaceColorDab(QPointF(8, 8), 20.0, 1.0, 2,
                                 std::vector<RGBAf>{}, 0.5, 0, false, 0.0));
    CHECK_EQ(d->layers[0].pixels->at(8, 8).r, 1.0f);
    // Sampling clear pixels also yields nothing.
    fillRect(*d, 0, 0, 15, 15, RGBAf{1, 0, 0, 0});
    CHECK(state.replaceTargetsAt(QPointF(8, 8), 10.0, 0, 0.5).empty());
}

// Soft tolerance: pixel at ~85% of tol is partly recolored.
static void test_tolerance_falloff() {
    AppState state;
    DocumentItem* d = makeDoc(state, QStringLiteral("falloff"));
    if (!d) return;
    fillRect(*d, 0, 0, 15, 15, RGBAf{1, 0, 0, 1});
    d->layers[0].pixels->at(12, 8) = RGBAf{0.9f, 0, 0, 1};  // dist 0.10
    state.setForeground(QColor(0, 0, 255));
    // tol 0.12 fades 0.09..0.12, so 0.10 lands mid-fade.
    CHECK(state.replaceColorDab(QPointF(8, 8), 20.0, 1.0, 2, tgt(1, 0, 0),
                                0.12, 0, false, 0.0));
    const float fullB = d->layers[0].pixels->at(4, 8).b;  // exact match
    const float partB = d->layers[0].pixels->at(12, 8).b;  // mid-fade
    CHECK(fullB > 0.9f);
    CHECK(partB > 0.0f);
    CHECK(partB < fullB);
}

// Palette: gradient area yields many colours, capped at 48.
static void test_palette_spans_area() {
    AppState state;
    DocumentItem* d = makeDoc(state, QStringLiteral("pal"));
    if (!d) return;
    for (int x = 0; x < 16; ++x) {
        const float t = x / 15.0f;
        fillRect(*d, x, 0, x, 15, RGBAf{1.0f, 0.5f * t, 0, 1});
    }
    const auto pal =
        state.replacePaletteAt(QPointF(8, 8), 10.0, 0, 0.08, 48);
    CHECK(pal.size() > 4);
    CHECK(pal.size() <= 48);
    // Covers both ends.
    bool hasRed = false, hasOrange = false;
    for (const RGBAf& c : pal) {
        if (c.g < 0.1f) hasRed = true;
        if (c.g > 0.35f) hasOrange = true;
    }
    CHECK(hasRed);
    CHECK(hasOrange);
    // Uniform collapses to one; transparent to none.
    fillRect(*d, 0, 0, 15, 15, RGBAf{0, 1, 0, 1});
    CHECK_EQ(state.replacePaletteAt(QPointF(8, 8), 10.0, 0, 0.15, 48).size(),
             1u);
    // Transparent gives none.
    fillRect(*d, 0, 0, 15, 15, RGBAf{0, 1, 0, 0});
    CHECK(state.replacePaletteAt(QPointF(8, 8), 10.0, 0, 0.15, 48).empty());
}

// Cap binds on a varied field: small cap truncates, 48-cap holds.
static void test_palette_cap() {
    AppState state;
    DocumentItem* d = makeDoc(state, QStringLiteral("palcap"));
    if (!d) return;
    for (int y = 0; y < 16; ++y)
        for (int x = 0; x < 16; ++x)
            d->layers[0].pixels->at(x, y) =
                RGBAf{x / 15.0f, y / 15.0f, 0, 1};
    CHECK_EQ(state.replacePaletteAt(QPointF(8, 8), 10.0, 0, 0.05, 10).size(),
             10u);
    const auto pal =
        state.replacePaletteAt(QPointF(8, 8), 10.0, 0, 0.05, 48);
    CHECK(pal.size() > 4);
    CHECK(pal.size() <= 48u);
}

// End to end: palette from one side drives a dab on the other.
static void test_palette_drives_dab() {
    AppState state;
    DocumentItem* d = makeDoc(state, QStringLiteral("paluse"));
    if (!d) return;
    for (int x = 0; x < 16; ++x) {
        const float t = x / 15.0f;
        fillRect(*d, x, 0, x, 15, RGBAf{1.0f, 0.5f * t, 0, 1});
    }
    state.setForeground(QColor(0, 0, 255));
    const auto pal = state.replacePaletteAt(QPointF(4, 8), 6.0, 0, 0.15, 48);
    CHECK(!pal.empty());
    CHECK(state.replaceColorDab(QPointF(13, 8), 3.0, 1.0, 2, pal, 0.15, 0,
                                false, 0.0));
    CHECK(d->layers[0].pixels->at(13, 8).b > 0.5f);
}

// Faded edges: anti-alias fades the boundary, off cuts hard.
static void test_faded_edges() {
    float edgeOff = 0, innerOff = 0, edgeOn = 0, innerOn = 0;
    for (bool aa : {false, true}) {
        AppState state;
        DocumentItem* d =
            makeDoc(state, QStringLiteral("fade%1").arg(aa));
        if (!d) return;
        fillRect(*d, 0, 0, 7, 15, RGBAf{1, 0, 0, 1});
        fillRect(*d, 8, 0, 15, 15, RGBAf{0, 1, 0, 1});
        state.setForeground(QColor(0, 0, 255));
        CHECK(state.replaceColorDab(QPointF(4, 8), 6.0, 1.0, 2, tgt(1, 0, 0),
                                    0.1, 0, aa, 0.0));
        float& edge = aa ? edgeOn : edgeOff;
        float& inner = aa ? innerOn : innerOff;
        edge = d->layers[0].pixels->at(7, 8).b;    // boundary red
        inner = d->layers[0].pixels->at(4, 8).b;   // deep interior
        CHECK_EQ(d->layers[0].pixels->at(10, 8).g, 1.0f);  // green untouched
    }
    CHECK_NEAR(edgeOff, innerOff, 1e-5f);  // hard edge: identical
    CHECK(innerOn > 0.9f);
    CHECK(edgeOn > 0.0f && edgeOn < innerOn);  // boundary faded
}

// No speckle at full harmony: checker stays blue-family, boundary matches interior.
static void test_harmony_no_speckle() {
    {
        AppState state;
        DocumentItem* d = makeDoc(state, QStringLiteral("speckle"));
        if (!d) return;
        for (int y = 0; y < 16; ++y)
            for (int x = 0; x < 16; ++x)
                d->layers[0].pixels->at(x, y) =
                    ((x + y) & 1) ? RGBAf{1, 0, 0, 1} : RGBAf{0, 1, 0, 1};
        state.setForeground(QColor(0, 0, 255));
        CHECK(state.replaceColorDab(QPointF(8, 8), 20.0, 1.0, 2, tgt(1, 0, 0),
                                    0.3, 0, false, 1.0));
        for (int y = 0; y < 16; ++y)
            for (int x = 0; x < 16; ++x) {
                const RGBAf c = d->layers[0].pixels->at(x, y);
                if ((x + y) & 1) {
                    CHECK(c.b >= c.r);
                    CHECK(c.b >= c.g);
                } else {
                    CHECK_EQ(c.g, 1.0f);
                }
            }
    }
    {
        AppState state;
        DocumentItem* d = makeDoc(state, QStringLiteral("boundspew"));
        if (!d) return;
        fillRect(*d, 0, 0, 7, 15, RGBAf{1, 0, 0, 1});
        fillRect(*d, 8, 0, 15, 15, RGBAf{0, 1, 0, 1});
        state.setForeground(QColor(0, 0, 255));
        CHECK(state.replaceColorDab(QPointF(4, 8), 20.0, 1.0, 2, tgt(1, 0, 0),
                                    0.2, 0, false, 1.0));
        const RGBAf edge = d->layers[0].pixels->at(6, 8);
        const RGBAf deep = d->layers[0].pixels->at(1, 8);
        CHECK_NEAR(edge.r, deep.r, 0.02f);
        CHECK_NEAR(edge.g, deep.g, 0.02f);
        CHECK_NEAR(edge.b, deep.b, 0.02f);
        CHECK_EQ(d->layers[0].pixels->at(10, 8).g, 1.0f);
    }
}

int main(int argc, char** argv) {
    QCoreApplication app(argc, argv);
    const QString logDir =
        QDir::tempPath() + QStringLiteral("/pittore-replace-color-log");
    QDir().mkpath(logDir);
    ::pittore::core::log::set_log_dir(logDir.toStdString().c_str());
    test_color_mode_keeps_luminance();
    test_no_match_no_op();
    test_tolerance_boundary();
    test_contiguous_vs_discontiguous();
    test_find_edges();
    test_hue_saturation_luminosity();
    test_antialias_flag_roundtrip();
    test_sampler();
    test_undo_single_step();
    test_transparency_untouched();
    test_multitarget_gradient();
    test_sample_size_averaging();
    test_harmony_uniform_fixedpoint();
    test_harmony_preserves_texture();
    test_empty_targets_no_op();
    test_tolerance_falloff();
    test_palette_spans_area();
    test_palette_cap();
    test_palette_drives_dab();
    test_faded_edges();
    test_harmony_no_speckle();
    const int rc = pittore_test::failures() == 0 ? 0 : 1;
    std::printf("  checks=%d failures=%d\n", pittore_test::checks(),
                pittore_test::failures());
    return rc;
}
