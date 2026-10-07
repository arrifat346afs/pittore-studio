// test_selection_ops.cpp — selection <-> mask interchange + Modify ops.
//
// Reveal/Hide Selection, From Transparency, Load Mask as Selection and the
// five Modify ops through AppState, with pixel-level checks and undo.
#include <QCoreApplication>
#include <QDir>

#include <cmath>
#include <cstdio>

#include "engine/compute/layer_mask.h"
#include "engine/core/log.h"
#include "test_util.h"
#include "ui/app_state.h"
#include "ui/selection_mask.h"

using namespace pittore::ui;

namespace {

std::shared_ptr<pittore::Image> whiteImg(int w = 8, int h = 4) {
    auto img = std::make_shared<pittore::Image>(
        static_cast<std::uint32_t>(w), static_cast<std::uint32_t>(h));
    img->fill(pittore::RGBAf{1, 1, 1, 1});
    return img;
}

DocumentItem* makeDoc(AppState& state) {
    DocumentItem* doc = state.addDocument(QStringLiteral("t"), QSize(8, 4), 72);
    if (!doc) return nullptr;
    LayerItem l;
    l.name = QStringLiteral("w");
    l.kind = LayerItem::Kind::Pixel;
    l.pixels = whiteImg();
    l.sourceStamp = 1;
    doc->layers = {l};
    state.setActiveLayerIndex(0);
    return doc;
}

}  // namespace

static void test_reveal_hide() {
    {
        AppState state;
        DocumentItem* doc = makeDoc(state);
        CHECK(doc != nullptr);
        if (!doc) return;
        state.setSelection(QRectF(0, 0, 4, 4), false);
        CHECK(state.maskRevealSelection());
        CHECK(doc->layers[0].hasMask);
        doc->rebuildComposite();
        CHECK_EQ(doc->composite.pixelColor(1, 1).alpha(), 255);
        CHECK_EQ(doc->composite.pixelColor(6, 1).alpha(), 0);
        state.undo();
        CHECK(!state.activeDocument()->layers[0].hasMask);
    }
    {
        AppState state;
        DocumentItem* doc = makeDoc(state);
        CHECK(doc != nullptr);
        if (!doc) return;
        state.setSelection(QRectF(0, 0, 4, 4), false);
        CHECK(state.maskHideSelection());
        doc->rebuildComposite();
        CHECK_EQ(doc->composite.pixelColor(1, 1).alpha(), 0);
        CHECK_EQ(doc->composite.pixelColor(6, 1).alpha(), 255);
    }
    // No selection: refused with a hint, no mask made.
    {
        AppState state;
        DocumentItem* doc = makeDoc(state);
        CHECK(doc != nullptr);
        if (!doc) return;
        CHECK(!state.maskRevealSelection());
        CHECK(!doc->layers[0].hasMask);
    }
}

static void test_from_transparency() {
    AppState state;
    DocumentItem* doc = makeDoc(state);
    CHECK(doc != nullptr);
    if (!doc) return;
    for (int i = 0; i < 32; ++i)
        doc->layers[0].pixels->data()[i].a = (i % 8 < 4) ? 1.0f : 0.0f;
    CHECK(state.maskFromTransparency());
    const LayerItem& l = doc->layers[0];
    CHECK(l.hasMask && l.mask);
    bool ok = true;
    for (int i = 0; ok && i < 32; ++i) {
        ok = (i % 8 < 4) ? (l.mask->data()[i].r > 0.99f)
                         : (l.mask->data()[i].r < 0.01f);
        ok = ok && l.pixels->data()[i].a > 0.99f;
    }
    CHECK(ok);
    state.undo();
    CHECK(!state.activeDocument()->layers[0].hasMask);
}

static void test_load_mask() {
    AppState state;
    DocumentItem* doc = makeDoc(state);
    CHECK(doc != nullptr);
    if (!doc) return;
    auto m = std::make_shared<pittore::Image>(8, 4);
    for (int y = 0; y < 4; ++y)
        for (int x = 0; x < 8; ++x)
            m->data()[y * 8 + x] =
                pittore::compute::make_mask_pixel(x < 4 ? 1.0f : 0.0f);
    doc->layers[0].mask = std::move(m);
    doc->layers[0].hasMask = true;
    CHECK(state.loadMaskAsSelection());
    const QRectF sel = state.activeDocument()->selection;
    CHECK_EQ(static_cast<int>(sel.x()), 0);
    CHECK_EQ(static_cast<int>(sel.width()), 4);
}

