// test_history.cpp — the undo/redo engine through AppState: per-action
// snapshots, copy-on-write paint edits, multi-select moves and deletes,
// the 100-step cap, and the redo-tail invalidation rules.
//
// Runs headless under QCoreApplication (no widgets are constructed) with the
// CPU backend; XDG_CONFIG_HOME is pointed at a scratch dir by meson so the
// real Settings.toml is never touched, and engine logs are redirected to the
// system temp dir so the user's log files stay clean.
#include <QCoreApplication>
#include <QDir>
#include <QImage>

#include <cstddef>

#include "engine/compute/factory.h"
#include "engine/compute/warp.h"
#include "engine/core/log.h"
#include "test_util.h"
#include "ui/app_state.h"

using namespace pittore::ui;

namespace {

QImage solidImage(int w, int h, QRgb c) {
    QImage img(w, h, QImage::Format_ARGB32_Premultiplied);
    img.fill(c);
    return img;
}

bool isRed(const QColor& c) { return c.red() > 250 && c.green() < 5 && c.blue() < 5; }

// A blue marker painted over the opaque near-white Background, and the
// Background itself — the two states a Liquify shift has to distinguish.
bool isBlue(const QColor& c) { return c.blue() > 200 && c.red() < 60; }
bool isWhite(const QColor& c) { return c.red() > 200 && c.green() > 200 && c.blue() > 200; }

// ---------------------------------------------------------------------------
// The small helpers below emulate a canvas gesture with the exact API sequence
// the canvas uses, so the test and the real input path agree.
// ---------------------------------------------------------------------------

}  // namespace

