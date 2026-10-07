// test_layer_stack_ui.cpp — the layer-stack operations behind Group/Ungroup,
// Arrange, and Layers-panel drag-drop: grouping selections into groups,
// exploding them, token moves ±1, bring-to-front/back, drag re-parenting, and
// the group transform helpers (pixel descendants, group bounds, batch move and
// batch scale placements). Every op is a single undoable step.
//
// Runs headless under QCoreApplication (no widgets are constructed) with the
// CPU backend; XDG_CONFIG_HOME is pointed at a scratch dir by meson so the
// real Settings.toml is never touched, and engine logs are redirected to the
// system temp dir so the user's log files stay clean.
#include <QCoreApplication>
#include <QDir>
#include <QImage>

#include <cstddef>
#include <tuple>

#include "engine/compute/factory.h"
#include "engine/compute/warp.h"
#include "engine/core/log.h"
#include "engine/render/layer_style.h"
#include "test_util.h"
#include "ui/app_state.h"
#include "ui/app_state_detail.h"
#include "ui/svg_parts.h"

using namespace pittore::ui;

namespace {

QImage solidImage(int w, int h, QRgb c) {
    QImage img(w, h, QImage::Format_ARGB32_Premultiplied);
    img.fill(c);
    return img;
}

// Panel index 0 = top of the stack. Build an explicit layer list without the
// Background so every index below is exact and obvious.
void buildStack(DocumentItem* d, std::initializer_list<const char*> names) {
    d->layers.clear();
    for (const char* name : names) {
        LayerItem l;
        l.name = QLatin1String(name);
        l.kind = LayerItem::Kind::Pixel;
        d->layers.append(l);
    }
    d->activeLayer = 0;
    d->rebuildComposite();
}

QString stackNames(const DocumentItem& d) {
    QStringList names;
    for (const LayerItem& l : d.layers)
        if (!l.locked) names << l.name;
    return names.join(QLatin1Char(','));
}

void expectStack(const DocumentItem& d, const char* names) {
    CHECK(stackNames(d) == QString::fromLatin1(names));
}

LayerItem& byName(DocumentItem& d, const char* name) {
    for (LayerItem& l : d.layers)
        if (l.name == QLatin1String(name)) return l;
    Q_UNREACHABLE();
}

}  // namespace

static void test_group_and_ungroup() {
    AppState state;
    DocumentItem* d = state.addDocument(QStringLiteral("stack"), QSize(96, 96), 300);
    CHECK(d != nullptr);
    if (!d) return;
    buildStack(d, {"T", "U", "V", "W"});
    CHECK_EQ(d->layers.size(), 4);
    CHECK(d->layers[0].name == QLatin1String("T"));

    // groupSelectedLayers must announce the new header through groupCreated
    // (the panel starts an inline rename on it) with the exact index.
    int created = -1;
    QObject::connect(&state, &AppState::groupCreated, &state,
                     [&created](int idx) { created = idx; });

    // --- a single row cannot group -------------------------------------------
    d->selectedLayers = QVector<int>{0};
    CHECK_EQ(state.groupSelectedLayers(), -1);
    CHECK_EQ(d->layers.size(), 4);

    // --- group the top two: one new group, children re-indented -------------
    d->selectedLayers = QVector<int>{0, 1};
    const int before = state.undoDepth();
    const int g = state.groupSelectedLayers();
    CHECK(g >= 0);
    CHECK_EQ(created, g);               // groupCreated carried the header index
    d->layers[g].name = QStringLiteral("G");    // name is auto-generated otherwise
    CHECK_EQ(d->layers.size(), 5);          // header + 4 previous rows
    CHECK(d->layers[g].kind == LayerItem::Kind::Group);
    CHECK_EQ(d->layers[g].indent, 0);
    CHECK(d->layers[g + 1].name == QLatin1String("T"));
    CHECK_EQ(d->layers[g + 1].indent, 1);
    CHECK(d->layers[g + 2].name == QLatin1String("U"));
    CHECK_EQ(d->layers[g + 2].indent, 1);
    CHECK(d->layers[g + 3].name == QLatin1String("V"));
    CHECK_EQ(d->layers[g + 3].indent, 0);
    CHECK_EQ(d->activeLayer, g);
    CHECK_EQ(d->selectedLayers.size(), 1);
    CHECK_EQ(d->selectedLayers.at(0), g);
    CHECK_EQ(state.undoDepth(), before + 1);

    // --- undo restores the flat stack; redo regroups -------------------------
    state.undo();
    CHECK_EQ(d->layers.size(), 4);
    CHECK(d->layers[0].kind == LayerItem::Kind::Pixel);
    state.redo();
    CHECK_EQ(d->layers.size(), 5);
    CHECK(d->layers[g].kind == LayerItem::Kind::Group);

    // --- ungroup the selected group: headers dropped, children promoted -----
    d->selectedLayers = QVector<int>{g};
    const int before2 = state.undoDepth();
    CHECK(state.ungroupSelectedLayers());
    CHECK_EQ(d->layers.size(), 4);
    CHECK(d->layers[0].kind == LayerItem::Kind::Pixel);
    CHECK(d->layers[0].name == QLatin1String("T"));
    CHECK_EQ(d->layers[0].indent, 0);
    CHECK(d->layers[1].name == QLatin1String("U"));
    CHECK_EQ(d->layers[1].indent, 0);
    CHECK_EQ(d->selectedLayers.size(), 2);   // promoted rows selected
    CHECK_EQ(d->selectedLayers.at(0), 0);
    CHECK_EQ(d->selectedLayers.at(1), 1);
    CHECK_EQ(state.undoDepth(), before2 + 1);
    state.undo();
    CHECK_EQ(d->layers.size(), 5);

    // --- grouping a group spans its whole subtree ---------------------------
    // Layers now: G(0) T(1) U(1) V(0) W(0). Select G and V (a non-contiguous
    // selection): the unit = G's subtree ∪ V.
    d->selectedLayers = QVector<int>{g, g + 3};
    const int g2 = state.groupSelectedLayers();
    CHECK(g2 >= 0);
    d->layers[g2].name = QStringLiteral("G2");
    // new stack: G2(0) G(1) T(2) U(2) V(1) W(0) — 6 rows.
    CHECK_EQ(d->layers.size(), 6);
    CHECK(d->layers[g2].kind == LayerItem::Kind::Group);
    CHECK_EQ(d->layers[g2].indent, 0);
    CHECK(d->layers[g2 + 1].kind == LayerItem::Kind::Group);
    CHECK_EQ(d->layers[g2 + 1].indent, 1);
    CHECK_EQ(d->layers[g2 + 2].indent, 2);
    CHECK_EQ(d->layers[g2 + 3].indent, 2);
    CHECK_EQ(d->layers[g2 + 4].indent, 1);      // V promoted into the super-group
    CHECK_EQ(d->layers[g2 + 5].indent, 0);      // W stays outside
    // Undoing the super-group restores G + V selection.
    state.undo();
    CHECK_EQ(d->layers.size(), 5);
    CHECK_EQ(d->selectedLayers.size(), 2);
    CHECK_EQ(d->selectedLayers.at(0), g);
    CHECK_EQ(d->selectedLayers.at(1), g + 3);

    // --- ungroup fallback: active layer's parent group ----------------------
    // Active = T (a child of G). Deselect everything; ungrouping must explode G.
    d->selectedLayers.clear();
    d->activeLayer = g + 1;
    CHECK(state.ungroupSelectedLayers());
    CHECK_EQ(d->layers.size(), 4);
    CHECK(d->layers[0].name == QLatin1String("T"));
    CHECK_EQ(d->layers[0].indent, 0);
    CHECK_EQ(d->selectedLayers.size(), 2);
    CHECK_EQ(d->selectedLayers.at(0), 0);
    CHECK_EQ(d->selectedLayers.at(1), 1);
}

