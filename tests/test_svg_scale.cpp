// test_svg_scale.cpp — the 80k-layer SVG session, distilled: per-layer log
// spam, unbounded diagnostic dumps and group thumbnails must all stay O(1)
// per composite no matter how many layers a document holds.
//
// Runs headless under QCoreApplication (no widgets are constructed). The log
// dir points at a scratch dir; after a cold composite of a 2000-layer
// document the test counts the per-layer lines the session logs showed by
// the hundred thousand ([render][placed], [gpu][upload], [thumb]) and
// requires zero, plus a bounded describeStack dump and a folder-glyph
// fallback for huge group previews. Composite pixels are asserted
// throughout so the gating cannot hide a rendering change.
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QImage>

#include <cstdio>
#include <vector>

#include "engine/core/log.h"
#include "test_util.h"
#include "ui/app_state.h"
#include "ui/project_manager.h"

using namespace pittore::ui;

namespace {

QImage solidImage(int w, int h, QRgb c) {
    QImage img(w, h, QImage::Format_ARGB32_Premultiplied);
    img.fill(c);
    return img;
}

LayerItem makePixel(const char* name, QRgb color, const QPointF& offset) {
    LayerItem l;
    l.name = QLatin1String(name);
    l.kind = LayerItem::Kind::Pixel;
    l.offset = offset;
    l.scaleX = 1.0;
    l.scaleY = 1.0;
    l.opacity = 100;
    l.fill = 100;
    l.pixels = straightRgba64ToImage(solidImage(8, 8, color));
    ++l.sourceStamp;
    return l;
}

// Lines containing `token` across every log file in `dir`.
int countLogLines(const QString& dir, const char* token) {
    std::fflush(nullptr);   // log writes are stdio-buffered; drain first
    int n = 0;
    const QStringList files =
        QDir(dir).entryList(QDir::Files, QDir::Name);
    for (const QString& f : files) {
        QFile in(QDir(dir).filePath(f));
        if (!in.open(QIODevice::ReadOnly)) continue;
        while (!in.atEnd()) {
            const QByteArray line = in.readLine();
            if (line.contains(token)) ++n;
        }
    }
    return n;
}

}  // namespace

// Cold composite of 2000 layers: pixels exact, and no per-layer log traffic.
static void test_scale_no_per_layer_logs(const QString& logDir) {
    AppState state;
    // Exercise the device-upload paths where they exist: request the GPU
    // backend (falls back to CPU with a warning when none is built, in
    // which case the upload counts below pass vacuously).
    {
        AppSettings s = state.settings();
        s.gpuEnabled = true;
        state.applySettings(s);
    }
    std::printf("# device: %s\n",
                state.computeDeviceLabel().toLocal8Bit().constData());
    DocumentItem* d = state.addDocument(QStringLiteral("scale"), QSize(512, 512), 96);
    CHECK(d != nullptr);
    if (!d) return;
    d->layers.clear();
    // Non-overlapping 8px tiles on the 8-grid (64 x 64 slots, 2000 used), so
    // every sample below is unambiguous. Layer 0 sits on top at (0,0).
    for (int i = 0; i < 2000; ++i) {
        const QRgb color = (i % 2 == 0) ? 0xFFFF0000 : 0xFF0000FF;
        const QPointF off((i % 64) * 8, ((i / 64) % 64) * 8);
        d->layers.append(makePixel("L", color, off));
    }
    d->activeLayer = 0;
    d->rebuildComposite();

    // Behaviour is unchanged: top tile red, far empty tile background.
    CHECK_EQ(d->composite.pixelColor(2, 2).red(), 255);
    CHECK_EQ(d->composite.pixelColor(2, 2).blue(), 0);
    CHECK(d->composite.pixelColor(508, 508).red() != 255);

    // The session logs showed these per layer per composite (~250k lines for
    // 80k layers); all are gated now. Upload lines only exist on GPU
    // backends (the CPU upload is a silent no-op), so that count passes
    // vacuously without one and strictly with one.
    CHECK_EQ(countLogLines(logDir, "[render][placed]"), 0);
    CHECK_EQ(countLogLines(logDir, "[gpu][upload]"), 0);
    CHECK_EQ(countLogLines(logDir, "[thumb]"), 0);
}

// Huge groups keep the folder glyph instead of resampling oversampled noise
// at O(box^2 x ops) CPU; small groups still preview combined content.
static void test_scale_group_thumb_fallback() {
    AppState state;
    DocumentItem* d = state.addDocument(QStringLiteral("thumbcap"), QSize(64, 64), 96);
    CHECK(d != nullptr);
    if (!d) return;
    d->layers.clear();
    for (int i = 0; i < 3000; ++i)
        d->layers.append(makePixel("L", 0xFFFF0000, QPointF(16, 16)));
    d->selectedLayers.clear();
    for (int i = 0; i < 3000; ++i) d->selectedLayers.push_back(i);
    const int g = state.groupSelectedLayers();
    CHECK(g >= 0);
    // Past the op cap the preview is null: the panel draws the folder glyph
    // exactly like it does for empty groups.
    CHECK(groupThumbnail(*d, g, 32).isNull());

    // Control: ordinary groups still get a combined preview.
    DocumentItem* e = state.addDocument(QStringLiteral("thumbsmall"), QSize(64, 64), 96);
    CHECK(e != nullptr);
    if (!e) return;
    state.placeImageLayer(solidImage(8, 8, 0xFFFF0000), QStringLiteral("A"),
                          QPointF(16, 16), 1.0);
    state.placeImageLayer(solidImage(8, 8, 0xFF0000FF), QStringLiteral("B"),
                          QPointF(48, 48), 1.0);
    e = state.activeDocument();
    e->selectedLayers = QVector<int>{0, 1};
    const int g2 = state.groupSelectedLayers();
    CHECK(g2 >= 0);
    const QImage t = groupThumbnail(*e, g2, 32);
    CHECK(!t.isNull());
    CHECK_EQ(t.width(), 32);
}

// describeStack stays one line no matter the layer count.
static void test_scale_describe_stack_bounded() {
    std::vector<CompositedLayer> small(3);
    CHECK(describeStack(small) == QStringLiteral("[Normal,Normal,Normal]"));
    std::vector<CompositedLayer> huge(5000);
    const QString s = describeStack(huge);
    CHECK(s.size() < 2048);
    CHECK(s.startsWith(QLatin1Char('[')));
    CHECK(s.contains(QStringLiteral("more")));
}

int main(int argc, char** argv) {
    QCoreApplication app(argc, argv);
    const QString logDir = QDir::tempPath() + QStringLiteral("/pittore-svg-scale-log");
    QDir(logDir).removeRecursively();
    QDir().mkpath(logDir);
    ::pittore::core::log::set_log_dir(logDir.toStdString().c_str());
    test_scale_no_per_layer_logs(logDir);
    test_scale_group_thumb_fallback();
    test_scale_describe_stack_bounded();
    const int rc = pittore_test::failures() == 0 ? 0 : 1;
    std::printf("  checks=%d failures=%d\n", pittore_test::checks(),
                pittore_test::failures());
    return rc;
}
