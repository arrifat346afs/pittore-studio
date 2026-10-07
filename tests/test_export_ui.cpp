// test_export_ui.cpp — the Export As dialog and the image it produces. The
// dialog only collects ExportSettings; writeExportImage() applies the size,
// resample mode, DPI and format to the document composite. Runs headless
// (QT_QPA_PLATFORM=offscreen).
#include <cstdio>
#include <cstdlib>

#include <QApplication>
#include <QCheckBox>
#include <QColorSpace>
#include <QComboBox>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QImage>
#include <QImageReader>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QListWidget>
#include <QSlider>
#include <QSpinBox>
#include <QTemporaryDir>

#include <cmath>

#include "engine/core/log.h"
#include "test_util.h"
#include "ui/app_state.h"
#include "ui/export_dialog.h"
#include "ui/settings.h"
#include "ui/svg_parts.h"

using namespace pittore::ui;

namespace {

QImage solidImage(int w, int h, QRgb c) {
    QImage img(w, h, QImage::Format_ARGB32_Premultiplied);
    img.fill(c);
    return img;
}

// Document-space bounds of a retained art node's control points, using the
// layer's placement (offset + the node's own matrix), for round-trip checks.
QRectF artBounds(const pittore::vector::ArtNode& n, const QPointF& offset) {
    const double* m = n.matrix;
    auto map = [&](double x, double y) {
        return QPointF(m[0] * x + m[2] * y + m[4],
                       m[1] * x + m[3] * y + m[5]) +
               offset;
    };
    double minX = 1e300, minY = 1e300, maxX = -1e300, maxY = -1e300;
    auto add = [&](const QPointF& p) {
        minX = std::min(minX, p.x());
        minY = std::min(minY, p.y());
        maxX = std::max(maxX, p.x());
        maxY = std::max(maxY, p.y());
    };
    for (const auto& s : n.segments) {
        if (s.kind == pittore::vector::Segment::Kind::Close) continue;
        add(map(s.x, s.y));
        if (s.kind == pittore::vector::Segment::Kind::CubicTo) {
            add(map(s.c1x, s.c1y));
            add(map(s.c2x, s.c2y));
        }
    }
    if (minX > maxX) return QRectF();
    return QRectF(QPointF(minX, minY), QPointF(maxX, maxY));
}

}  // namespace