static void test_move_and_arrange() {
    AppState state;
    DocumentItem* d = state.addDocument(QStringLiteral("move"), QSize(96, 96), 300);
    CHECK(d != nullptr);
    if (!d) return;
    buildStack(d, {"A", "B", "C", "D"});

    // --- move up / down one slot --------------------------------------------
    d->selectedLayers.clear();
    d->activeLayer = 2;                       // C
    CHECK(state.moveSelectedLayersInStack(-1));
    expectStack(*d, "A,C,B,D");
    CHECK_EQ(d->activeLayer, 1);
    CHECK_EQ(d->selectedLayers.size(), 1);
    CHECK_EQ(d->selectedLayers.at(0), 1);
    state.undo();
    expectStack(*d, "A,B,C,D");

    CHECK(state.moveSelectedLayersInStack(1));
    expectStack(*d, "A,B,D,C");
    CHECK_EQ(d->activeLayer, 3);
    CHECK_EQ(d->selectedLayers.at(0), 3);
    state.undo();
    expectStack(*d, "A,B,C,D");

    // --- blocked at the boundaries ------------------------------------------
    d->activeLayer = 0;
    CHECK(!state.moveSelectedLayersInStack(-1));   // already at the top
    d->activeLayer = 3;
    CHECK(!state.moveSelectedLayersInStack(1));    // already at the back

    // --- bring to front / send to back --------------------------------------
    d->activeLayer = 2;                       // C
    const int before = state.undoDepth();
    CHECK(state.bringSelectedLayersToFront());
    expectStack(*d, "C,A,B,D");
    CHECK_EQ(d->activeLayer, 0);
    CHECK_EQ(state.undoDepth(), before + 1);
    state.undo();
    expectStack(*d, "A,B,C,D");
    d->activeLayer = 2;
    CHECK(state.sendSelectedLayersToBack());
    expectStack(*d, "A,B,D,C");
    CHECK_EQ(d->activeLayer, 3);
    state.undo();
    expectStack(*d, "A,B,C,D");

    // --- a group moves with its children ------------------------------------
    d->selectedLayers = QVector<int>{0, 1};
    const int g = state.groupSelectedLayers();
    CHECK(g >= 0);
    d->layers[g].name = QStringLiteral("G");
    // G(0) A(1) B(1) C(0) D(0). Move the group down one: below it is C.
    d->selectedLayers.clear();
    d->activeLayer = g;
    CHECK(state.moveSelectedLayersInStack(1));
    expectStack(*d, "C,G,A,B,D");
    CHECK(d->layers[1].kind == LayerItem::Kind::Group);
    CHECK_EQ(d->layers[2].indent, 1);         // A travelled with the group
    CHECK_EQ(d->layers[3].indent, 1);         // B travelled with the group

    // A child must never escape its group upward past the header: A is at
    // index 2 with indent 1; its parent G (index 1, indent 0) blocks it.
    d->activeLayer = 2;
    CHECK(!state.moveSelectedLayersInStack(-1));
    expectStack(*d, "C,G,A,B,D");
}