static void test_modify() {
    AppState state;
    DocumentItem* doc = makeDoc(state);
    CHECK(doc != nullptr);
    if (!doc) return;
    // 2x2 block at (3,1)-(5,3).
    state.setSelection(QRectF(3, 1, 2, 2), false);
    CHECK(state.modifySelectionExpand(1));
    QRectF sel = state.activeDocument()->selection;
    CHECK_EQ(static_cast<int>(sel.x()), 2);
    CHECK_EQ(static_cast<int>(sel.width()), 4);
    CHECK(state.modifySelectionContract(1));
    sel = state.activeDocument()->selection;
    CHECK_EQ(static_cast<int>(sel.x()), 3);
    CHECK_EQ(static_cast<int>(sel.width()), 2);
    // Feather keeps the core, softens outward (4x4 block, radius 1).
    state.setSelection(QRectF(1, 0, 4, 4), false);
    CHECK(state.modifySelectionFeather(1));
    const QImage soft = selectionAsMask(*state.activeDocument());
    CHECK(soft.constScanLine(2)[1] > 127);
    CHECK(soft.constScanLine(2)[0] > 0);
    // Smooth keeps a live selection.
    state.setSelection(QRectF(1, 0, 4, 4), false);
    CHECK(state.modifySelectionSmooth(1));
    CHECK(!state.activeDocument()->selection.isEmpty());
    // Border: edge band selected, centre not.
    state.setSelection(QRectF(1, 0, 4, 4), false);
    CHECK(state.modifySelectionBorder(1));
    const QImage band = selectionAsMask(*state.activeDocument());
    CHECK(band.constScanLine(2)[1] > 127);
    CHECK(band.constScanLine(2)[3] < 128);
}