static void test_history() {
    AppState state;
    DocumentItem* d = state.addDocument(QStringLiteral("hist"), QSize(64, 48), 300);
    CHECK(d != nullptr);
    if (!d) return;
    CHECK(!state.canUndo());
    CHECK(!state.canRedo());

    // --- place two layers: each place is one undo/redo step ----------------
    state.placeImageLayer(solidImage(16, 16, 0xFFFF0000), QStringLiteral("red"),
                          QPointF(32, 24), 1.0);
    CHECK_EQ(d->layers.size(), 2);
    CHECK(state.canUndo());
    CHECK_EQ(state.undoDepth(), 1);
    CHECK(isRed(d->composite.pixelColor(32, 24)));

    state.placeImageLayer(solidImage(16, 16, 0xFF00FF00), QStringLiteral("green"),
                          QPointF(32, 24), 1.0);
    CHECK_EQ(d->layers.size(), 3);
    CHECK_EQ(state.undoDepth(), 2);

    // --- undo removes the last place; redo puts it back --------------------
    state.undo();
    CHECK_EQ(d->layers.size(), 2);
    CHECK(state.canRedo());
    CHECK(isRed(d->composite.pixelColor(32, 24)));   // green is gone
    state.redo();
    CHECK_EQ(d->layers.size(), 3);
    CHECK(!isRed(d->composite.pixelColor(32, 24)));  // green is back on top

    // --- new layer (begin/commit) is one undoable step ---------------------
    state.beginUndoStep();
    LayerItem blank;
    blank.name = QStringLiteral("blank");
    state.addLayer(blank);
    state.commitUndoStep(QStringLiteral("New Layer"), QStringLiteral("newlayer"));
    CHECK_EQ(d->layers.size(), 4);
    state.undo();
    CHECK_EQ(d->layers.size(), 3);

    // --- an aborted gesture adds no history ---------------------------------
    const int depth = state.undoDepth();
    state.beginUndoStep();
    LayerItem ghost;
    ghost.name = QStringLiteral("ghost");
    state.addLayer(ghost);
    state.discardUndoStep();
    CHECK_EQ(d->layers.size(), 4);          // the add still happened …
    CHECK_EQ(state.undoDepth(), depth);    // … but it is not undoable

    // --- select all + move: every pixel layer moves by one delta -----------
    const QPointF off0 = d->layers[0].offset;
    const QPointF off1 = d->layers[1].offset;
    const QPointF off2 = d->layers[2].offset;
    state.selectAllLayers();
    CHECK_EQ(state.selectedLayerIndices().size(), d->layers.size());
    state.clearLayerSelection();
    CHECK_EQ(state.selectedLayerIndices().size(), 1);   // falls back to active

    state.selectAllLayers();
    state.beginUndoStep();
    // Canvas-style gesture: baseline the selection, then deliver the same
    // press-relative delta several times (as mouse-move events do). Each event
    // must place every layer at start + delta — no accumulation, or a drag
    // would send the image flying past the cursor.
    QVector<QPointF> starts;
    for (int idx : state.selectedLayerIndices()) starts.append(d->layers[idx].offset);
    CHECK(state.moveSelectedLayers(QPointF(5, -3), starts));
    CHECK(state.moveSelectedLayers(QPointF(5, -3), starts));   // 2nd mouse-move
    CHECK(state.moveSelectedLayers(QPointF(5, -3), starts));   // 3rd mouse-move
    state.commitUndoStep(QStringLiteral("Move"), QStringLiteral("move"));
    CHECK(d->layers[0].offset == off0 + QPointF(5, -3));   // moved once, not 3×
    CHECK(d->layers[1].offset == off1 + QPointF(5, -3));
    CHECK(d->layers[2].offset == off2 + QPointF(5, -3));
    state.undo();
    CHECK(d->layers[0].offset == off0);
    CHECK(d->layers[1].offset == off1);
    CHECK(d->layers[2].offset == off2);

    // --- delete a multi-selection; undo restores the removed layers --------
    // layers: [ghost, green, red, Background]; selecting {0,1} (ghost+green)
    // removes those two and leaves [red, Background].
    d->selectedLayers = {0, 1};
    state.removeSelectedLayers();
    CHECK_EQ(d->layers.size(), 2);
    CHECK(d->layers[0].name == QStringLiteral("red"));
    state.undo();
    CHECK_EQ(d->layers.size(), 4);

    // Deleting when EVERY layer is selected (Ctrl+A + Del) removes them all —
    // there is no "keep one layer" rule; undo restores the whole stack.
    d->selectedLayers = {0, 1, 2, 3};
    state.removeSelectedLayers();
    CHECK_EQ(d->layers.size(), 0);            // everything deleted …
    CHECK(d->layers.isEmpty());
    state.undo();
    CHECK_EQ(d->layers.size(), 4);            // … undo brings it all back

    // Deleting the LAST remaining layer is allowed too; an empty document is
    // legal and new layers still land on top of it.
    DocumentItem* solo = state.addDocument(QStringLiteral("solo"), QSize(16, 16), 300);
    CHECK(solo != nullptr);
    CHECK_EQ(solo->layers.size(), 1);         // just the Background
    solo->selectedLayers = {0};
    state.removeSelectedLayers();
    CHECK_EQ(solo->layers.size(), 0);         // empty document is fine
    state.beginUndoStep();
    LayerItem again;
    again.name = QStringLiteral("again");
    state.addLayer(again);
    state.commitUndoStep(QStringLiteral("New Layer"), QStringLiteral("newlayer"));
    CHECK_EQ(solo->layers.size(), 1);         // addLayer on an empty doc works
    state.setActiveDocumentIndex(0);          // back to the main doc

    // --- redo is invalidated by a new action after an undo -----------------
    // The all-selected delete was undone above, so redo is already available
    // on the main document; a fresh action must clear it.
    CHECK(state.canRedo());
    state.beginUndoStep();
    LayerItem z;
    z.name = QStringLiteral("z");
    state.addLayer(z);
    state.commitUndoStep(QStringLiteral("New Layer"), QStringLiteral("newlayer"));
    CHECK(!state.canRedo());

    // --- paint then undo: copy-on-write keeps the pre-stroke image ---------
    // Target the green photo layer (index 2 of [z, ghost, green, red, bg]).
    d->activeLayer = 2;
    LayerItem* top = state.activeLayer();
    CHECK(top != nullptr && top->pixels && top->name == QStringLiteral("green"));
    const auto prePixels = top->pixels;
    const QColor before = d->composite.pixelColor(32, 24);
    state.setActiveTool(ToolId::Brush);
    state.beginUndoStep();
    CHECK(state.copyOnWriteActiveLayer());
    CHECK(state.activeLayer()->pixels.get() != prePixels.get());  // cloned
    CHECK(state.paintDab(QPointF(32, 24), 8.0, 1.0, 1.0, QColor(0, 0, 255)));
    state.flushPaint();
    state.commitUndoStep(QStringLiteral("Brush"), QStringLiteral("brush"));
    const QColor painted = d->composite.pixelColor(32, 24);
    CHECK(painted != before);
    state.undo();
    // The snapshot shared the ORIGINAL image; the painted clone is discarded,
    // so the pre-stroke composite (and pixels) come back exactly.
    CHECK(d->composite.pixelColor(32, 24) == before);
    state.redo();
    CHECK(d->composite.pixelColor(32, 24) == painted);

    // --- the 100-step cap: only the newest 100 actions undo ---------------
    const int base = d->layers.size();
    for (int i = 0; i < 110; ++i)
        state.placeImageLayer(solidImage(8, 8, 0xFF111111 + (i % 8) * 0x10101),
                              QStringLiteral("n"), QPointF(32, 24), 1.0);
    CHECK_EQ(d->layers.size(), base + 110);
    CHECK_EQ(state.undoDepth(), 100);    // oldest 10 evicted
    for (int i = 0; i < 100; ++i) state.undo();
    CHECK_EQ(d->layers.size(), base + 10);
    CHECK(!state.canUndo());

    // --- liquify: one history step per stroke; undo restores exactly --------
    DocumentItem* liq =
        state.addDocument(QStringLiteral("liq"), QSize(32, 32), 300);
    CHECK(liq != nullptr);
    if (liq) {
        state.setActiveDocumentIndex(state.documents().size() - 1);
        CHECK(state.activeDocument() == liq);
        liq->activeLayer = 0;   // the opaque Background
        CHECK_EQ(liq->layers.size(), 1);

        // Two blue markers on the Background: one inside the region we warp,
        // one outside it.
        CHECK(state.paintDab(QPointF(8, 8), 2.0, 1.0, 1.0, QColor(0, 0, 255)));
        CHECK(state.paintDab(QPointF(20, 8), 2.0, 1.0, 1.0, QColor(0, 0, 255)));
        state.flushPaint();
        CHECK(isBlue(liq->composite.pixelColor(8, 8)));
        CHECK(isBlue(liq->composite.pixelColor(20, 8)));

        // A mesh that shifts every vertex +4 in x. dst(x,y) fetches src(x+4,y),
        // so the content moves LEFT by four pixels.
        pittore::compute::WarpMesh mesh =
            pittore::compute::make_warp_mesh(32, 32);
        for (std::size_t i = 0; i < mesh.offsets.size(); i += 2)
            mesh.offsets[i] = 4.0f;

        // A region warp only rewrites the region: the marker at x=8 moves to
        // x=4, the marker at x=20 is outside [0,12) and stays put.
        const int depthBefore = state.undoDepth();
        CHECK(state.beginLiquifyStroke());
        CHECK(state.liquifyDab(mesh, 0, 0, 12, 32));
        state.flushPaint();
        CHECK(isBlue(liq->composite.pixelColor(4, 8)));
        CHECK(isWhite(liq->composite.pixelColor(8, 8)));
        CHECK(isBlue(liq->composite.pixelColor(20, 8)));
        state.commitUndoStep(QStringLiteral("Liquify"),
                             QStringLiteral("liquify"));
        CHECK_EQ(state.undoDepth(), depthBefore + 1);

        // Undo restores the exact pre-stroke image; redo re-applies the warp.
        state.undo();
        CHECK(isBlue(liq->composite.pixelColor(8, 8)));
        CHECK(isWhite(liq->composite.pixelColor(4, 8)));
        CHECK(isBlue(liq->composite.pixelColor(20, 8)));
        state.redo();
        CHECK(isBlue(liq->composite.pixelColor(4, 8)));

        // A cancelled stroke restores the pre-stroke pixels and adds no history.
        const int depth2 = state.undoDepth();
        CHECK(state.beginLiquifyStroke());
        CHECK(state.liquifyDab(mesh, 0, 0, 32, 32));
        state.flushPaint();
        CHECK(isBlue(liq->composite.pixelColor(0, 8)));   // marker now at x=0
        state.cancelLiquifyStroke();
        CHECK_EQ(state.undoDepth(), depth2);
        CHECK(isBlue(liq->composite.pixelColor(4, 8)));   // pre-cancel state
        CHECK(isWhite(liq->composite.pixelColor(0, 8)));
    }
}

int main(int argc, char** argv) {
    QCoreApplication app(argc, argv);
    const QString logDir = QDir::tempPath() + QStringLiteral("/pittore-history-log");
    QDir().mkpath(logDir);
    ::pittore::core::log::set_log_dir(logDir.toStdString().c_str());
    test_history();
    const int rc = pittore_test::failures() == 0 ? 0 : 1;
    std::printf("  checks=%d failures=%d\n", pittore_test::checks(),
                pittore_test::failures());
    return rc;
}