static void test_reparent() {
    AppState state;
    DocumentItem* d = state.addDocument(QStringLiteral("reparent"), QSize(96, 96), 300);
    CHECK(d != nullptr);
    if (!d) return;
    buildStack(d, {"A", "B", "C", "D"});

    // Make a group holding A and B: G(0) A(1) B(1) C(0) D(0).
    d->selectedLayers = QVector<int>{0, 1};
    const int g = state.groupSelectedLayers();
    CHECK(g >= 0);
    d->layers[g].name = QStringLiteral("G");
    d->selectedLayers.clear();
    d->activeLayer = g + 3;                   // C

    // --- drag C into the group (drop on G's body → above its first child) ---
    const int before = state.undoDepth();
    CHECK(state.reparentSelectedLayers(g + 1, 1));   // slot = first child, indent 1
    expectStack(*d, "G,C,A,B,D");
    CHECK_EQ(byName(*d, "G").indent, 0);
    CHECK_EQ(byName(*d, "C").indent, 1);
    CHECK_EQ(byName(*d, "A").indent, 1);
    CHECK_EQ(d->activeLayer, g + 1);
    CHECK_EQ(state.undoDepth(), before + 1);
    state.undo();
    expectStack(*d, "G,A,B,C,D");

    // --- drag the group to an end-of-stack drop -----------------------------
    d->selectedLayers = QVector<int>{g};
    CHECK(state.reparentSelectedLayers(d->layers.size(), 0));
    expectStack(*d, "C,D,G,A,B");
    CHECK(d->layers[2].kind == LayerItem::Kind::Group);
    CHECK_EQ(d->layers[3].indent, 1);         // A travelled
    CHECK_EQ(d->layers[4].indent, 1);         // B travelled
    state.undo();
    expectStack(*d, "G,A,B,C,D");

    // --- a drop onto the dragged unit itself is refused ---------------------
    d->selectedLayers = QVector<int>{g};
    const int sizeBefore = d->layers.size();
    const int depthBefore = state.undoDepth();
    CHECK(!state.reparentSelectedLayers(g + 1, 1));   // into itself
    CHECK(!state.reparentSelectedLayers(g + 2, 1));   // onto its own child
    CHECK_EQ(d->layers.size(), sizeBefore);
    CHECK_EQ(state.undoDepth(), depthBefore);

    // --- no-op self drop is allowed and changes nothing ---------------------
    d->selectedLayers = QVector<int>{g};      // caret above the group itself
    CHECK(state.reparentSelectedLayers(g, 0));
    expectStack(*d, "G,A,B,C,D");

    // --- a leaf dragged out of a group loses the indent ---------------------
    d->activeLayer = g + 1;                   // A, inside the group
    d->selectedLayers.clear();
    CHECK(state.reparentSelectedLayers(4, 0));   // drop after C, outside
    expectStack(*d, "G,B,C,A,D");
    CHECK_EQ(byName(*d, "A").indent, 0);
    CHECK_EQ(byName(*d, "G").indent, 0);
    CHECK_EQ(byName(*d, "B").indent, 1);
}

static void test_background_pins_bottom() {
    AppState state;
    DocumentItem* d = state.addDocument(QStringLiteral("bgpin"), QSize(96, 96), 300);
    CHECK(d != nullptr);
    if (!d) return;
    // Real document: the Background layer is locked and sits at the bottom.
    CHECK_EQ(d->layers.size(), 1);
    CHECK(d->layers[0].locked);

    // Place A, B, C → C(0) B(1) A(2) Bg(0) (newest on top).
    for (const char* name : {"A", "B", "C"})
        state.placeImageLayer(solidImage(8, 8, 0xFF888888), QLatin1String(name),
                              QPointF(40, 40), 1.0);
    CHECK_EQ(d->layers.size(), 4);

    // Group the top two: G(0) C(1) B(1) A(0) Bg(0).
    d->selectedLayers = QVector<int>{0, 1};
    const int g = state.groupSelectedLayers();
    CHECK(g >= 0);
    d->layers[g].name = QStringLiteral("G");
    expectStack(*d, "G,C,B,A");

    // --- the locked Background pins the bottom of every operation ----------
    // The panel's end-of-list drop lands just above the Background, never
    // below it.
    d->selectedLayers = QVector<int>{g};
    CHECK(state.reparentSelectedLayers(d->layers.size(), 0));
    expectStack(*d, "A,G,C,B");
    CHECK(d->layers[4].locked);     // Background untouched, still last
    state.undo();
    expectStack(*d, "G,C,B,A");

    // A group moves down within the reachable stack…
    d->activeLayer = g;
    d->selectedLayers.clear();
    CHECK(state.moveSelectedLayersInStack(1));
    expectStack(*d, "A,G,C,B");
    // …but the push past the Background is refused.
    CHECK(!state.moveSelectedLayersInStack(1));
    expectStack(*d, "A,G,C,B");
    CHECK(d->layers[4].locked);

    // Send-to-back lands above the Background, never below it (here A is
    // already the bottom-most movable row, so it is a no-op).
    d->activeLayer = 0;                        // A
    CHECK(state.sendSelectedLayersToBack());
    expectStack(*d, "G,C,B,A");
    CHECK(d->layers.at(4).name == QLatin1String("Background"));
    CHECK(d->layers.at(4).locked);
}

