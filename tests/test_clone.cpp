// Clone Stamp: exact copies, offset tracking, flow/opacity,
// transparent source, sample-all, single-step undo.
// Headless AppState test, CPU backend.
#include <QCoreApplication>
#include <QDir>

#include <cmath>
#include <cstddef>
#include <cstdint>

#include "engine/core/log.h"
#include "test_util.h"
#include "ui/app_state.h"

using namespace pittore::ui;
using pittore::RGBAf;

namespace {

// Fresh doc with a WxH pixel layer; layer 0 is the test layer.
DocumentItem* makeDoc(AppState& state, const QString& title, int w = 32,
                      int h = 32) {
    DocumentItem* d = state.addDocument(title, QSize(64, 64), 300);
    CHECK(d != nullptr);
    if (!d) return nullptr;
    d->layers.clear();
    LayerItem l;
    l.name = QStringLiteral("T");
    l.kind = LayerItem::Kind::Pixel;
    l.pixels = std::make_shared<pittore::Image>(std::uint32_t(w),
                                                 std::uint32_t(h));
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

}  // namespace

// Basic copy: dab copies red source onto green dest exactly.
static void test_basic_clone() {
    AppState state;
    DocumentItem* d = makeDoc(state, QStringLiteral("basic"));
    if (!d) return;
    fillRect(*d, 0, 0, 15, 31, RGBAf{1, 0, 0, 1});
    fillRect(*d, 16, 0, 31, 31, RGBAf{0, 1, 0, 1});
    CHECK(state.cloneStampDab(QPointF(24, 16), 6.0, 1.0, 1.0, 1.0,
                              QPointF(-16, 0), 0));
    state.endCloneStroke();
    const RGBAf c = d->layers[0].pixels->at(24, 16);
    CHECK(c.r > 0.9f);
    CHECK(c.g < 0.1f);
    CHECK_EQ(c.a, 1.0f);
    // Far from dab, untouched.
    CHECK_EQ(d->layers[0].pixels->at(30, 30).g, 1.0f);
}

// Offset follows the stroke: two dabs copy from two spots.
static void test_offset_tracks() {
    AppState state;
    DocumentItem* d = makeDoc(state, QStringLiteral("track"));
    if (!d) return;
    fillRect(*d, 0, 0, 31, 31, RGBAf{0, 1, 0, 1});
    fillRect(*d, 7, 7, 8, 8, RGBAf{1, 0, 0, 1});    // red witness
    fillRect(*d, 7, 23, 8, 24, RGBAf{0, 0, 1, 1});  // blue witness
    state.beginUndoStep();
    CHECK(state.cloneStampDab(QPointF(24, 8), 3.0, 1.0, 1.0, 1.0,
                              QPointF(-16, 0), 0));
    CHECK(state.cloneStampDab(QPointF(24, 24), 3.0, 1.0, 1.0, 1.0,
                              QPointF(-16, 0), 0));
    state.commitUndoStep(QStringLiteral("Clone Stamp"),
                         QStringLiteral("clone"));
    state.endCloneStroke();
    CHECK(d->layers[0].pixels->at(24, 8).r > 0.9f);
    CHECK(d->layers[0].pixels->at(24, 24).b > 0.9f);
}

// Flow 0.5 mixes halfway in one dab.
static void test_flow_rate() {
    AppState state;
    DocumentItem* d = makeDoc(state, QStringLiteral("flow"));
    if (!d) return;
    fillRect(*d, 0, 0, 31, 31, RGBAf{0, 1, 0, 1});
    fillRect(*d, 0, 0, 15, 31, RGBAf{1, 0, 0, 1});
    CHECK(state.cloneStampDab(QPointF(24, 16), 6.0, 1.0, 1.0, 0.5,
                              QPointF(-16, 0), 0));
    state.endCloneStroke();
    const RGBAf c = d->layers[0].pixels->at(24, 16);
    CHECK_NEAR(c.r, 0.5f, 0.03f);
    CHECK_NEAR(c.g, 0.5f, 0.03f);
}

// Opacity caps the stroke: dabs can't push past it.
static void test_opacity_cap() {
    AppState state;
    DocumentItem* d = makeDoc(state, QStringLiteral("opcap"));
    if (!d) return;
    fillRect(*d, 0, 0, 31, 31, RGBAf{0, 1, 0, 1});
    fillRect(*d, 0, 0, 15, 31, RGBAf{1, 0, 0, 1});
    state.beginUndoStep();
    CHECK(state.cloneStampDab(QPointF(24, 16), 6.0, 1.0, 0.5, 1.0,
                              QPointF(-16, 0), 0));
    CHECK(!state.cloneStampDab(QPointF(24, 16), 6.0, 1.0, 0.5, 1.0,
                               QPointF(-16, 0), 0));  // capped: no-op
    state.commitUndoStep(QStringLiteral("Clone Stamp"),
                         QStringLiteral("clone"));
    state.endCloneStroke();
    const RGBAf c = d->layers[0].pixels->at(24, 16);
    CHECK_NEAR(c.r, 0.5f, 0.03f);
    CHECK_NEAR(c.g, 0.5f, 0.03f);
}

// Transparent source stamps nothing, reports false.
static void test_transparent_source_noop() {
    AppState state;
    DocumentItem* d = makeDoc(state, QStringLiteral("noop"));
    if (!d) return;
    fillRect(*d, 16, 0, 31, 31, RGBAf{0, 1, 0, 1});  // opaque dest
    // Source side stays transparent.
    const int depth = state.undoDepth();
    CHECK(!state.cloneStampDab(QPointF(24, 16), 6.0, 1.0, 1.0, 1.0,
                               QPointF(-20, 0), 0));
    state.endCloneStroke();
    CHECK_EQ(state.undoDepth(), depth);
    CHECK_EQ(d->layers[0].pixels->at(24, 16).g, 1.0f);
}

// Sample All Layers deposits from the composite onto transparency.
static void test_sample_all_deposits() {
    AppState state;
    DocumentItem* d = state.addDocument(QStringLiteral("cloneall"),
                                        QSize(64, 64), 300);
    CHECK(d != nullptr);
    if (!d) return;
    d->layers.clear();
    LayerItem top;
    top.name = QStringLiteral("Top");
    top.kind = LayerItem::Kind::Pixel;
    top.pixels = std::make_shared<pittore::Image>(64, 64);
    top.pixels->fill(RGBAf{0, 0, 0, 0});
    LayerItem bottom;
    bottom.name = QStringLiteral("Bottom");
    bottom.kind = LayerItem::Kind::Pixel;
    bottom.pixels = std::make_shared<pittore::Image>(64, 64);
    bottom.pixels->fill(RGBAf{1, 0, 0, 1});
    d->layers.append(std::move(top));
    d->layers.append(std::move(bottom));
    d->activeLayer = 0;
    d->selectedLayers = QVector<int>{0};
    d->rebuildComposite();
    CHECK(state.cloneStampDab(QPointF(32, 32), 8.0, 1.0, 1.0, 1.0,
                              QPointF(-8, 0), /*All Layers*/ 2));
    state.endCloneStroke();
    const RGBAf c = d->layers[0].pixels->at(32, 32);
    CHECK(c.a > 0.5f);
    CHECK(c.r > 0.5f);
}

// One stroke = one undo step.
static void test_clone_undo_single_step() {
    AppState state;
    DocumentItem* d = makeDoc(state, QStringLiteral("cloneundo"));
    if (!d) return;
    fillRect(*d, 0, 0, 31, 31, RGBAf{0, 0, 1, 1});
    fillRect(*d, 20, 10, 21, 11, RGBAf{0.9f, 0.9f, 0.2f, 1});
    fillRect(*d, 26, 20, 27, 21, RGBAf{0.9f, 0.9f, 0.2f, 1});
    const int depth = state.undoDepth();
    state.beginUndoStep();
    state.copyOnWriteActiveLayer();
    // Two dabs, one undo step.
    CHECK(state.cloneStampDab(QPointF(21, 10), 4.0, 1.0, 1.0, 1.0,
                              QPointF(-8, 0), 0));
    CHECK(state.cloneStampDab(QPointF(27, 20), 4.0, 1.0, 1.0, 1.0,
                              QPointF(-8, 0), 0));
    state.commitUndoStep(QStringLiteral("Clone Stamp"),
                         QStringLiteral("clone"));
    state.endCloneStroke();
    CHECK_EQ(state.undoDepth(), depth + 1);
    CHECK(d->layers[0].pixels->at(21, 10).b > 0.9f);
    CHECK(d->layers[0].pixels->at(27, 20).b > 0.9f);
    state.undo();
    CHECK_EQ(d->layers[0].pixels->at(21, 10).r, 0.9f);
    CHECK_EQ(d->layers[0].pixels->at(27, 20).r, 0.9f);
    state.redo();
    CHECK(d->layers[0].pixels->at(21, 10).b > 0.9f);
}

int main(int argc, char** argv) {
    QCoreApplication app(argc, argv);
    const QString logDir =
        QDir::tempPath() + QStringLiteral("/pittore-clone-log");
    QDir().mkpath(logDir);
    ::pittore::core::log::set_log_dir(logDir.toStdString().c_str());
    test_basic_clone();
    test_offset_tracks();
    test_flow_rate();
    test_opacity_cap();
    test_transparent_source_noop();
    test_sample_all_deposits();
    test_clone_undo_single_step();
    const int rc = pittore_test::failures() == 0 ? 0 : 1;
    std::printf("  checks=%d failures=%d\n", pittore_test::checks(),
                pittore_test::failures());
    return rc;
}
