// test_layer_style_ui.cpp — the non-destructive Layer Style path. Effect
// parameters live on the LayerItem and are rendered at composite time, so
// turning an effect on changes the composite without touching the layer's own
// pixels; the dialog's Cancel restores the previous style and its OK lands as
// a single undo step. Runs headless (QT_QPA_PLATFORM=offscreen).
#include <cstdio>

#include <QApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QImage>
#include <QListWidget>
#include <cmath>
#include <cstdlib>

#include "engine/compute/factory.h"
#include "engine/core/log.h"
#include "test_util.h"
#include "ui/app_state.h"
#include "ui/layer_style_dialog.h"

using namespace pittore::ui;
namespace render = pittore::render;

namespace {

QImage solidImage(int w, int h, QRgb c) {
    QImage img(w, h, QImage::Format_ARGB32_Premultiplied);
    img.fill(c);
    return img;
}

bool near(const QColor& c, int r, int g, int b, int tol = 8) {
    return std::abs(c.red() - r) <= tol && std::abs(c.green() - g) <= tol &&
           std::abs(c.blue() - b) <= tol;
}

// The dialog previews on a trailing 80ms throttle: pump the loop until it
// fires so headless assertions observe the previewed style.
void settlePreview(QApplication& app) {
    QElapsedTimer w;
    w.start();
    while (w.elapsed() < 200) app.processEvents(QEventLoop::AllEvents, 10);
}

// Apply a style exactly the way the dialog previews it: write the params on the
// active layer, invalidate the cached styled raster, rebuild the composite.
// The layer is resolved fresh — the layer stack is implicitly shared with undo
// snapshots, so a cached LayerItem* can go stale.
void applyStyle(AppState& state, const render::LayerStyle& s) {
    DocumentItem* d = state.activeDocument();
    LayerItem* l = state.activeLayer();
    if (!d || !l) return;
    l->style = s;
    ++l->sourceStamp;
    l->styledValid = false;
    d->rebuildComposite();
}

int darkestOutside(const DocumentItem& d, const QRect& skip) {
    int minLum = 255;
    for (int y = 0; y < d.size.height(); ++y) {
        for (int x = 0; x < d.size.width(); ++x) {
            if (skip.contains(x, y)) continue;
            const QColor c = d.composite.pixelColor(x, y);
            minLum = std::min(minLum, (c.red() + c.green() + c.blue()) / 3);
        }
    }
    return minLum;
}

}  // namespace

// GPU variant: a styled layer composites through DocumentItem::placedSourceFor
// with the styled raster as the device source, so a placement change must
// re-upload (styledRev) and clearing the style must fall back to the native
// pixels. Skips cleanly when no GPU backend is available.
static void test_layer_style_gpu() {
    bool haveGpu = false;
    for (const auto& dev : pittore::compute::enumerate_devices())
        if (dev.type != pittore::compute::BackendType::CPU) haveGpu = true;
    if (!haveGpu) {
        std::printf("  [layer-style] no GPU device — GPU checks skipped\n");
        return;
    }

    AppState state;
    AppSettings settings = state.settings();
    settings.gpuEnabled = true;
    state.applySettings(settings);
    if (state.computeBackend().type() == pittore::compute::BackendType::CPU) {
        std::printf("  [layer-style] GPU backend unavailable — checks skipped\n");
        return;
    }

    DocumentItem* d = state.addDocument(QStringLiteral("g"), QSize(64, 48), 300);
    CHECK(d != nullptr);
    if (!d) return;
    state.placeImageLayer(solidImage(16, 16, 0xFF00FF00), QStringLiteral("green"),
                          QPointF(32, 24), 2.0);
    CHECK(near(d->composite.pixelColor(32, 24), 0, 255, 0));

    render::LayerStyle overlay;
    overlay.hasColorOverlay = true;
    applyStyle(state, overlay);
    CHECK(near(d->composite.pixelColor(32, 24), 255, 0, 0));

    // Moving the styled layer re-uploads the rebuilt raster.
    CHECK(state.setActiveLayerPlacement(QPointF(16, 40), 2.0, 2.0));
    CHECK(near(d->composite.pixelColor(32, 44), 255, 0, 0));
    CHECK(near(d->composite.pixelColor(32, 16), 242, 242, 242));

    // Clearing the style falls back to the (smaller) native pixels.
    render::LayerStyle none;
    applyStyle(state, none);
    CHECK(near(d->composite.pixelColor(32, 44), 0, 255, 0));
}