static void test_group_transform_helpers() {
    AppState state;
    DocumentItem* d = state.addDocument(QStringLiteral("gtransform"), QSize(128, 128), 300);
    CHECK(d != nullptr);
    if (!d) return;

    // G(0) left(1) right(1): two pixel layers under one group.
    // placeImageLayer inserts newest on top: right first, then left on top.
    state.placeImageLayer(solidImage(16, 16, 0xFFFF0000), QStringLiteral("right"),
                          QPointF(80, 80), 1.0);
    state.placeImageLayer(solidImage(16, 16, 0xFF00FF00), QStringLiteral("left"),
                          QPointF(30, 30), 1.0);
    d->selectedLayers = QVector<int>{0, 1};
    const int g = state.groupSelectedLayers();
    CHECK(g >= 0);
    d->selectedLayers.clear();

    // --- descendant discovery and bounds -------------------------------------
    // left sits at (22,22)-(38,38), right at (72,72)-(88,88) (offset = center
    // - half the 16 px size for a 16x16 image at scale 1.0).
    const QVector<int> kids = state.groupPixelDescendantIndices(g);
    CHECK_EQ(kids.size(), 2);
    CHECK(d->layers[kids[0]].name == QLatin1String("left"));    // top-first
    CHECK(d->layers[kids[1]].name == QLatin1String("right"));
    const QRectF bounds = state.groupBounds(g);
    CHECK_EQ(bounds.left(), 22.0);
    CHECK_EQ(bounds.top(), 22.0);
    CHECK_EQ(bounds.right(), 88.0);           // 72 + 16
    CHECK_EQ(bounds.bottom(), 88.0);

    // --- translate the whole group as a unit (press-baseline drag) -----------
    const QPointF l0 = d->layers[kids[0]].offset;   // (22,22)
    const QPointF r0 = d->layers[kids[1]].offset;   // (72,72)
    CHECK(state.moveLayersAt(kids, QPointF(5, -3), QVector<QPointF>{l0, r0}));
    CHECK_EQ(d->layers[kids[0]].offset.x(), 27.0);
    CHECK_EQ(d->layers[kids[0]].offset.y(), 19.0);
    CHECK_EQ(d->layers[kids[1]].offset.x(), 77.0);
    CHECK_EQ(d->layers[kids[1]].offset.y(), 69.0);
    // The group bounds follow the drag: old centre (55,55) + (5,-3).
    const QRectF movedBounds = state.groupBounds(g);
    CHECK_EQ(movedBounds.center().x(), 60.0);
    CHECK_EQ(movedBounds.center().y(), 52.0);
    CHECK_EQ(movedBounds.left(), 27.0);       // 22 + 5
    CHECK_EQ(movedBounds.top(), 19.0);        // 22 - 3

    // --- scale the whole group about a fixed anchor --------------------------
    // Anchor: press-time top-left (22,22), factor 2. Update both children at
    // once (the gesture's per-frame placement). Each child pins the source
    // pixel that sat under the anchor at press: aSrc is fixed, then
    // offset = anchor - aSrc * newScale.
    const QPointF anchor(22.0, 22.0);
    const double f = 2.0;
    QVector<QPointF> offsets;
    QVector<double> sxs, sys;
    const QPointF starts[] = {l0, r0};        // press-time offsets, before the move
    const double baseScales[] = {1.0, 1.0};
    QPointF srcAnchors[2];
    for (int i = 0; i < 2; ++i) {
        const QPointF aSrc((anchor.x() - starts[i].x()) / baseScales[i],
                           (anchor.y() - starts[i].y()) / baseScales[i]);
        srcAnchors[i] = aSrc;
        offsets.append(QPointF(anchor.x() - aSrc.x() * (baseScales[i] * f),
                               anchor.y() - aSrc.y() * (baseScales[i] * f)));
        sxs.append(baseScales[i] * f);
        sys.append(baseScales[i] * f);
    }
    CHECK(state.setLayerPlacements(kids, offsets, sxs, sys));
    CHECK_EQ(d->layers[kids[0]].scaleX, 2.0);
    CHECK_EQ(d->layers[kids[1]].scaleX, 2.0);
    // left: aSrc = ((22-22)/1, (22-22)/1) = (0,0) → offset stays (22,22).
    CHECK_EQ(d->layers[kids[0]].offset.x(), 22.0);
    CHECK_EQ(d->layers[kids[0]].offset.y(), 22.0);
    // right: aSrc = ((22-72), (22-72)) = (-50,-50) → offset = (22+100, 22+100).
    CHECK_EQ(d->layers[kids[1]].offset.x(), 122.0);
    CHECK_EQ(d->layers[kids[1]].offset.y(), 122.0);
    // The anchor's source point is stationary for both children.
    for (int i = 0; i < 2; ++i) {
        const LayerItem& l = d->layers[kids[i]];
        const QPointF back(l.offset.x() + srcAnchors[i].x() * l.scaleX,
                           l.offset.y() + srcAnchors[i].y() * l.scaleY);
        CHECK(std::abs(back.x() - anchor.x()) < 1e-6);
        CHECK(std::abs(back.y() - anchor.y()) < 1e-6);
    }
}

static void test_select_all_group_and_outer_group() {
    AppState state;
    DocumentItem* d = state.addDocument(QStringLiteral("selall"), QSize(96, 96), 300);
    CHECK(d != nullptr);
    if (!d) return;

    // Real document: four parts above the locked Background.
    for (const char* name : {"A", "B", "C", "D"})
        state.placeImageLayer(solidImage(8, 8, 0xFF888888), QLatin1String(name),
                              QPointF(40, 40), 1.0);
    CHECK_EQ(d->layers.size(), 5);

    // --- Select All Layers includes the Background, which must not join ---
    state.selectAllLayers();
    CHECK_EQ(d->selectedLayers.size(), 5);
    CHECK_EQ(d->selectedLayers.at(4), 4);       // Background selected
    const int g = state.groupSelectedLayers();
    CHECK(g >= 0);
    d->layers[g].name = QStringLiteral("G");
    // G(0) D(1) C(1) B(1) A(1) Bg(0) — the Background stays OUTSIDE, last.
    expectStack(*d, "G,D,C,B,A");
    CHECK_EQ(d->layers.size(), 6);
    CHECK(d->layers.at(5).locked);
    CHECK(d->layers.at(5).name == QLatin1String("Background"));
    // Moving the new group down is still blocked by the pinned Background.
    d->selectedLayers.clear();
    d->activeLayer = g;
    CHECK(!state.moveSelectedLayersInStack(1));
    expectStack(*d, "G,D,C,B,A");
    // Undo restores the full selection with the Background included.
    state.undo();
    CHECK_EQ(d->layers.size(), 5);
    CHECK_EQ(d->selectedLayers.size(), 5);

    // --- selecting just the Background cannot group --------------------------
    d->selectedLayers = QVector<int>{4};
    CHECK_EQ(state.groupSelectedLayers(), -1);

    // --- outerGroupContaining walks nested groups ----------------------------
    // G(0) D(1) C(1) B(1) A(1): make a super-group from G + its siblings by
    // selecting everything again (Background still filtered out).
    state.selectAllLayers();
    const int g2 = state.groupSelectedLayers();
    CHECK(g2 >= 0);
    d->layers[g2].name = QStringLiteral("S");
    // S(0) G(1) D(2) C(2) B(2) A(2) Bg(0).
    // Outer group of a nested member is the super-group.
    CHECK_EQ(state.outerGroupContaining(g2), -1);   // S itself, no parent
    const int gIdx = 1;                             // G inside S
    const int dIdx = 2;                             // D inside G
    CHECK_EQ(state.outerGroupContaining(gIdx), 0);  // S
    CHECK_EQ(state.outerGroupContaining(dIdx), 0);  // S (outermost wins)
    CHECK_EQ(state.outerGroupContaining(5), -1);    // Background: no group
    // Collapse by re-grouping stops at the innermost parent when the walk
    // starts inside a plain member of the inner group only.
    CHECK_EQ(state.outerGroupContaining(gIdx + 2), 0);  // C inside G → S
}