static void test_refine_ops() {
    // --- edge snap: soft ramp offset from a hard luma step ---------------
    // Luma: black x<10, white x>=10 (20x8). Mask ramps 6..14, so the matte
    // edge sits right of the true edge; snapping must push left-of-edge
    // pixels down and right-of-edge pixels up.
    QImage luma(20, 8, QImage::Format_Grayscale8);
    QImage ramp(20, 8, QImage::Format_Grayscale8);
    for (int y = 0; y < 8; ++y) {
        uchar* lrow = luma.scanLine(y);
        uchar* rrow = ramp.scanLine(y);
        for (int x = 0; x < 20; ++x) {
            lrow[x] = x < 10 ? 0 : 255;
            rrow[x] = static_cast<uchar>(
                std::clamp((x - 6) * 255 / 8, 0, 255));
        }
    }
    const int before8 = ramp.constScanLine(4)[8];
    const int before11 = ramp.constScanLine(4)[11];
    const QImage snapped = selectionMaskSnapToEdges(luma, ramp, 8, 3);
    CHECK(snapped.size() == ramp.size());
    CHECK(snapped.constScanLine(4)[8] < before8);    // drained toward empty
    CHECK(snapped.constScanLine(4)[11] > before11);  // filled toward full
    // Flat luma: no edges, matte passes through untouched.
    QImage flat(20, 8, QImage::Format_Grayscale8);
    flat.fill(128);
    const QImage still = selectionMaskSnapToEdges(flat, ramp, 8, 3);
    CHECK(still.constScanLine(4)[8] == before8);
    CHECK(still.constScanLine(4)[11] == before11);

    // --- colour affinity: dark hair over a cool backdrop -----------------
    // Backdrop (35,60,70) and hair (60,40,35) sit within 8 luma of each
    // other, so a tone-only solve honestly refuses (no-op). The colour
    // solve separates them and lands the matte on the hair edge — select
    // the hair, keep the backdrop out.
    QImage cool(64, 48, QImage::Format_ARGB32);
    for (int y = 0; y < 48; ++y) {
        QRgb* row = reinterpret_cast<QRgb*>(cool.scanLine(y));
        for (int x = 0; x < 64; ++x)
            row[x] = (x >= 24 && x <= 39) ? qRgb(60, 40, 35)
                                           : qRgb(35, 60, 70);
    }
    QImage marquee(64, 48, QImage::Format_Grayscale8);
    marquee.fill(0);
    for (int y = 4; y <= 43; ++y) {
        uchar* row = marquee.scanLine(y);
        for (int x = 24; x <= 37; ++x) row[x] = 255;
    }
    const QImage toneOnly = selectionMaskSnapToEdges(
        cool.convertToFormat(QImage::Format_Grayscale8), marquee, 4, 3);
    CHECK(toneOnly.constScanLine(24)[38] == 0);  // tone guard: refused, no-op
    const QImage colourSnap = selectionMaskSnapToEdges(cool, marquee, 4, 3);
    CHECK(colourSnap.constScanLine(24)[30] == 255);  // interior untouched
    CHECK(colourSnap.constScanLine(24)[39] >= 200);  // hair pulled to the edge
    CHECK(colourSnap.constScanLine(24)[40] <= 55);   // backdrop rejected
    CHECK(colourSnap.constScanLine(24)[22] <= 55);   // left backdrop too
    CHECK(colourSnap.constScanLine(2)[30] >= 200);   // hair above the marquee

    // --- matte brush paint on the same boundary --------------------------
    // The selection overshoots 4 px of backdrop right of the hair; a stroke
    // along the edge must keep the hair and clear the backdrop it covers.
    QImage over(64, 48, QImage::Format_Grayscale8);
    over.fill(0);
    for (int y = 4; y <= 43; ++y) {
        uchar* row = over.scanLine(y);
        for (int x = 24; x <= 43; ++x) row[x] = 255;
    }
    const QImage painted =
        selectionMaskMatteSolve(cool, over, QPointF(41, 24), 6.0);
    CHECK(painted.constScanLine(24)[30] == 255);  // hair interior untouched
    CHECK(painted.constScanLine(24)[39] >= 200);  // hair edge kept
    CHECK(painted.constScanLine(24)[40] <= 55);   // backdrop at the edge gone
    CHECK(painted.constScanLine(24)[41] <= 55);   // stroke centre cleared
    CHECK(painted.constScanLine(24)[44] == 0);    // outside the marquee stays

    // --- backdrop cast on the subject never reads as wall ---------------
    // Backlit edges (ears, wet hair, coat rims) wear the wall's colour. A
    // rim column between the tight 12-RMS proof and the old loose one used
    // to be eaten by colour-based clearing. Solid cast rim must survive, a
    // feathered cast pixel must never fall without proof, and the real
    // wall overshoot must still clear.
    QImage cast(64, 48, QImage::Format_ARGB32);
    QImage castMask(64, 48, QImage::Format_Grayscale8);
    for (int y = 0; y < 48; ++y) {
        QRgb* crow = reinterpret_cast<QRgb*>(cast.scanLine(y));
        uchar* mrow = castMask.scanLine(y);
        for (int x = 0; x < 64; ++x) {
            crow[x] = (x >= 24 && x <= 39) ? qRgb(60, 40, 35)
                                           : qRgb(35, 60, 70);
            if (x == 38 || x == 39) crow[x] = qRgb(50, 80, 90);  // cast rim
            mrow[x] = x <= 43 ? 255 : 0;
        }
        if (y == 24) mrow[39] = 150;  // feathered rim pixel
    }
    const QImage rim =
        selectionMaskMatteSolve(cast, castMask, QPointF(41, 24), 6.0);
    CHECK(rim.constScanLine(24)[38] == 255);  // solid cast rim kept
    CHECK(rim.constScanLine(24)[39] >= 150);  // soft cast rim never falls
    CHECK(rim.constScanLine(24)[41] <= 55);   // real wall overshoot cleared
    CHECK(rim.constScanLine(24)[30] == 255);  // interior untouched

    // --- feather stamp softens a hard boundary ---------------------------
    QImage hard(20, 8, QImage::Format_Grayscale8);
    for (int y = 0; y < 8; ++y) {
        uchar* row = hard.scanLine(y);
        for (int x = 0; x < 20; ++x) row[x] = x < 10 ? 255 : 0;
    }
    const QImage soft =
        selectionMaskFeatherStamp(hard, QPointF(10, 4), 3.0);
    const int edge = soft.constScanLine(4)[10];
    CHECK(edge > 0 && edge < 255);
    CHECK(soft.constScanLine(4)[0] == 255);   // far field untouched
    CHECK(soft.constScanLine(4)[19] == 0);

    // --- decontamination unmixes fringe ----------------------------------
    // Red foreground (cov 255) | purple fringe (cov 128) | blue background.
    QImage straight(8, 4, QImage::Format_RGBA8888);
    QImage cov(8, 4, QImage::Format_Grayscale8);
    for (int y = 0; y < 4; ++y) {
        QRgb* srow = reinterpret_cast<QRgb*>(straight.scanLine(y));
        uchar* crow = cov.scanLine(y);
        for (int x = 0; x < 8; ++x) {
            if (x < 3) {
                srow[x] = qRgba(255, 0, 0, 255);
                crow[x] = 255;
            } else if (x == 3) {
                srow[x] = qRgba(128, 0, 128, 255);
                crow[x] = 128;
            } else {
                srow[x] = qRgba(0, 0, 255, 255);
                crow[x] = 0;
            }
        }
    }
    CHECK(decontaminateStraightRgba(straight, cov));
    const QRgb fixed = reinterpret_cast<QRgb*>(straight.scanLine(1))[3];
    CHECK(qRed(fixed) > 200);    // fringe red restored toward pure red
    CHECK(qBlue(fixed) < 60);    // background blue removed
    // Solid areas untouched.
    const QRgb kept = reinterpret_cast<QRgb*>(straight.scanLine(1))[1];
    CHECK(kept == qRgba(255, 0, 0, 255));
}