int main(int argc, char** argv) {
    QApplication app(argc, argv);
    const QString logDir =
        QDir::tempPath() + QStringLiteral("/pittore-layer-style-log");
    QDir().mkpath(logDir);
    ::pittore::core::log::set_log_dir(logDir.toStdString().c_str());

    AppState state;
    DocumentItem* d = state.addDocument(QStringLiteral("t"), QSize(64, 48), 300);
    CHECK(d != nullptr);
    if (!d) return 1;

    // A green 16x16 layer at the centre, 1:1, over the white background.
    state.placeImageLayer(solidImage(16, 16, 0xFF00FF00), QStringLiteral("green"),
                          QPointF(32, 24), 1.0);
    const QRect layerRect(24, 16, 16, 16);
    CHECK(near(d->composite.pixelColor(32, 24), 0, 255, 0));
    const std::shared_ptr<pittore::Image> native = state.activeLayer()->pixels;

    // --- enabling an effect re-renders at composite time -------------------
    render::LayerStyle overlay;
    overlay.hasColorOverlay = true;   // default overlay is opaque red
    applyStyle(state, overlay);
    CHECK(near(d->composite.pixelColor(32, 24), 255, 0, 0));
    CHECK(state.activeLayer()->pixels == native);   // own pixels untouched
    const LayerDrawSource styled = layerDrawSource(*state.activeLayer());
    CHECK(styled.scaleX == 1.0);                    // document-space, 1:1
    CHECK(std::abs(styled.offset.x() - 24.0) <= 1.0);
    CHECK(std::abs(styled.offset.y() - 16.0) <= 1.0);
    CHECK(styled.img->width() >= 16u);

    // --- clearing the style restores the plain render ----------------------
    render::LayerStyle none;
    applyStyle(state, none);
    CHECK(near(d->composite.pixelColor(32, 24), 0, 255, 0));
    CHECK(state.activeLayer()->styled == nullptr);

    // --- a drop shadow grows the layer's compositing footprint -------------
    CHECK(darkestOutside(*d, layerRect) > 230);     // only the background there
    render::LayerStyle shadow;
    shadow.hasDropShadow = true;                    // black, 75%, size 5
    applyStyle(state, shadow);
    const QRectF grown = layerBounds(*d, *state.activeLayer());
    CHECK(grown.width() > layerRect.width());
    CHECK(grown.height() > layerRect.height());
    CHECK(darkestOutside(*d, layerRect) < 150);     // the shadow darkens the bg

    // Back to the overlay-only style for the dialog checks.
    render::LayerStyle base;
    base.hasColorOverlay = true;
    applyStyle(state, base);
    CHECK(near(d->composite.pixelColor(32, 24), 255, 0, 0));

    // --- dialog: Cancel reverts, OK commits one undo step ------------------
    {
        LayerStyleDialog dlg(&state);
        auto* list = dlg.findChild<QListWidget*>();
        CHECK(list != nullptr);
        if (list) {
            CHECK_EQ(list->count(), 10);
            // Toggle Drop Shadow (row 8); the preview is coalesced through the
            // event loop.
            list->item(8)->setCheckState(Qt::Checked);
            settlePreview(app);
            CHECK(state.activeLayer()->style.hasDropShadow);
            CHECK(state.activeLayer()->style.hasColorOverlay);  // kept
        }
        static_cast<QDialog*>(&dlg)->reject();
        CHECK(!state.activeLayer()->style.hasDropShadow);
        CHECK(state.activeLayer()->style.hasColorOverlay);
        CHECK(near(d->composite.pixelColor(32, 24), 255, 0, 0));
    }

    {
        const int before = state.undoDepth();
        LayerStyleDialog dlg(&state);
        dlg.selectEffect(StyleEffect::DropShadow);
        settlePreview(app);
        CHECK(state.activeLayer()->style.hasDropShadow);
        static_cast<QDialog*>(&dlg)->accept();
        CHECK_EQ(state.undoDepth(), before + 1);
        CHECK(state.undoStepName() == QStringLiteral("Layer Style"));
    }
    CHECK(state.activeLayer()->style.hasDropShadow);
    CHECK(darkestOutside(*d, layerRect) < 150);

    state.undo();
    CHECK(!state.activeLayer()->style.hasDropShadow);
    CHECK(state.activeLayer()->style.hasColorOverlay);   // earlier effect kept
    state.redo();
    CHECK(state.activeLayer()->style.hasDropShadow);

    // --- multi-select + group targeting ----------------------------------
    // Second pixel layer (red, right side) so selections span two rows.
    state.placeImageLayer(solidImage(16, 16, 0xFFFF0000), QStringLiteral("red"),
                          QPointF(48, 24), 1.0);
    DocumentItem* dd = state.activeDocument();
    const int top = 0;  // addLayer inserts at the top
    CHECK((int)dd->layers.size() == 3);
    // No selection: the active layer alone.
    dd->selectedLayers.clear();
    state.setActiveLayerIndex(top);
    CHECK(state.fxTargetLayers() == QVector<int>{top});
    // Multi-select: both pixel rows (background has pixels too — include it
    // to prove filtering keeps pixel rows and drops nothing pixel-less).
    dd->selectedLayers = QVector<int>{0, 1, 2};
    QVector<int> targets = state.fxTargetLayers();
    CHECK(targets.size() == 3);
    CHECK(targets.contains(0) && targets.contains(1) && targets.contains(2));
    // Group the two art rows; selecting the folder targets its subtree.
    dd->selectedLayers = QVector<int>{0, 1};
    const int group = state.groupSelectedLayers();
    CHECK(group >= 0);
    dd = state.activeDocument();
    dd->selectedLayers = QVector<int>{group};
    targets = state.fxTargetLayers();
    CHECK(targets.size() == 2);
    for (int t : targets)
        CHECK(dd->layers[t].kind == LayerItem::Kind::Pixel);  // no headers
    // The dialog applies to every target in one undo step...
    {
        const int before = state.undoDepth();
        LayerStyleDialog dlg(&state);
        CHECK(dlg.windowTitle().contains(QStringLiteral("2 layers")));
        dlg.selectEffect(StyleEffect::DropShadow);
        settlePreview(app);
        for (int t : state.fxTargetLayers())
            CHECK(state.activeDocument()->layers[t].style.hasDropShadow);
        static_cast<QDialog*>(&dlg)->accept();
        CHECK_EQ(state.undoDepth(), before + 1);
        CHECK(state.undoStepName() == QStringLiteral("Layer Style"));
    }
    // ...undo restores each target's own pre-dialog state (red loses the
    // effect; green keeps the drop shadow the earlier single-layer dialog
    // gave it), and Cancel restores mid-session states on every target.
    state.undo();
    {
        DocumentItem* d2 = state.activeDocument();
        auto byName = [&](const QString& name) -> const LayerItem* {
            for (int i = 0; i < d2->layers.size(); ++i)
                if (d2->layers[i].name == name) return &d2->layers[i];
            return nullptr;
        };
        const LayerItem* red = byName(QStringLiteral("red"));
        const LayerItem* green = byName(QStringLiteral("green"));
        CHECK(red != nullptr && green != nullptr);
        if (red) CHECK(!red->style.hasDropShadow);
        if (green) CHECK(green->style.hasDropShadow);  // pre-dialog state
    }
    // Cancel previews on every target, then restores each one.
    {
        QVector<int> ts = state.fxTargetLayers();
        CHECK(ts.size() == 2);
        LayerStyleDialog dlg(&state);
        dlg.selectEffect(StyleEffect::OuterGlow);
        settlePreview(app);
        for (int t : state.fxTargetLayers())
            CHECK(state.activeDocument()->layers[t].style.hasOuterGlow);
        static_cast<QDialog*>(&dlg)->reject();
        for (int t : state.fxTargetLayers())
            CHECK(!state.activeDocument()->layers[t].style.hasOuterGlow);
        // ...but each target's earlier style survives the round trip.
        bool greenShadow = false;
        for (int i = 0; i < state.activeDocument()->layers.size(); ++i)
            if (state.activeDocument()->layers[i].name == QStringLiteral("green"))
                greenShadow =
                    state.activeDocument()->layers[i].style.hasDropShadow;
        CHECK(greenShadow);
    }

    // --- proxy drag previews + full-res settle --------------------------------
    // A 1500px layer exceeds the proxy threshold: the leading drag preview
    // bakes small (fast), the idle settle re-bakes full, and accept lands
    // full-res in one undo step. Smaller layers never leave full res.
    {
        QImage big(1500, 1500, QImage::Format_ARGB32_Premultiplied);
        big.fill(0xFF0080FF);
        state.placeImageLayer(big, QStringLiteral("big"),
                              QPointF(32, 24), 1.0);
        DocumentItem* db = state.activeDocument();
        db->selectedLayers.clear();
        state.setActiveLayerIndex(0);
        LayerStyleDialog dlg(&state);
        dlg.selectEffect(StyleEffect::DropShadow);
        // Leading edge renders synchronously (proxy): small styled raster.
        LayerItem* bigLayer = state.activeLayer();
        CHECK(bigLayer != nullptr && bigLayer->styled != nullptr);
        if (bigLayer && bigLayer->styled)
            CHECK(bigLayer->styled->width() < 1300u);
        // Storm ticks stay proxy; then idle settles back to full res.
        for (int i = 0; i < 5; ++i) {
            dlg.selectEffect(StyleEffect::DropShadow);
            app.processEvents(QEventLoop::AllEvents, 5);
        }
        QElapsedTimer w;
        w.start();
        while (w.elapsed() < 3000) {
            app.processEvents(QEventLoop::AllEvents, 10);
            bigLayer = state.activeLayer();
            if (bigLayer && bigLayer->styled &&
                bigLayer->styled->width() >= 1400u)
                break;
        }
        bigLayer = state.activeLayer();
        CHECK(bigLayer != nullptr && bigLayer->styled != nullptr);
        if (bigLayer && bigLayer->styled)
            CHECK(bigLayer->styled->width() >= 1400u);
        const int before = state.undoDepth();
        static_cast<QDialog*>(&dlg)->accept();
        CHECK_EQ(state.undoDepth(), before + 1);
        bigLayer = state.activeLayer();
        CHECK(bigLayer != nullptr && bigLayer->styled != nullptr);
        if (bigLayer && bigLayer->styled)
            CHECK(bigLayer->styled->width() >= 1400u);
    }

    test_layer_style_gpu();

    // --- proxy/full bake alignment -------------------------------------------
    // The same style baked at proxy and full res must land in the same place:
    // track a hard square edge column in both composites (exact match).
    // Guards the styledOffset-vs-grow rounding the drag previews rely on.
    {
        AppState state2;
        DocumentItem* db = state2.addDocument(QStringLiteral("align"),
                                              QSize(900, 900), 300);
        CHECK(db != nullptr);
        if (db) {
            QImage sq(800, 800, QImage::Format_ARGB32_Premultiplied);
            sq.fill(0xFF000000);
            state2.placeImageLayer(sq, QStringLiteral("sq"),
                                   QPointF(450, 450), 1.0);
            render::LayerStyle shadow;
            shadow.hasDropShadow = true;
            LayerItem* l = state2.activeLayer();
            CHECK(l != nullptr);
            if (l) {
                l->style = shadow;
                l->styledValid = false;
            }
            auto edgeColumn = [&]() {
                DocumentItem* d = state2.activeDocument();
                const int y = 450;
                for (int x = 0; x < d->composite.width(); ++x) {
                    const QColor c = d->composite.pixelColor(x, y);
                    if ((c.red() + c.green() + c.blue()) / 3 < 128) return x;
                }
                return -1;
            };
            setStyledBakeCap(2048.0);
            for (int i = 0; i < db->layers.size(); ++i)
                db->layers[i].styledValid = false;
            db->rebuildComposite();
            const int fullEdge = edgeColumn();
            setStyledBakeCap(512.0);
            for (int i = 0; i < db->layers.size(); ++i)
                db->layers[i].styledValid = false;
            db->rebuildComposite();
            const int proxyEdge = edgeColumn();
            setStyledBakeCap(2048.0);
            for (int i = 0; i < db->layers.size(); ++i)
                db->layers[i].styledValid = false;
            db->rebuildComposite();
            CHECK(fullEdge == 50);
            CHECK(proxyEdge == fullEdge);
        }
    }

    const int rc = pittore_test::failures() == 0 ? 0 : 1;
    std::printf("  checks=%d failures=%d\n", pittore_test::checks(),
                pittore_test::failures());
    return rc;
}