static void test_reparent_with_background_selected() {
    AppState state;
    DocumentItem* d = state.addDocument(QStringLiteral("repbg"), QSize(96, 96), 300);
    CHECK(d != nullptr);
    if (!d) return;
    for (const char* name : {"A", "B", "C"})
        state.placeImageLayer(solidImage(8, 8, 0xFF888888), QLatin1String(name),
                              QPointF(40, 40), 1.0);
    // C(0) B(1) A(2) Bg(0). Select B + the Background and drag B to the top.
    d->selectedLayers = QVector<int>{1, 3};
    CHECK(state.reparentSelectedLayers(0, 0));
    // B moves; the Background is filtered out and stays at the bottom.
    expectStack(*d, "B,C,A");
    CHECK_EQ(d->layers.size(), 4);
    CHECK(d->layers.at(3).locked);
    CHECK(d->layers.at(3).name == QLatin1String("Background"));
    expectStack(*d, "B,C,A");
}

// Bug 2 (FX slows everything down): the styled raster is anchored to the layer,
// so a pure translation must only move where it is placed — never re-render the
// effects on the CPU. The styledRevision only changes when the content or the
// bake (scale) genuinely changes.
static void test_style_translation_does_not_rerender() {
    AppState state;
    DocumentItem* d =
        state.addDocument(QStringLiteral("fx"), QSize(200, 200), 300);
    CHECK(d != nullptr);
    if (!d) return;
    state.placeImageLayer(solidImage(40, 40, 0xFFFF8844), QStringLiteral("A"),
                          QPointF(100, 100), 1.0);
    LayerItem& a = d->layers[0];
    CHECK(a.pixels != nullptr);
    if (!a.pixels) return;
    pittore::render::LayerStyle st;
    st.hasDropShadow = true;
    st.dropShadow.size = 6.0f;
    st.dropShadow.distance = 4.0f;
    a.style = st;

    d->rebuildComposite();          // renders the style into a.styled
    CHECK(a.styledValid);
    CHECK(a.styled != nullptr);
    if (!a.styled) return;
    const std::uint64_t rev = a.styledRev;
    const QPointF off = a.styledOffset;
    const std::uint32_t w = a.styled->width();
    const std::uint32_t h = a.styled->height();

    // A pure translation must NOT re-render: same revision, same raster, only
    // the placement (styledOffset) follows the offset.
    a.offset += QPointF(12, -7);
    d->rebuildComposite();
    CHECK(a.styledValid);
    CHECK(a.styled != nullptr);
    CHECK_EQ(a.styledRev, rev);                       // no re-render happened
    CHECK_EQ(a.styled->width(), w);
    CHECK_EQ(a.styled->height(), h);
    CHECK(a.styledOffset == off + QPointF(12, -7));   // placement followed
    CHECK_NEAR(a.styledOffset.x(), a.offset.x() - a.style.outset(), 1e-6);
    CHECK_NEAR(a.styledOffset.y(), a.offset.y() - a.style.outset(), 1e-6);

    // A scale change genuinely alters the document footprint → re-render.
    a.scaleX = 2.0;
    a.scaleY = 2.0;
    d->rebuildComposite();
    CHECK(a.styledValid);
    CHECK(a.styled != nullptr);
    CHECK(a.styledRev != rev);
}

// Bug 2 (FX slows everything down): a style on a huge layer is baked at a
// capped resolution and resampled back to the document footprint by
// layerDrawSource, instead of a full-resolution CPU render of the effects.
static void test_style_resolution_cap() {
    AppState state;
    DocumentItem* d =
        state.addDocument(QStringLiteral("bigfx"), QSize(4200, 3200), 300);
    CHECK(d != nullptr);
    if (!d) return;
    state.placeImageLayer(solidImage(4000, 3000, 0xFF4488FF),
                          QStringLiteral("P"), QPointF(0, 0), 1.0);
    LayerItem& p = d->layers[0];
    CHECK(p.pixels != nullptr);
    if (!p.pixels) return;
    pittore::render::LayerStyle st;
    st.hasOuterGlow = true;
    st.outerGlow.size = 12.0f;
    p.style = st;

    d->rebuildComposite();
    CHECK(p.styledValid);
    CHECK(p.styled != nullptr);
    if (!p.styled) return;
    // Long edge capped at 2048 (plus the effect's halo), not 4000.
    CHECK(p.styled->width() <= 2048 + 32);
    CHECK(p.styled->height() <= 2048 + 32);
    CHECK(p.styledResample > 0.0);
    CHECK(p.styledResample < 1.0);
    // layerDrawSource resamples the capped raster back to the full document
    // footprint, so effects keep their exact document size. The raster
    // includes the effect's grown halo, so its doc-span is the content plus a
    // modest reach — never less than the content itself.
    const LayerDrawSource s = layerDrawSource(p);
    CHECK(s.img == p.styled);
    CHECK(s.scaleX > 1.0);
    CHECK(s.scaleY > 1.0);
    const double spanX = s.scaleX * p.styled->width();
    const double spanY = s.scaleY * p.styled->height();
    CHECK(spanX >= 4000.0 && spanX < 4000.0 + 128.0);
    CHECK(spanY >= 3000.0 && spanY < 3000.0 + 128.0);
    CHECK_NEAR(s.scaleX, 1.0 / p.styledResample, 1e-9);
    CHECK_NEAR(s.scaleY, 1.0 / p.styledResample, 1e-9);
}