static void test_refine_matte_and_ramp() {
    // --- matte solve separates by color ---------------------------------
    // Red left / blue right, matte confident outside 8..12, uncertain inside.
    QImage color(20, 8, QImage::Format_ARGB32);
    QImage matte(20, 8, QImage::Format_Grayscale8);
    for (int y = 0; y < 8; ++y) {
        QRgb* crow = reinterpret_cast<QRgb*>(color.scanLine(y));
        uchar* mrow = matte.scanLine(y);
        for (int x = 0; x < 20; ++x) {
            crow[x] = x < 10 ? qRgb(255, 0, 0) : qRgb(0, 0, 255);
            mrow[x] = x < 8 ? 255 : (x > 12 ? 0 : 128);
        }
    }
    const QImage solved =
        selectionMaskMatteSolve(color, matte, QPointF(10, 4), 5.0);
    CHECK(solved.size() == matte.size());
    CHECK(solved.constScanLine(4)[8] > 192);    // red side resolves selected
    CHECK(solved.constScanLine(4)[10] < 64);    // blue side resolves clear
    CHECK(solved.constScanLine(4)[0] == 255);   // far field untouched
    CHECK(solved.constScanLine(4)[19] == 0);
    // No confident context anywhere: returns a usable mask untouched.
    QImage flat(20, 8, QImage::Format_Grayscale8);
    flat.fill(128);
    const QImage fallback =
        selectionMaskMatteSolve(color, flat, QPointF(10, 4), 5.0);
    CHECK(!fallback.isNull());

    // --- painting over the subject never chews up good matte ------------
    // Loose matte: the person blob AND the wall to its right are all
    // selected; only x >= 51 is clear. A stroke on the person's interior
    // has no unselected sample at 1.5x radius — the solve must widen to
    // find the wall, keep the person and clear wrongly-selected wall, and
    // never stamp uncertainty (the old 128 fallback landed at ~143 on the
    // interior instead of keeping it selected).
    QImage scene(64, 48, QImage::Format_ARGB32);
    QImage loose(64, 48, QImage::Format_Grayscale8);
    for (int y = 0; y < 48; ++y) {
        QRgb* srow = reinterpret_cast<QRgb*>(scene.scanLine(y));
        uchar* mrow = loose.scanLine(y);
        for (int x = 0; x < 64; ++x) {
            srow[x] = (x >= 24 && x <= 39) ? qRgb(60, 40, 35)
                                            : qRgb(35, 60, 70);
            mrow[x] = x <= 50 ? 255 : 0;  // overshoots the wall
        }
    }
    const QImage kept =
        selectionMaskMatteSolve(scene, loose, QPointF(36, 24), 6.0);
    CHECK(kept.constScanLine(24)[36] > 192);  // interior stays selected
    const QImage walled =
        selectionMaskMatteSolve(scene, loose, QPointF(44, 24), 6.0);
    CHECK(walled.constScanLine(24)[44] <= 55);  // wrong wall cleared
    CHECK(walled.constScanLine(24)[39] >= 192);  // person kept while clearing

    // --- negative ramp grows the edge softly -----------------------------
    QImage block(12, 12, QImage::Format_Grayscale8);
    block.fill(0);
    for (int y = 4; y < 8; ++y) {
        uchar* row = block.scanLine(y);
        for (int x = 4; x < 8; ++x) row[x] = 255;
    }
    const QRect before = selectionMaskBbox(block);
    const QImage grown = selectionMaskGrow(block, 1);
    const QRect after = selectionMaskBbox(grown);
    CHECK(!before.isEmpty() && !after.isEmpty());
    CHECK(after.adjusted(-1, -1, 1, 1).contains(before));
    CHECK(after.width() > before.width());
}