int main(int argc, char** argv) {
    // Never touch the user's real config/presets on manual runs (meson
    // isolates via env; see test_color_policy for why this matters).
    if (qEnvironmentVariableIsEmpty("XDG_CONFIG_HOME")) {
        static QTemporaryDir* scratch = new QTemporaryDir;
        if (scratch->isValid())
            qputenv("XDG_CONFIG_HOME", scratch->path().toLocal8Bit());
    }
    QApplication app(argc, argv);
    const QString logDir = QDir::tempPath() + QStringLiteral("/pittore-export-log");
    QDir().mkpath(logDir);
    ::pittore::core::log::set_log_dir(logDir.toStdString().c_str());

    AppState state;
    DocumentItem* doc = state.addDocument(QStringLiteral("poster"), QSize(200, 100), 144);
    CHECK(doc != nullptr);
    if (!doc) return 1;
    doc->profile = QStringLiteral("sRGB IEC61966-2.1");
    // A second pixel layer, so the target list has the background + this one.
    state.placeImageLayer(solidImage(40, 40, 0xFF3366CC), QStringLiteral("badge"),
                          QPointF(100, 50), 1.0);
    CHECK(doc->layers.size() >= 2);

    // --- the dialog seeds from the document --------------------------------
    {
        ExportDialog dialog(&state, doc);
        auto* format = dialog.findChild<QComboBox*>(QStringLiteral("export.format"));
        auto* width = dialog.findChild<QSpinBox*>(QStringLiteral("export.width"));
        auto* height = dialog.findChild<QSpinBox*>(QStringLiteral("export.height"));
        auto* dpi = dialog.findChild<QSpinBox*>(QStringLiteral("export.dpi"));
        auto* quality = dialog.findChild<QSlider*>(QStringLiteral("export.quality"));
        auto* targets = dialog.findChild<QListWidget*>(QStringLiteral("export.targets"));
        auto* colour = dialog.findChild<QComboBox*>(QStringLiteral("export.colorSpace"));
        CHECK(format && width && height && dpi && quality && targets && colour);

        CHECK(dialog.settings().pixelSize == QSize(200, 100));
        CHECK(dialog.settings().dpi == 144);
        CHECK(width->value() == 200 && height->value() == 100);
        CHECK(dpi->value() == 144);
        CHECK(colour->currentText() == QStringLiteral("sRGB IEC61966-2.1"));
        // The built-in codecs are always offered, whatever Qt's plugins provide.
        CHECK(format->findData(QStringLiteral("png")) >= 0);
        CHECK(format->findData(QStringLiteral("jpg")) >= 0);
        CHECK(format->findData(QStringLiteral("psd")) >= 0);
        CHECK(format->findData(QStringLiteral("xcf")) >= 0);
        // Whole canvas + every pixel layer (background + badge).
        CHECK(targets->count() == 1 + doc->layers.size());

        // Aspect lock: typing a width drives the height.
        width->setValue(100);
        CHECK_EQ(height->value(), 50);

        // Format switch gates the quality row and the format-specific options.
        auto* qualityRow = dialog.findChild<QWidget*>(QStringLiteral("export.qualityRow"));
        auto* dither = dialog.findChild<QCheckBox*>(QStringLiteral("export.dither"));
        auto* interlace = dialog.findChild<QCheckBox*>(QStringLiteral("export.interlace"));
        auto* embed = dialog.findChild<QCheckBox*>(QStringLiteral("export.embedIcc"));
        CHECK(qualityRow && dither && interlace && embed);
        format->setCurrentIndex(format->findData(QStringLiteral("png")));
        CHECK(qualityRow->isHidden());   // PNG has no quality knob
        CHECK(!interlace->isEnabled());  // only JPEG does progressive here
        CHECK(dpi->isEnabled());         // PNG records DPI + ICC
        CHECK(embed->isEnabled());
        format->setCurrentIndex(format->findData(QStringLiteral("jpg")));
        CHECK(!qualityRow->isHidden());
        CHECK(interlace->isEnabled());   // JPEG is progressive-capable
        const int webpIndex = format->findData(QStringLiteral("webp"));
        if (webpIndex >= 0) {
            format->setCurrentIndex(webpIndex);
            CHECK(!qualityRow->isHidden());   // WebP has quality...
            CHECK(!dpi->isEnabled());         // ...but no DPI or ICC
            CHECK(!embed->isEnabled());
            format->setCurrentIndex(format->findData(QStringLiteral("jpg")));
        }
        // SVG: real vector output, so quality/resample/colour are all moot and
        // the controls say so rather than silently doing nothing.
        const int svgIndex = format->findData(QStringLiteral("svg"));
        CHECK(svgIndex >= 0);
        if (svgIndex >= 0) {
            format->setCurrentIndex(svgIndex);
            auto* resample = dialog.findChild<QComboBox*>(QStringLiteral("export.resample"));
            CHECK(resample != nullptr);
            CHECK(resample && !resample->isEnabled());
            CHECK(!colour->isEnabled());
            CHECK(qualityRow->isHidden());
            CHECK(!dpi->isEnabled());
            CHECK(!embed->isEnabled());
            format->setCurrentIndex(format->findData(QStringLiteral("jpg")));
        }
        quality->setValue(80);
        // Dither is disabled: the 8-bit pipeline has nothing to quantise.
        CHECK(!dither->isEnabled());

        // Selecting a layer re-seeds the size fields to that layer's own bounds
        // (the badge is 40x40). layers[0] is the topmost layer, so its row is 1.
        CHECK(doc->layers[0].name == QStringLiteral("badge"));
        targets->setCurrentRow(1);
        CHECK_EQ(width->value(), 40);
        CHECK_EQ(height->value(), 40);
        dialog.accept();
        const ExportSettings s = dialog.settings();
        CHECK(s.format == QStringLiteral("jpg"));
        CHECK(s.quality == 80);
        CHECK(s.pixelSize == QSize(40, 40));
        CHECK(s.exportTarget == 0);
        CHECK(s.dpi == 144);
    }

    // --- a saved preset is loaded from disk and applied when selected -------
    {
        QJsonObject preset;
        preset.insert(QStringLiteral("name"), QStringLiteral("MyTestPreset"));
        preset.insert(QStringLiteral("format"), QStringLiteral("jpg"));
        preset.insert(QStringLiteral("quality"), 55);
        preset.insert(QStringLiteral("dither"), false);
        preset.insert(QStringLiteral("interlace"), false);
        preset.insert(QStringLiteral("dpi"), 200);
        preset.insert(QStringLiteral("resample"), 2);
        preset.insert(QStringLiteral("colorProfile"), QStringLiteral("Adobe RGB (1998)"));
        preset.insert(QStringLiteral("embedIcc"), true);
        QJsonArray array;
        array.append(preset);
        QFile file(QFileInfo(settingsPath()).absolutePath() +
                   QStringLiteral("/export_presets.json"));
        CHECK(file.open(QIODevice::WriteOnly));
        file.write(QJsonDocument(array).toJson());
        file.close();

        ExportDialog dialog(&state, doc);
        auto* combo = dialog.findChild<QComboBox*>(QStringLiteral("export.preset"));
        auto* format = dialog.findChild<QComboBox*>(QStringLiteral("export.format"));
        auto* dpi = dialog.findChild<QSpinBox*>(QStringLiteral("export.dpi"));
        auto* quality = dialog.findChild<QSlider*>(QStringLiteral("export.quality"));
        auto* colour = dialog.findChild<QComboBox*>(QStringLiteral("export.colorSpace"));
        auto* resample = dialog.findChild<QComboBox*>(QStringLiteral("export.resample"));
        CHECK(combo && format && dpi && quality && colour && resample);
        CHECK(combo->findText(QStringLiteral("MyTestPreset")) >= 0);
        if (combo->findText(QStringLiteral("MyTestPreset")) >= 0) {
            combo->setCurrentIndex(combo->findText(QStringLiteral("MyTestPreset")));
            CHECK(format->currentData().toString() == QStringLiteral("jpg"));
            CHECK_EQ(dpi->value(), 200);
            CHECK_EQ(quality->value(), 55);
            CHECK_EQ(resample->currentIndex(), 2);
            CHECK(colour->currentText() == QStringLiteral("Adobe RGB (1998)"));
        }
    }

    // --- writeExportImage: size, resample modes and DPI ---------------------
    QTemporaryDir dir;
    CHECK(dir.isValid());
    if (!dir.isValid()) return 1;
    QString error;

    ExportSettings s;
    s.format = QStringLiteral("png");
    s.pixelSize = QSize(50, 25);
    s.dpi = 300;
    s.resampleMode = 0;   // smooth
    CHECK(writeExportImage(doc->composite, QRect(), s, dir.filePath("smooth.png"), &error));
    QImage smooth(dir.filePath("smooth.png"));
    CHECK(!smooth.isNull());
    CHECK(smooth.size() == QSize(50, 25));
    // PNG stores the DPI (pHYs); allow for the round-to-int dots-per-metre.
    CHECK_NEAR(smooth.dotsPerMeterX(), qRound(300.0 / 0.0254), 2.0);

    s.resampleMode = 1;   // Lanczos
    CHECK(writeExportImage(doc->composite, QRect(), s, dir.filePath("lanczos.png"), &error));
    CHECK(QImage(dir.filePath("lanczos.png")).size() == QSize(50, 25));

    s.resampleMode = 2;   // nearest
    CHECK(writeExportImage(doc->composite, QRect(), s, dir.filePath("nearest.png"), &error));
    CHECK(QImage(dir.filePath("nearest.png")).size() == QSize(50, 25));

    // Crop: only the requested rect is written when the size matches it.
    ExportSettings cropped = s;
    cropped.pixelSize = QSize(20, 15);
    CHECK(writeExportImage(doc->composite, QRect(10, 10, 20, 15), cropped,
                           dir.filePath("crop.png"), &error));
    CHECK(QImage(dir.filePath("crop.png")).size() == QSize(20, 15));

    // An empty result is a clean failure, not a crash.
    CHECK(!writeExportImage(QImage(), QRect(), s, dir.filePath("empty.png"), &error));
    CHECK(!error.isEmpty());

    // --- colour space: convert + embed, and "Don't Convert" strips ----------
    ExportSettings adobe = s;
    adobe.pixelSize = QSize(200, 100);   // 1:1 so only colour can differ
    adobe.colorProfile = QStringLiteral("Adobe RGB (1998)");
    adobe.embedIccProfile = true;
    CHECK(writeExportImage(doc->composite, QRect(), adobe, dir.filePath("adobe.png"), &error));
    const QImage adobeOut(dir.filePath("adobe.png"));
    CHECK(adobeOut.colorSpace().isValid());
    CHECK(adobeOut.colorSpace() != QColorSpace::SRgb);

    ExportSettings unchanged = adobe;
    unchanged.colorProfile = QStringLiteral("Don't Convert");
    CHECK(writeExportImage(doc->composite, QRect(), unchanged, dir.filePath("plain.png"), &error));
    CHECK(!QImage(dir.filePath("plain.png")).colorSpace().isValid());

    // Conversion must change the pixels, not just attach a different tag.
    ExportSettings srgb = adobe;
    srgb.colorProfile = QStringLiteral("sRGB IEC61966-2.1");
    CHECK(writeExportImage(doc->composite, QRect(), srgb, dir.filePath("srgb.png"), &error));
    const QImage srgbOut(dir.filePath("srgb.png"));
    CHECK(srgbOut.size() == QSize(200, 100));
    CHECK(srgbOut.pixel(100, 50) != adobeOut.pixel(100, 50));

    // --- lossy quality actually reaches the encoder ------------------------
    ExportSettings high = s;
    high.format = QStringLiteral("jpg");
    high.quality = 95;
    high.resampleMode = 0;
    CHECK(writeExportImage(doc->composite, QRect(), high, dir.filePath("q95.jpg"), &error));
    ExportSettings low = high;
    low.quality = 20;
    CHECK(writeExportImage(doc->composite, QRect(), low, dir.filePath("q20.jpg"), &error));
    const qint64 highBytes = QFileInfo(dir.filePath("q95.jpg")).size();
    const qint64 lowBytes = QFileInfo(dir.filePath("q20.jpg")).size();
    CHECK(highBytes > 0 && lowBytes > 0);
    CHECK(lowBytes < highBytes);

    // Progressive really changes the JPEG stream: SOF2 (0xFFC2) rather than the
    // baseline SOF0 (0xFFC0).
    ExportSettings progressive = high;
    progressive.interlace = true;
    CHECK(writeExportImage(doc->composite, QRect(), progressive,
                           dir.filePath("prog.jpg"), &error));
    ExportSettings baseline = high;
    baseline.interlace = false;
    CHECK(writeExportImage(doc->composite, QRect(), baseline,
                           dir.filePath("base.jpg"), &error));
    auto hasMarker = [](const QString& path, quint8 marker) {
        QFile f(path);
        if (!f.open(QIODevice::ReadOnly)) return false;
        const QByteArray bytes = f.readAll();
        for (int i = 0; i + 1 < bytes.size(); ++i)
            if (quint8(bytes[i]) == 0xFF && quint8(bytes[i + 1]) == marker) return true;
        return false;
    };
    CHECK(hasMarker(dir.filePath("prog.jpg"), 0xC2));
    CHECK(hasMarker(dir.filePath("base.jpg"), 0xC0));

    // WebP goes through the built-in encoder, so the quality slider selects
    // lossy VP8 (100 stays lossless). Qt may not have a WebP reader, so verify
    // the RIFF/WEBP container rather than decoding it back.
    ExportSettings webp = s;
    webp.format = QStringLiteral("webp");
    webp.pixelSize = QSize(64, 32);
    webp.resampleMode = 0;
    webp.quality = 100;
    if (writeExportImage(doc->composite, QRect(), webp, dir.filePath("lossless.webp"),
                         &error)) {
        QFile lossless(dir.filePath("lossless.webp"));
        CHECK(lossless.open(QIODevice::ReadOnly));
        CHECK(lossless.read(4) == QByteArrayLiteral("RIFF"));
        CHECK(lossless.read(8).mid(4) == QByteArrayLiteral("WEBP"));
        webp.quality = 20;
        CHECK(writeExportImage(doc->composite, QRect(), webp, dir.filePath("lossy.webp"),
                               &error));
        CHECK(QFileInfo(dir.filePath("lossy.webp")).size() > 0);
    }

    // --- default file name carries the scale suffix ------------------------
    ExportSettings named;
    named.scaleSuffix = QStringLiteral("@2x");
    CHECK(exportBaseName(*doc, named) == QStringLiteral("poster@2x"));
    named.scaleSuffix.clear();
    CHECK(exportBaseName(*doc, named) == QStringLiteral("poster"));
    doc->filePath = QStringLiteral("/tmp/some project.ifp");
    CHECK(exportBaseName(*doc, named) == QStringLiteral("some project"));

    // --- SVG export embeds raster layers as PNG <image> --------------------
    {
        ExportSettings vs;
        vs.format = QStringLiteral("svg");
        vs.exportTarget = -1;
        vs.pixelSize = QSize(200, 100);
        const QString path = dir.filePath(QStringLiteral("raster.svg"));
        CHECK(writeExportVector(*doc, vs, path, &error));
        QFile f(path);
        CHECK(f.open(QIODevice::ReadOnly));
        const QByteArray out = f.readAll();
        CHECK(out.contains("<svg "));
        CHECK(out.contains("<image "));
        CHECK(out.contains("data:image/png;base64,"));
        CHECK(out.contains("viewBox=\"0 0 200 100\""));
        CHECK(out.contains("</svg>"));
    }

    // --- SVG export keeps real vector geometry through a round trip --------
    {
        const QByteArray svgSource = QByteArrayLiteral(
            "<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"120\" height=\"80\" "
            "viewBox=\"0 0 120 80\">\n"
            "  <defs>\n"
            "    <linearGradient id=\"g\" x1=\"0\" y1=\"0\" x2=\"1\" y2=\"1\">\n"
            "      <stop offset=\"0\" stop-color=\"#ff0000\"/>\n"
            "      <stop offset=\"1\" stop-color=\"#0000ff\"/>\n"
            "    </linearGradient>\n"
            "  </defs>\n"
            "  <rect x=\"10\" y=\"10\" width=\"50\" height=\"40\" fill=\"url(#g)\"/>\n"
            "  <circle cx=\"85\" cy=\"40\" r=\"20\" fill=\"#cc3366\" "
            "stroke=\"#222222\" stroke-width=\"3\"/>\n"
            "</svg>");
        const QString inPath = dir.filePath(QStringLiteral("vec_in.svg"));
        QFile inFile(inPath);
        CHECK(inFile.open(QIODevice::WriteOnly));
        inFile.write(svgSource);
        inFile.close();

        QString importError;
        CHECK(state.openImageFile(inPath, &importError));
        DocumentItem* vdoc = state.activeDocument();
        CHECK(vdoc != nullptr);
        if (vdoc) {
            // The importer retained true geometry for both shapes.
            int withArt = 0;
            for (const LayerItem& l : vdoc->layers)
                if (l.art && !l.art->segments.empty()) ++withArt;
            CHECK(withArt >= 2);

            ExportSettings vs;
            vs.format = QStringLiteral("svg");
            vs.exportTarget = -1;
            vs.pixelSize = QSize(120, 80);
            const QString outPath = dir.filePath(QStringLiteral("vec_out.svg"));
            QString ve;
            CHECK(writeExportVector(*vdoc, vs, outPath, &ve));

            QFile outFile(outPath);
            CHECK(outFile.open(QIODevice::ReadOnly));
            const QByteArray out = outFile.readAll();
            CHECK(out.contains("<path "));
            CHECK(out.contains("d=\"M"));
            CHECK(out.contains("<linearGradient"));
            CHECK(out.contains("fill=\"url(#grad0)\""));
            CHECK(out.contains("viewBox=\"0 0 120 80\""));
            CHECK(out.contains("width=\"120\" height=\"80\""));
            CHECK(out.contains("stroke=\"#222222\""));
            CHECK(!out.contains("<image "));   // pure vector: no bitmap fallback

            // Re-importing the export yields vector geometry again.
            SvgImportResult reimport;
            int redpi = 96;
            QString rerr;
            CHECK(svgPartsImport(out, &reimport, &redpi, &rerr));
            CHECK(reimport.docSize == QSize(120, 80));
            int artAgain = 0;
            for (const SvgPartLayer& p : reimport.layers)
                if (p.art && !p.art->segments.empty()) ++artAgain;
            CHECK(artAgain >= 2);
            // Coordinates survive the trip: the union of both shapes lands back
            // on the source rect and circle bounds.
            QRectF rb;
            for (const SvgPartLayer& p : reimport.layers)
                if (p.art) rb |= artBounds(*p.art, p.offset);
            CHECK_NEAR(rb.left(), 10.0, 1.5);
            CHECK_NEAR(rb.top(), 10.0, 1.5);
            CHECK_NEAR(rb.right(), 105.0, 1.5);
            CHECK_NEAR(rb.bottom(), 60.0, 1.5);
        }
    }

    // --- a native .af shape reaches the writer as real vector --------------
    // Fixture-gated: PITTORE_AF_SHAPE_FIXTURE points at a .af file with a
    // vector shape layer. No fixture path is baked into the source.
    {
        const char* env = std::getenv("PITTORE_AF_SHAPE_FIXTURE");
        const QString afPath = (env && *env) ? QString::fromLocal8Bit(env) : QString();
        if (!afPath.isEmpty() && QFileInfo::exists(afPath)) {
            QString importError;
            CHECK(state.openImageFile(afPath, &importError));
            DocumentItem* adoc = state.activeDocument();
            CHECK(adoc != nullptr);
            if (adoc) {
                // openAfLayers() must hand the decoder's geometry to the layer.
                int artIndex = -1;
                for (int i = 0; i < adoc->layers.size(); ++i) {
                    const LayerItem& l = adoc->layers[i];
                    if (l.kind != LayerItem::Kind::Group && l.art &&
                        !l.art->segments.empty()) {
                        artIndex = i;
                        break;
                    }
                }
                CHECK(artIndex >= 0);

                if (artIndex >= 0) {
                    ExportSettings vs;
                    vs.format = QStringLiteral("svg");
                    vs.exportTarget = artIndex;   // solo the shape layer
                    vs.pixelSize = adoc->size;
                    const QString outPath = dir.filePath(QStringLiteral("shape_out.svg"));
                    QString ve;
                    CHECK(writeExportVector(*adoc, vs, outPath, &ve));
                    QFile f(outPath);
                    CHECK(f.open(QIODevice::ReadOnly));
                    const QByteArray svg = f.readAll();
                    CHECK(svg.contains("<path "));
                    CHECK(!svg.contains("<image "));   // a shape, not a bake
                    CHECK(svg.contains("fill=\"#"));
                }
            }
        }
    }

    std::printf("  checks=%d failures=%d\n", pittore_test::checks(),
                pittore_test::failures());
    return pittore_test::failures() == 0 ? 0 : 1;
}