// Bug 1 (a selection lags other tools): the canvas's derived geometry caches
// key on DocumentItem::revision, which must advance on every composite so a
// stale gizmo bounds is never reused after an edit.
static void test_document_revision() {
    AppState state;
    DocumentItem* d = state.addDocument(QStringLiteral("rev"), QSize(64, 64), 300);
    CHECK(d != nullptr);
    if (!d) return;
    const std::uint64_t r0 = d->revision;
    d->rebuildComposite();
    CHECK(d->revision > r0);
    const std::uint64_t r1 = d->revision;
    d->recompositeRegion(QRect(0, 0, 8, 8));
    CHECK(d->revision > r1);
    // A no-op region does not bump the revision (nothing changed on screen).
    const std::uint64_t r2 = d->revision;
    d->recompositeRegion(QRect());
    CHECK_EQ(d->revision, r2);
}

static void test_select_from_layer_alpha() {
    AppState state;
    DocumentItem* d = state.addDocument(QStringLiteral("selalpha"), QSize(40, 40),
                                        300);
    CHECK(d != nullptr);
    if (!d) return;
    // Real document: the locked Background sits at the bottom; the new layer
    // lands on top → index 0.
    CHECK_EQ(d->layers.size(), 1);
    // A 4×4 fully opaque square centred at (12,12) → placed at offset (10,10).
    state.placeImageLayer(solidImage(4, 4, 0xFFFFFFFF), QStringLiteral("A"),
                          QPointF(12, 12), 1.0);
    CHECK(d->selection.isEmpty());

    // The Ctrl+click-thumbnail path: load the layer's alpha as the mask.
    CHECK(state.selectFromLayerAlpha(0));
    CHECK(d->selectionIsMask);
    CHECK(!d->selectionIsEllipse);
    CHECK(!d->selectionMask.isNull());
    CHECK_EQ(d->selectionMask.width(), 40);
    CHECK_EQ(d->selectionMask.height(), 40);
    CHECK_EQ(d->selectionMask.pixelColor(11, 11).red(), 255);  // inside square
    CHECK_EQ(d->selectionMask.pixelColor(5, 5).red(), 0);      // far outside
    CHECK(!d->selection.isEmpty());

    // One undoable step: Undo restores the previous (empty) selection, mask
    // included.
    const int depth = state.undoDepth();
    CHECK(state.canUndo());
    state.undo();
    CHECK_EQ(state.undoDepth(), depth - 1);
    CHECK(d->selection.isEmpty());
    CHECK(!d->selectionIsMask);
    CHECK(d->selectionMask.isNull());

    // A fully transparent layer selects nothing and pushes no undo step.
    state.placeImageLayer(solidImage(4, 4, 0x00000000), QStringLiteral("T"),
                          QPointF(12, 12), 1.0);
    const int depth2 = state.undoDepth();
    CHECK(!state.selectFromLayerAlpha(0));
    CHECK_EQ(state.undoDepth(), depth2);
    CHECK(d->selection.isEmpty());
}

static void test_select_from_group_alpha() {
    AppState state;
    DocumentItem* d = state.addDocument(QStringLiteral("selgroup"), QSize(40, 40),
                                        300);
    CHECK(d != nullptr);
    if (!d) return;
    // Two overlapping opaque 4×4 squares; newest on top → B(0) A(1) Bg(2).
    state.placeImageLayer(solidImage(4, 4, 0xFFFFFFFF), QStringLiteral("A"),
                          QPointF(10, 10), 1.0);   // offset (8,8)  → doc [8..14)
    state.placeImageLayer(solidImage(4, 4, 0xFFFFFFFF), QStringLiteral("B"),
                          QPointF(14, 14), 1.0);   // offset (12,12) → doc [12..17)
    d->selectedLayers = QVector<int>{0, 1};
    const int g = state.groupSelectedLayers();
    CHECK(g >= 0);
    CHECK(d->layers[g].kind == LayerItem::Kind::Group);

    // Loading from the group row outlines the assembled union of both shapes.
    CHECK(state.selectFromLayerAlpha(g));
    CHECK(d->selectionIsMask);
    CHECK(!d->selectionMask.isNull());
    CHECK_EQ(d->selectionMask.pixelColor(9, 9).red(), 255);    // A only
    CHECK_EQ(d->selectionMask.pixelColor(15, 15).red(), 255);  // B only
    CHECK_EQ(d->selectionMask.pixelColor(4, 4).red(), 0);      // neither
    CHECK(d->selection.contains(QPointF(9, 9)));
    CHECK(d->selection.contains(QPointF(15, 15)));

    state.undo();
    CHECK(d->selection.isEmpty());
}