static void test_new_decontaminated_layer() {
    // Refine output "New decontaminated layer" must behave like Remove
    // Background with decontaminated edges: the refined matte is baked into
    // the new layer's alpha (background actually removed). The "with mask"
    // variant keeps the full pixels and rides the matte on a live mask.
    AppState state;
    DocumentItem* doc = makeDoc(state);
    CHECK(doc != nullptr);
    if (!doc) return;
    QImage cov(8, 4, QImage::Format_Grayscale8);
    for (int y = 0; y < 4; ++y) {
        uchar* row = cov.scanLine(y);
        for (int x = 0; x < 8; ++x)
            row[x] = (x < 4) ? 255 : (x == 4 ? 0 : (x == 5 ? 64 : 0));
    }
    CHECK(state.newDecontaminatedLayer(cov, false));
    CHECK(doc->layers.size() == 2);
    const LayerItem& plain = doc->layers[0];
    CHECK(!plain.hasMask);
    CHECK(plain.pixels != nullptr);
    if (plain.pixels) {
        CHECK(plain.pixels->at(1, 0).a > 0.99f);  // selected: solid
        CHECK(plain.pixels->at(4, 0).a < 0.01f);  // hole: background gone
        const float partial = plain.pixels->at(5, 0).a;
        CHECK(partial > 0.15f && partial < 0.4f);  // soft edge kept (~64/255)
        CHECK(plain.pixels->at(5, 0).r > 0.9f);    // decontaminated color kept
    }
    // The first output became the active layer; lift the mask variant from
    // the ORIGINAL layer, not from the cutout we just made.
    state.setActiveLayerIndex(1);
    CHECK(state.newDecontaminatedLayer(cov, true));
    CHECK(doc->layers.size() == 3);
    const LayerItem& masked = doc->layers[1];
    CHECK(masked.hasMask && masked.mask);
    if (masked.pixels) {
        CHECK(masked.pixels->at(5, 0).a > 0.99f);  // full lift; mask cuts
        CHECK(masked.pixels->at(4, 0).a > 0.99f);  // hole kept, mask hides it
    }
    state.undo();
    CHECK(doc->layers.size() == 2);
}

int main(int argc, char** argv) {
    QCoreApplication app(argc, argv);
    const QString logDir =
        QDir::tempPath() + QStringLiteral("/pittore-selection-ops-log");
    QDir().mkpath(logDir);
    ::pittore::core::log::set_log_dir(logDir.toStdString().c_str());
    test_reveal_hide();
    test_from_transparency();
    test_load_mask();
    test_modify();
    test_refine_ops();
    test_refine_matte_and_ramp();
    test_new_decontaminated_layer();
    const int rc = pittore_test::failures() == 0 ? 0 : 1;
    std::printf("  checks=%d failures=%d\n", pittore_test::checks(),
                pittore_test::failures());
    return rc;
}
