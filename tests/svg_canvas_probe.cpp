// TEMP-PROBE (delete before commit): replicate the canvas paint headless.
// Renders the same view twice - with the tile store on (the shipped path)
// and off (composite + classic walk) - and reports edge energy for both, so
// "the tiles are not crisp" is a number instead of an impression.
#include <QApplication>
#include <QElapsedTimer>
#include <QFile>
#include <QRect>
#include <QThread>

#include <cstdio>

#include "test_util.h"
#include "ui/app_state.h"
#include "ui/canvas/paint/canvas_tile_store.h"
#include "ui/canvas_view.h"
#include "ui/svg_parts.h"

using namespace pittore::ui;

namespace {

// Mean |Laplacian| over the RGB channels: soft bilinear upscale scores low,
// real vector edges score high. Sampled every second pixel to stay cheap.
double edgeEnergy(const QImage& in) {
    const QImage img = in.convertToFormat(QImage::Format_RGB32);
    if (img.width() < 3 || img.height() < 3) return 0.0;
    double sum = 0.0;
    long n = 0;
    for (int y = 1; y < img.height() - 1; y += 2) {
        const QRgb* r = reinterpret_cast<const QRgb*>(img.constScanLine(y));
        const QRgb* a = reinterpret_cast<const QRgb*>(img.constScanLine(y - 1));
        const QRgb* b = reinterpret_cast<const QRgb*>(img.constScanLine(y + 1));
        for (int x = 1; x < img.width() - 1; x += 2) {
            for (int c = 0; c < 3; ++c) {
                const int v = ((r[x - 1] >> (c * 8)) & 255) +
                              ((r[x + 1] >> (c * 8)) & 255) +
                              ((a[x] >> (c * 8)) & 255) +
                              ((b[x] >> (c * 8)) & 255) -
                              4 * ((r[x] >> (c * 8)) & 255);
                sum += v < 0 ? -v : v;
            }
            ++n;
        }
    }
    return n ? sum / (double)n : 0.0;
}

// 4x nearest-neighbour crop so a soft edge is obvious to the eye.
QImage zoomCrop(const QImage& img, int x, int y, int w, int h) {
    const QRect r = QRect(x, y, w, h).intersected(img.rect());
    return img.copy(r).scaled(r.size() * 4, Qt::KeepAspectRatio,
                              Qt::FastTransformation);
}

}  // namespace

int main(int argc, char** argv) {
    QApplication app(argc, argv);
    AppState state;
    const QString path =
        argc > 1 ? QString::fromLocal8Bit(argv[1])
                 : QStringLiteral("/tmp/user-svg.svg");
    QFile f(path);
    if (!f.exists()) {
        std::printf("  SKIP: no %s fixture\n", qPrintable(path));
        return 0;
    }
    CHECK(f.open(QIODevice::ReadOnly));
    SvgImportResult svg;
    int dpi = 96;
    QString error;
    CHECK(svgPartsImport(f.readAll(), &svg, &dpi, &error));
    QString openError;
    CHECK(state.openSvgParts(path, svg, dpi, &openError));
    // Structural check behind the crispness hunt: a layer with no art goes
    // to the composite tier, and crispLayerBox gives art-less layers the
    // whole document, so its composite blit buries every crisp layer below
    // it in the stack. Report what those layers actually are.
    if (DocumentItem* d = state.activeDocument()) {
        const QRectF docBox(0, 0, d->size.width(), d->size.height());
        int artless = 0, artlessFull = 0, topMost = -1;
        double maxW = 0.0, maxH = 0.0;
        for (int i = 0; i < d->layers.size(); ++i) {
            const LayerItem& l = d->layers.at(i);
            if (l.kind != LayerItem::Kind::Pixel || l.art) continue;
            ++artless;
            const QRectF fp = l.pixels
                                  ? QRectF(l.offset,
                                           QSizeF(l.pixels->width() * l.scaleX,
                                                  l.pixels->height() * l.scaleY))
                                  : docBox;
            if (fp.contains(docBox)) ++artlessFull;
            if (fp.width() * fp.height() > maxW * maxH) {
                maxW = fp.width();
                maxH = fp.height();
            }
            if (topMost < 0) topMost = i;  // first hit is the top of the stack
        }
        std::printf("  layers=%lld artless=%d artlessFullBox=%d topIndex=%d "
                    "maxFootprint=%.0fx%.0f doc=%.0fx%.0f\n",
                    static_cast<long long>(d->layers.size()), artless,
                    artlessFull, topMost, maxW,
                    maxH, docBox.width(), docBox.height());
    }
    QWidget window;
    auto* canvas = new CanvasView(&state, &window);
    window.resize(1400, 1000);
    canvas->setGeometry(0, 0, 1400, 1000);
    window.show();

    for (const double z : {1.0, 1.5}) {
        canvas->setZoom(z);
        // Let the bakers finish: each publish repaints, so pumping the loop
        // until the store stops changing settles every visible tile.
        for (int i = 0; i < 600; ++i) {
            app.processEvents();
            QThread::msleep(5);
        }
        app.processEvents();

        CanvasTileStore::instance().setEnabled(true);
        canvas->viewport()->update();
        app.processEvents();
        const QImage with = canvas->viewport()->grab().toImage();
        // Same view with the tile path disabled: composite plus whatever the
        // classic walk draws inside its frame budget.
        CanvasTileStore::instance().setEnabled(false);
        canvas->viewport()->update();
        for (int i = 0; i < 40; ++i) app.processEvents();
        const QImage without = canvas->viewport()->grab().toImage();
        CanvasTileStore::instance().setEnabled(true);
        canvas->viewport()->update();
        for (int i = 0; i < 40; ++i) app.processEvents();

        char path1[64], path2[64];
        std::snprintf(path1, sizeof path1, "/tmp/canvas_on_%d.png",
                      (int)(z * 100));
        std::snprintf(path2, sizeof path2, "/tmp/canvas_off_%d.png",
                      (int)(z * 100));
        with.save(QString::fromLatin1(path1));
        without.save(QString::fromLatin1(path2));
        // A text-bearing crop (upper-left quadrant) at 4x.
        char cropOn[64], cropOff[64];
        std::snprintf(cropOn, sizeof cropOn, "/tmp/crop_on_%d.png",
                      (int)(z * 100));
        std::snprintf(cropOff, sizeof cropOff, "/tmp/crop_off_%d.png",
                      (int)(z * 100));
        zoomCrop(with, 200, 40, 300, 200)
            .save(QString::fromLatin1(cropOn));
        zoomCrop(without, 200, 40, 300, 200)
            .save(QString::fromLatin1(cropOff));
        const double eOn = edgeEnergy(with);
        const double eOff = edgeEnergy(without);
        std::printf("  z=%.2f tiles=ON edge=%.3f  tiles=OFF edge=%.3f  "
                    "gain=%+.1f%%  %s\n",
                    z, eOn, eOff,
                    eOff > 1e-6 ? 100.0 * (eOn - eOff) / eOff : 0.0, path1);
    }
    return 0;
}