static void test_group_collapse_and_group_visibility() {
    AppState state;
    DocumentItem* d = state.addDocument(QStringLiteral("grpvis"), QSize(64, 64), 300);
    CHECK(d != nullptr);
    if (!d) return;
    if (d->composite.isNull()) d->rebuildComposite();
    const int bgR = d->composite.pixelColor(5, 5).red();

    // A red square at (12,12) and a blue square at (44,44), doc 64×64. When
    // grouped, hiding the group's eye hides both children.
    state.placeImageLayer(solidImage(8, 8, 0xFFFF0000), QStringLiteral("A"),
                          QPointF(16, 16), 1.0);   // offset (12,12) → doc [12..20)
    state.placeImageLayer(solidImage(8, 8, 0xFF0000FF), QStringLiteral("B"),
                          QPointF(48, 48), 1.0);   // offset (44,44) → doc [44..52)
    d->selectedLayers = QVector<int>{0, 1};
    const int g = state.groupSelectedLayers();
    CHECK(g >= 0);
    CHECK(d->layers[g].kind == LayerItem::Kind::Group);
    // Stack: G(0) B(1) A(2) Bg(3).

    CHECK_EQ(d->composite.pixelColor(14, 14).red(), 255);
    CHECK_EQ(d->composite.pixelColor(46, 46).blue(), 255);

    // Group eye → children follow (effective visibility propagates down the
    // enclosing-group chain; the Background is outside every group).
    d->layers[g].visible = false;
    d->rebuildComposite();
    CHECK_EQ(d->composite.pixelColor(14, 14).red(), bgR);
    CHECK_EQ(d->composite.pixelColor(46, 46).blue(), bgR);
    CHECK(!d->effectivelyVisible(g + 1));
    CHECK(!d->effectivelyVisible(g + 2));
    CHECK(d->effectivelyVisible(3));   // Bg unaffected
    d->layers[g].visible = true;
    d->rebuildComposite();
    CHECK_EQ(d->composite.pixelColor(14, 14).red(), 255);
    CHECK(d->effectivelyVisible(g + 1));
    CHECK_EQ(d->enclosingGroups(g + 1).front(), g);   // innermost ancestor first

    // Collapse is a UI toggle: children rows hide, the row itself and anything
    // outside never do, and it must not be undoable.
    CHECK(d->layers[g].groupExpanded);
    CHECK(!d->rowHiddenByCollapsedGroup(g));
    CHECK(!d->rowHiddenByCollapsedGroup(g + 1));
    const int depth = state.undoDepth();
    state.toggleLayerGroupExpanded(g, false, false);
    CHECK(!d->layers[g].groupExpanded);
    CHECK(!d->rowHiddenByCollapsedGroup(g));
    CHECK(d->rowHiddenByCollapsedGroup(g + 1));
    CHECK(d->rowHiddenByCollapsedGroup(g + 2));
    CHECK(!d->rowHiddenByCollapsedGroup(3));         // Bg unaffected
    CHECK_EQ(state.undoDepth(), depth);              // not undoable
    // Collapse never touches the composite: children still render.
    CHECK_EQ(d->composite.pixelColor(14, 14).red(), 255);
    state.toggleLayerGroupExpanded(g, false, false);
    CHECK(d->layers[g].groupExpanded);
    CHECK(!d->rowHiddenByCollapsedGroup(g + 1));

    // Ctrl/Cmd+chevron flips every group; Expand/Collapse All sets them all.
    state.toggleLayerGroupExpanded(g, false, true);
    CHECK(!d->layers[g].groupExpanded);
    state.setAllGroupsExpanded(true);
    CHECK(d->layers[g].groupExpanded);

    // Alt/Option+chevron flips this group AND every group nested inside it.
    d->selectedLayers = QVector<int>{g + 1, g + 2};   // B and A
    const int g2 = state.groupSelectedLayers();
    CHECK(g2 >= 0);
    CHECK_EQ(d->layers[g2].indent, 1);               // nested one level deep
    CHECK(d->enclosingGroups(g2).contains(g));
    state.toggleLayerGroupExpanded(g, true, false);
    CHECK(!d->layers[g].groupExpanded);
    CHECK(!d->layers[g2].groupExpanded);             // nested group followed
    state.setAllGroupsExpanded(true);

    // Nested group visibility: hiding G2 hides only its subtree.
    d->layers[g2].visible = false;
    CHECK(!d->effectivelyVisible(g2));
    CHECK(!d->effectivelyVisible(g2 + 1));           // B under G2
    CHECK(d->effectivelyVisible(d->layers.size() - 1));  // Bg outside
    d->layers[g2].visible = true;
    CHECK(d->effectivelyVisible(g2 + 1));
}

static void test_group_thumbnail() {
    AppState state;
    DocumentItem* d = state.addDocument(QStringLiteral("grpthumb"), QSize(64, 64),
                                        300);
    CHECK(d != nullptr);
    if (!d) return;
    state.placeImageLayer(solidImage(8, 8, 0xFFFF0000), QStringLiteral("A"),
                          QPointF(16, 16), 1.0);
    state.placeImageLayer(solidImage(8, 8, 0xFF0000FF), QStringLiteral("B"),
                          QPointF(48, 48), 1.0);
    d->selectedLayers = QVector<int>{0, 1};
    const int g = state.groupSelectedLayers();
    CHECK(g >= 0);

    // Combined preview: both squares present, transparent margins (the union
    // of their bounds covers the box, with empty space around each square).
    const QImage t1 = groupThumbnail(*d, g, 32);
    CHECK(!t1.isNull());
    CHECK_EQ(t1.width(), 32);
    CHECK_EQ(t1.height(), 32);
    int opaque = 0, clear = 0;
    for (int y = 0; y < 32; ++y)
        for (int x = 0; x < 32; ++x)
            (qAlpha(t1.pixel(x, y)) > 0 ? opaque : clear)++;
    CHECK(opaque > 20);
    CHECK(clear > 20);

    // Same stamp → the cached image is returned (implicitly shared).
    const QImage t2 = groupThumbnail(*d, g, 32);
    CHECK(!t2.isNull());
    CHECK(t2.cacheKey() == t1.cacheKey());

    // Hiding every child reports an empty group.
    d->layers[g + 1].visible = false;
    d->layers[g + 2].visible = false;
    CHECK(groupThumbnail(*d, g, 32).isNull());
    d->layers[g + 1].visible = true;
    d->layers[g + 2].visible = true;
    CHECK(!groupThumbnail(*d, g, 32).isNull());

    // Hiding the group itself reports an empty group too (ancestor visibility
    // propagates into the preview).
    d->layers[g].visible = false;
    CHECK(groupThumbnail(*d, g, 32).isNull());
    d->layers[g].visible = true;
    CHECK(!groupThumbnail(*d, g, 32).isNull());

    // Nested groups keep working: a group inside the group still contributes.
    d->selectedLayers = QVector<int>{g + 1, g + 2};
    const int g2 = state.groupSelectedLayers();
    CHECK(g2 >= 0);
    CHECK(!groupThumbnail(*d, g, 32).isNull());

    // A group with no pixel children has no combined preview.
    LayerItem empty;
    empty.kind = LayerItem::Kind::Group;
    empty.name = QStringLiteral("Empty");
    d->layers.prepend(empty);
    CHECK(groupThumbnail(*d, 0, 32).isNull());
}

static void test_effective_visibility_matches_scanback() {
    // The linear effectiveVisibility() sweep must agree with the per-index
    // effectivelyVisible()/parentGroupIndex() scan-backs on every structure:
    // flat, nested, hidden groups, and indent jumps. Hot paths rely on it.
    AppState state;
    DocumentItem* d = state.addDocument(QStringLiteral("vis"),
                                        QSize(64, 64), 300);
    CHECK(d != nullptr);
    if (!d) return;
    auto setRows = [&](std::initializer_list<std::tuple<const char*, int, bool, bool>> rows) {
        d->layers.clear();
        for (const auto& r : rows) {
            LayerItem l;
            l.name = QLatin1String(std::get<0>(r));
            l.kind = std::get<3>(r) ? LayerItem::Kind::Group
                                    : LayerItem::Kind::Pixel;
            l.indent = std::get<1>(r);
            l.visible = std::get<2>(r);
            d->layers.append(l);
        }
    };
    auto checkAll = [&]() {
        QVector<char> vis;
        QVector<int> parent;
        d->effectiveVisibility(vis, parent);
        CHECK(vis.size() == d->layers.size());
        CHECK(parent.size() == d->layers.size());
        for (int i = 0; i < d->layers.size(); ++i) {
            CHECK((bool)vis[i] == d->effectivelyVisible(i));
            CHECK(parent[i] == parentGroupIndex(*d, i));
        }
    };
    // Flat mix.
    setRows({{"a", 0, true, false},
             {"b", 0, false, false},
             {"c", 0, true, false}});
    checkAll();
    // Nested groups with a hidden middle group and hidden leaf.
    setRows({{"G0", 0, true, true},
             {"a", 1, true, false},
             {"b", 1, false, false},
             {"G1", 0, true, true},
             {"c", 1, true, false},
             {"H", 1, false, true},
             {"e", 2, true, false},
             {"f", 0, true, false}});
    checkAll();
    // Toggle each group hidden in turn: the chain AND must match exactly.
    for (int g : {0, 3, 5}) {
        d->layers[g].visible = false;
        checkAll();
        d->layers[g].visible = true;
    }
    checkAll();
    // Indent jumps and shield rows (a shallower pixel ends the subtree).
    setRows({{"G", 0, true, true},
             {"P", 2, true, false},
             {"R", 1, true, false}});
    checkAll();
    setRows({{"G", 0, true, true},
             {"H", 1, true, true},
             {"X", 0, true, false},
             {"R", 1, true, false}});
    checkAll();
    setRows({{"G", 0, false, true},
             {"H", 1, true, true},
             {"X", 0, true, false},
             {"R", 1, true, false}});
    checkAll();
}

static void test_placed_svg_rebases_into_group() {
    // Dropping a grouped SVG onto a row inside a group must not split the
    // outer group: the incoming depths rebase to siblings of the drop row,
    // so the outer group's subtree scan (and combined thumbnail) survives.
    AppState state;
    DocumentItem* d = state.addDocument(QStringLiteral("svgdrop"),
                                        QSize(64, 64), 300);
    CHECK(d != nullptr);
    if (!d) return;
    state.placeImageLayer(solidImage(8, 8, 0xFFFF0000), QStringLiteral("x"),
                          QPointF(10, 10), 1.0);
    state.placeImageLayer(solidImage(8, 8, 0xFF0000FF), QStringLiteral("y"),
                          QPointF(40, 40), 1.0);
    d->selectedLayers = QVector<int>{0, 1};
    const int h = state.groupSelectedLayers();
    CHECK(h >= 0);

    static const char* kSvg =
        "<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"64\" height=\"64\">"
        "<g id=\"A\">"
        "<rect x=\"40\" y=\"40\" width=\"10\" height=\"10\" fill=\"#00ff00\"/>"
        "</g>"
        "<g id=\"B\">"
        "<rect x=\"4\" y=\"4\" width=\"6\" height=\"6\" fill=\"#0000ff\"/>"
        "</g>"
        "</svg>";
    SvgImportResult svg;
    int dpi = 96;
    QString error;
    CHECK(svgPartsImport(QByteArray(kSvg), &svg, &dpi, &error));
    // Drop onto x, one level inside H.
    int xrow = -1;
    for (int i = 0; i < d->layers.size(); ++i)
        if (d->layers[i].name == QStringLiteral("x")) xrow = i;
    CHECK(xrow >= 0);
    d->activeLayer = xrow;
    d->selectedLayers = QVector<int>{xrow};
    QString placeError;
    CHECK(state.placeSvgParts(QStringLiteral("s.svg"), svg, QPointF(32, 32),
                              &placeError));

    // x still belongs to H (not stranded inside the last SVG group).
    d = state.activeDocument();
    int hrow = -1;
    xrow = -1;
    for (int i = 0; i < d->layers.size(); ++i) {
        if (d->layers[i].name == QStringLiteral("x")) xrow = i;
        if (d->layers[i].indent == 0 &&
            d->layers[i].kind == LayerItem::Kind::Group)
            hrow = i;
    }
    CHECK(xrow >= 0 && hrow >= 0);
    CHECK(d->enclosingGroups(xrow).contains(hrow));

    // Every group — the outer H included — renders a combined thumbnail.
    for (int i = 0; i < d->layers.size(); ++i) {
        if (d->layers[i].kind != LayerItem::Kind::Group) continue;
        CHECK(!groupThumbnail(*d, i, 32).isNull());
    }
}

int main(int argc, char** argv) {
    QCoreApplication app(argc, argv);
    const QString logDir = QDir::tempPath() + QStringLiteral("/pittore-layer-stack-log");
    QDir().mkpath(logDir);
    ::pittore::core::log::set_log_dir(logDir.toStdString().c_str());
    test_group_and_ungroup();
    test_move_and_arrange();
    test_reparent();
    test_background_pins_bottom();
    test_group_transform_helpers();
    test_select_all_group_and_outer_group();
    test_reparent_with_background_selected();
    test_style_translation_does_not_rerender();
    test_style_resolution_cap();
    test_document_revision();
    test_select_from_layer_alpha();
    test_select_from_group_alpha();
    test_group_collapse_and_group_visibility();
    test_group_thumbnail();
    test_effective_visibility_matches_scanback();
    test_placed_svg_rebases_into_group();
    const int rc = pittore_test::failures() == 0 ? 0 : 1;
    std::printf("  checks=%d failures=%d\n", pittore_test::checks(),
                pittore_test::failures());
    return rc;
}