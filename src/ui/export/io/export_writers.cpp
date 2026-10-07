#include "ui/export_dialog.h"

#include <QCheckBox>
#include <QColorSpace>
#include <QComboBox>
#include <QBuffer>
#include <QDialogButtonBox>
#include <QFile>
#include <QFileInfo>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QImageWriter>
#include <QInputDialog>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QPainter>
#include <QPushButton>
#include <QSaveFile>
#include <QScrollArea>
#include <QSlider>
#include <QSpinBox>
#include <QTransform>
#include <QVBoxLayout>

#include <algorithm>
#include <cmath>

#include "ui/app_state.h"
#include "ui/project_manager.h"
#include "ui/settings.h"
#include "ui/theme.h"
#include "ui/export/shared/export_helpers.h"
#include "engine/vector/svg_exchange.h"
#ifdef PITTORE_TIFF
#include "engine/io/tiff.h"
#endif

namespace pittore::ui {

using namespace export_detail;


QString exportBaseName(const DocumentItem& doc, const ExportSettings& settings) {
    const QString base = doc.filePath.isEmpty()
                             ? doc.title
                             : QFileInfo(doc.filePath).completeBaseName();
    return base + settings.scaleSuffix;
}


namespace {

// Encode img to bytes with a Qt image format (no metadata text).
bool encodeQtImageToBuffer(const QImage& img, const QByteArray& fmt,
                           QByteArray& out) {
    out.clear();
    QBuffer buffer(&out);
    if (!buffer.open(QIODevice::WriteOnly)) return false;
    QImageWriter writer(&buffer, fmt);
    if (!writer.write(img)) return false;
    buffer.close();
    return true;
}

// Fit a palettized export under maxBytes at FULL resolution: step the palette
// down until the encoded file fits. Returns the smallest palette tried when
// nothing fits (the caller still exports it and reports the miss); outColors
// receives the palette size used.
QImage fitPaletteToSize(const QImage& src, int maxColors, bool dither,
                        bool gray, qint64 maxBytes, const QString& fmt,
                        int* outColors, qint64* outBytes) {
    const QByteArray wfmt =
        (fmt == QLatin1String("jpg")) ? QByteArrayLiteral("jpeg")
                                      : fmt.toLatin1();
    static const int kCounts[] = {256, 192, 128, 96, 64, 48,
                                  32,  24,  16,  12, 8,  6,
                                  4,   2};
    QImage smallest;
    int smallestCount = 2;
    qint64 smallestBytes = -1;
    for (int c : kCounts) {
        if (c > maxColors) continue;
        QImage q = quantizeIndexed(src, c, dither, gray);
        if (q.isNull()) continue;
        QByteArray bytes;
        if (!encodeQtImageToBuffer(q, wfmt, bytes)) continue;
        smallest = q;
        smallestCount = c;
        smallestBytes = qint64(bytes.size());
        if (smallestBytes <= maxBytes) {
            if (outColors) *outColors = c;
            if (outBytes) *outBytes = smallestBytes;
            return q;
        }
    }
    if (outColors) *outColors = smallestCount;
    if (outBytes) *outBytes = smallestBytes;
    if (!smallest.isNull()) return smallest;
    return quantizeIndexed(src, 2, dither, gray);
}

// PNG with the max-lossless pass: encode to memory, recompress (adaptive
// filtering + zlib-9 + gray-fold + sub-byte packing), then commit to disk.
// Same pixels as QImageWriter would write, smaller file.
bool writeOptimizedPng(const QImage& img, const QString& path,
                       const ExportSettings& settings, QString* error) {
    QByteArray raw;
    {
        QBuffer buffer(&raw);
        if (!buffer.open(QIODevice::WriteOnly)) {
            if (error) *error = QObject::tr("Could not encode PNG.");
            return false;
        }
        QImageWriter writer(&buffer, "png");
        if (settings.embedMetadata) {
            writer.setText(QStringLiteral("Software"),
                           QStringLiteral("Pittore Studio"));
            if (settings.dpi > 0)
                writer.setText(QStringLiteral("Resolution"),
                               QStringLiteral("%1 ppi").arg(settings.dpi));
        }
        if (!writer.write(img)) {
            if (error)
                *error = writer.errorString().isEmpty()
                             ? QObject::tr("Could not encode PNG.")
                             : writer.errorString();
            return false;
        }
        buffer.close();
    }
    QByteArray final = optimizePngLossless(raw);
    if (final.isEmpty()) final = raw;
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly)) {
        if (error) *error = QObject::tr("Cannot write %1").arg(path);
        return false;
    }
    file.write(final);
    if (!file.commit()) {
        if (error) *error = QObject::tr("Could not save %1").arg(path);
        return false;
    }
    return true;
}

}  // namespace


bool writeExportImage(const QImage& source, const QRect& crop,
                      const ExportSettings& settings, const QString& path,
                      QString* error, QString* report) {
    if (source.isNull()) {
        if (error) *error = QObject::tr("Nothing to export (empty image).");
        return false;
    }

    QImage img = source;
    if (!crop.isEmpty()) {
        const QRect clipped = crop.intersected(QRect(QPoint(0, 0), source.size()));
        if (clipped.isEmpty()) {
            if (error) *error = QObject::tr("The export area is empty.");
            return false;
        }
        if (clipped != QRect(QPoint(0, 0), source.size())) img = source.copy(clipped);
    }

    QString fmt = settings.format.toLower();
    if (fmt == QLatin1String("jpeg")) fmt = QStringLiteral("jpg");
    if (fmt == QLatin1String("tif")) fmt = QStringLiteral("tiff");
    if (fmt == QLatin1String("jfif")) fmt = QStringLiteral("jpg");

    QSize target = settings.pixelSize;
    if (!target.isValid() || target.isEmpty()) target = img.size();
    if (target != img.size()) {
        switch (settings.resampleMode) {
            case 2:
                img = img.scaled(target, Qt::IgnoreAspectRatio, Qt::FastTransformation);
                break;
            case 1:
                img = lanczosResample(img, target);
                break;
            default:
                // 0 = bicubic/smooth, 3 = bilinear: Qt's smooth scaler covers
                // both at export sizes.
                img = img.scaled(target, Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
                break;
        }
        if (img.isNull()) {
            if (error) *error = QObject::tr("Could not resample the image.");
            return false;
        }
    }

    if (settings.dpi > 0) {
        const double dotsPerMeter = settings.dpi / 0.0254;
        img.setDotsPerMeterX(qRound(dotsPerMeter));
        img.setDotsPerMeterY(qRound(dotsPerMeter));
    }

    const bool isHdrPng =
        fmt == QLatin1String("png") && settings.transferFunction != 0;
    const bool isTiff =
        fmt == QLatin1String("tiff") || fmt == QLatin1String("tif");

    // HDR stills bypass the SDR pipeline: 16-bit Rec.2020 codes plus a cICP
    // chunk, written directly (Qt's writer cannot tag transfer functions).
    if (isHdrPng) {
        const bool bt2020 = settings.primaries == 1;
        QImage hdr = encodeHdrCodes(img, settings.transferFunction,
                                    settings.fullRange, bt2020);
        if (hdr.isNull()) {
            if (error) *error = QObject::tr("Could not encode the HDR image.");
            return false;
        }
        const QByteArray png =
            encodePngWithCIcp(hdr, settings.transferFunction,
                              settings.fullRange, bt2020);
        if (png.isEmpty()) {
            if (error) *error = QObject::tr("Could not encode PNG.");
            return false;
        }
        // Max-lossless pass also applies to HDR (chunk-preserving).
        const QByteArray final = optimizePngLossless(png);
        QSaveFile file(path);
        if (!file.open(QIODevice::WriteOnly)) {
            if (error) *error = QObject::tr("Cannot write %1").arg(path);
            return false;
        }
        file.write(final.isEmpty() ? png : final);
        if (!file.commit()) {
            if (error) *error = QObject::tr("Could not save %1").arg(path);
            return false;
        }
        return true;
    }

    // Colour: assume sRGB when the composite carries no profile, convert to the
    // chosen space, then either keep the tag (embed) or strip it. "Don't
    // Convert" leaves the pixels and removes any profile.
    const QColorSpace targetSpace = colorSpaceFor(settings.colorProfile);
    if (targetSpace.isValid()) {
        QImage conv = img;
        if (!conv.colorSpace().isValid()) conv.setColorSpace(QColorSpace::SRgb);
        img = conv.convertedToColorSpace(targetSpace);
        if (!settings.embedIccProfile) img.setColorSpace(QColorSpace());
    } else {
        img.setColorSpace(QColorSpace());
    }

    // Matte + pixel format. JPEG has no alpha channel, so it always flattens
    // (matte color, else white). Palettized output keeps its alpha for the
    // quantizer, which reserves a transparent entry itself.
    const QColor matte =
        settings.matteEnabled && settings.matte.isValid()
            ? settings.matte
            : QColor(255, 255, 255);
    const bool palettized =
        settings.palettized &&
        (fmt == QLatin1String("png") || fmt == QLatin1String("gif"));
    if (fmt == QLatin1String("jpg")) {
        img = flattenOverMatte(img, matte);
    } else if (!palettized &&
               settings.pixelFormat != QLatin1String("doc")) {
        img = convertPixelFormat(img, settings.pixelFormat,
                                 settings.matteEnabled ? settings.matte
                                                       : QColor());
        if (img.isNull()) {
            if (error) *error = QObject::tr("Could not convert the pixel format.");
            return false;
        }
    }

    if (palettized) {
        const bool gray =
            settings.pixelFormat == QLatin1String("gray8") ||
            settings.pixelFormat == QLatin1String("gray16");
        if (settings.limitFileSize && settings.maxFileSizeMB > 0.0) {
            // Full resolution is sacred: step the palette down until the
            // encoded file fits, never the dimensions.
            const qint64 maxBytes = qint64(settings.maxFileSizeMB * 1024.0 *
                                           1024.0);
            int usedColors = settings.paletteColors;
            qint64 usedBytes = -1;
            img = fitPaletteToSize(img, settings.paletteColors,
                                   settings.paletteDither, gray, maxBytes, fmt,
                                   &usedColors, &usedBytes);
            if (report) {
                if (usedBytes >= 0) {
                    *report = QObject::tr("%1 colors · %2 MB")
                                  .arg(usedColors)
                                  .arg(usedBytes / (1024.0 * 1024.0), 0, 'f',
                                       2);
                } else {
                    *report = QObject::tr("%1 colors").arg(usedColors);
                }
            }
        } else {
            img = quantizeIndexed(img, settings.paletteColors,
                                  settings.paletteDither, gray);
        }
        if (img.isNull()) {
            if (error) *error = QObject::tr("Could not quantize the image.");
            return false;
        }
    }

    if (settings.dither && !palettized)
        img = img.convertToFormat(QImage::Format_ARGB32_Premultiplied,
                                  Qt::DiffuseDither);

#ifdef PITTORE_TIFF
    // TIFF goes through the built-in libtiff encoder: exact bit depth, gray
    // modes, and real None/LZW/ZIP compression, which Qt's writer cannot select.
    if (isTiff) {
        const bool gray = settings.pixelFormat == QLatin1String("gray8") ||
                          settings.pixelFormat == QLatin1String("gray16");
        const bool wide = settings.pixelFormat == QLatin1String("rgb16") ||
                          settings.pixelFormat == QLatin1String("gray16");
        QImage flat = img;
        if (gray && flat.hasAlphaChannel())
            flat = flattenOverMatte(flat, matte);
        const std::vector<std::uint16_t> rgba = rgba16FromQImage(flat);
        if (rgba.empty()) {
            if (error) *error = QObject::tr("Could not encode TIFF.");
            return false;
        }
        pittore::io::TiffEncodeOptions opt;
        opt.bits = wide ? 16 : 8;
        opt.grayscale = gray;
        // A CMYK document delivered through "Use document format" separates
        // into ink planes here (grayscale output was chosen explicitly, so it
        // wins); `icc` is its destination profile, empty -> naive core.
        opt.cmyk = settings.cmyk && !gray;
        if (opt.cmyk)
            opt.icc.assign(settings.icc.cbegin(), settings.icc.cend());
        opt.withAlpha = flat.hasAlphaChannel();
        opt.compression = qBound(0, settings.tiffCompression, 2);
        std::vector<std::uint8_t> bytes;
        if (!pittore::io::tiffEncodeExport(
                static_cast<std::uint32_t>(flat.width()),
                static_cast<std::uint32_t>(flat.height()), settings.dpi,
                rgba.data(), opt, bytes)) {
            if (error) *error = QObject::tr("Could not encode TIFF.");
            return false;
        }
        if (report && opt.cmyk) *report = QObject::tr("CMYK");
        QSaveFile file(path);
        if (!file.open(QIODevice::WriteOnly)) {
            if (error) *error = QObject::tr("Cannot write %1").arg(path);
            return false;
        }
        file.write(reinterpret_cast<const char*>(bytes.data()),
                   static_cast<qint64>(bytes.size()));
        if (!file.commit()) {
            if (error) *error = QObject::tr("Could not save %1").arg(path);
            return false;
        }
        return true;
    }
#else
    Q_UNUSED(isTiff);
#endif

    // Formats without a dedicated encoder above (PSD/XCF/WebP/…) keep the
    // shared saveImageFile path exactly as before.
    if (fmt != QLatin1String("png") && fmt != QLatin1String("jpg") &&
        fmt != QLatin1String("gif") && fmt != QLatin1String("bmp")) {
        return saveImageFile(img, path, error, settings.quality,
                             settings.interlace || settings.progressive);
    }

    // PNG / JPEG / GIF / BMP. PNG always takes the max-lossless pass
    // (adaptive filtering + zlib-9 + gray-fold + sub-byte packing): same
    // pixels, smaller file. JPEG/GIF/BMP go through QImageWriter directly
    // so quality, progressive scans and metadata text chunks reach them.
    if (fmt == QLatin1String("png"))
        return writeOptimizedPng(img, path, settings, error);
    const QByteArray wfmt =
        (fmt == QLatin1String("jpg")) ? QByteArrayLiteral("jpeg")
                                      : fmt.toLatin1();
    if (!QImageWriter::supportedImageFormats().contains(wfmt)) {
        if (error)
            *error = QObject::tr("Unsupported export format \".%1\".").arg(fmt);
        return false;
    }
    QImageWriter writer(path, wfmt);
    if (writer.supportsOption(QImageIOHandler::Quality) &&
        formatHasQuality(fmt))
        writer.setQuality(qBound(1, settings.quality, 100));
    if ((settings.interlace || settings.progressive) &&
        writer.supportsOption(QImageIOHandler::ProgressiveScanWrite))
        writer.setProgressiveScanWrite(true);
    if (settings.embedMetadata) {
        writer.setText(QStringLiteral("Software"),
                       QStringLiteral("Pittore Studio"));
        if (settings.dpi > 0)
            writer.setText(QStringLiteral("Resolution"),
                           QStringLiteral("%1 ppi").arg(settings.dpi));
    }
    if (!writer.write(img)) {
        if (error)
            *error = writer.errorString().isEmpty()
                         ? QObject::tr("Could not save %1").arg(path)
                         : writer.errorString();
        return false;
    }
    return true;
}


bool writeExportVector(DocumentItem& doc, const ExportSettings& settings,
                       const QString& path, QString* error) {
    const QRect canvas(QPoint(0, 0), doc.size);
    if (canvas.isEmpty() || doc.layers.isEmpty()) {
        if (error) *error = QObject::tr("Nothing to export (empty document).");
        return false;
    }

    // "Don't export hidden layers" off reveals everything for the render.
    // A single-layer target always paints its layer (hidden or not).
    AllLayersVisibleGuard visGuard(!settings.hideHiddenLayers ? &doc : nullptr);

    // Same target resolution as the raster path: whole canvas, the selection,
    // or a single layer cropped to its own bounds. The crop becomes the SVG
    // viewBox, so output scaling stays resolution-independent.
    QRect crop = canvas;
    bool soloLayer = false;
    if (settings.exportTarget == -2 && !doc.selection.isEmpty()) {
        const QRect sel = doc.selection.toAlignedRect().intersected(canvas);
        if (!sel.isEmpty()) crop = sel;
    } else if (settings.exportTarget >= 0 &&
               settings.exportTarget < doc.layers.size()) {
        const QRect bounds =
            layerBounds(doc, doc.layers[settings.exportTarget])
                .toAlignedRect()
                .intersected(canvas);
        if (!bounds.isEmpty()) crop = bounds;
        soloLayer = true;
    }

    pittore::vector::ArtSvgOptions svgOpt;
    svgOpt.decimals = qBound(0, settings.svgDecimalPlaces, 6);
    svgOpt.setViewbox = settings.svgSetViewbox;
    svgOpt.lineBreaks = settings.svgLineBreaks;

    pittore::vector::ArtDocument art;
    QSize output = settings.pixelSize;
    if (!output.isValid() || output.isEmpty()) output = crop.size();
    art.width = output.width();
    art.height = output.height();
    art.viewX = crop.x();
    art.viewY = crop.y();
    art.viewW = crop.width();
    art.viewH = crop.height();

    auto writeSvg = [&](const pittore::vector::ArtDocument& a) {
        const std::string svg = pittore::vector::artDocumentToSvg(a, svgOpt);
        if (svg.empty()) {
            if (error) *error = QObject::tr("Could not build the SVG.");
            return false;
        }
        QSaveFile file(path);
        if (!file.open(QIODevice::WriteOnly)) {
            if (error) *error = QObject::tr("Cannot write %1").arg(path);
            return false;
        }
        file.write(reinterpret_cast<const char*>(svg.data()),
                   static_cast<qint64>(svg.size()));
        if (!file.commit()) {
            if (error) *error = QObject::tr("Could not save %1").arg(path);
            return false;
        }
        return true;
    };

    // "Rasterize: Everything" (the flatten preset): one bitmap filling the
    // viewBox, like a flattened SVG.
    if (settings.svgRasterize == 1) {
        QImage flat = doc.composite.copy(crop);
        if (flat.isNull()) {
            if (error)
                *error = QObject::tr("Nothing to export (no visible layers).");
            return false;
        }
        if (flat.size() != output)
            flat = flat.scaled(output, Qt::IgnoreAspectRatio,
                               Qt::SmoothTransformation);
        pittore::vector::ArtElement el;
        el.raster = true;
        el.image.x = crop.x();
        el.image.y = crop.y();
        el.image.w = crop.width();
        el.image.h = crop.height();
        el.image.opacity = 1.0;
        el.image.base64Png = pngBase64Of(flat);
        el.image.mime = "image/png";
        if (el.image.base64Png.empty()) {
            if (error) *error = QObject::tr("Could not build the SVG.");
            return false;
        }
        art.elements.push_back(std::move(el));
        return writeSvg(art);
    }

    auto qmat = [](const double m[6]) {
        return QTransform(m[0], m[1], m[2], m[3], m[4], m[5]);
    };

    // Output scale for the downsample readout: document px to output px.
    const double outScale =
        (crop.width() > 0 && output.width() > 0)
            ? double(output.width()) / crop.width()
            : 1.0;
    const double dpiRef = settings.dpi > 0 ? settings.dpi : 96;

    // Paint order: layers index 0 is the top of the stack, so walk backwards.
    // A single-layer target paints only that layer (hidden or not), like the
    // raster path's solo render.
    const int soloIndex = soloLayer ? settings.exportTarget : -1;
    for (int i = doc.layers.size() - 1; i >= 0; --i) {
        const LayerItem& l = doc.layers[i];
        if (soloIndex >= 0 && i != soloIndex) continue;
        if (!soloLayer && settings.hideHiddenLayers && !l.visible) continue;
        const std::string blend = blendCssFor(l.blendMode).toStdString();

        // Real vector when the layer kept its geometry (an SVG import).
        if (l.art && !l.art->segments.empty()) {
            QTransform t = qmat(l.art->matrix);
            t *= QTransform::fromScale(l.scaleX, l.scaleY);
            t *= QTransform::fromTranslate(l.offset.x(), l.offset.y());
            pittore::vector::ArtElement el;
            el.node = *l.art;
            el.node.matrix[0] = t.m11();
            el.node.matrix[1] = t.m12();
            el.node.matrix[2] = t.m21();
            el.node.matrix[3] = t.m22();
            el.node.matrix[4] = t.dx();
            el.node.matrix[5] = t.dy();
            el.node.opacity =
                qBound(0.0, l.art->opacity * (l.opacity / 100.0), 1.0);
            el.blendCss = blend;
            art.elements.push_back(std::move(el));
            continue;
        }

        // Everything else: embed the layer's placed raster, style included.
        const LayerDrawSource src = layerDrawSource(l);
        if (!src.img || src.img->width() == 0 || src.img->height() == 0)
            continue;
        QImage raster = engineImageToQImage(*src.img);
        if (raster.isNull()) continue;
        const double placedW = src.img->width() * src.scaleX;
        const double placedOutW = placedW * outScale;
        // "Downsample images": rasters placed hotter than the threshold are
        // resampled down with the dialog's resample mode first.
        if (settings.svgDownsample && settings.svgAboveDpi > 0 &&
            placedOutW > 0 && dpiRef > 0) {
            const double effDpi = raster.width() / (placedOutW / dpiRef);
            if (effDpi > settings.svgAboveDpi) {
                const double f = settings.svgAboveDpi / effDpi;
                const QSize ns(qMax(1, int(raster.width() * f)),
                               qMax(1, int(raster.height() * f)));
                if (ns != raster.size()) {
                    switch (settings.resampleMode) {
                        case 2:
                            raster = raster.scaled(ns, Qt::IgnoreAspectRatio,
                                                   Qt::FastTransformation);
                            break;
                        case 1:
                            raster = lanczosResample(raster, ns);
                            break;
                        default:
                            raster = raster.scaled(ns, Qt::IgnoreAspectRatio,
                                                   Qt::SmoothTransformation);
                            break;
                    }
                    if (raster.isNull()) continue;
                }
            }
        }
        pittore::vector::ArtElement el;
        el.raster = true;
        el.image.x = src.offset.x();
        el.image.y = src.offset.y();
        el.image.w = src.img->width() * src.scaleX;
        el.image.h = src.img->height() * src.scaleY;
        el.image.opacity = qBound(0.0, l.opacity / 100.0, 1.0);
        // "Allow JPEG compression": opaque-friendly rasters ride as JPEG at
        // the dialog quality; anything with transparency stays PNG.
        if (settings.svgAllowJpeg) {
            QImage flat = raster;
            if (flat.hasAlphaChannel())
                flat = flattenOverMatte(flat, QColor(255, 255, 255));
            const std::string jpg =
                jpegBase64Of(flat, qBound(1, settings.svgJpegQuality, 100));
            if (!jpg.empty()) {
                el.image.base64Png = jpg;
                el.image.mime = "image/jpeg";
            } else {
                el.image.base64Png = pngBase64Of(raster);
                el.image.mime = "image/png";
            }
        } else {
            el.image.base64Png = pngBase64Of(raster);
            el.image.mime = "image/png";
        }
        el.blendCss = blend;
        art.elements.push_back(std::move(el));
    }

    if (art.elements.empty()) {
        if (error) *error = QObject::tr("Nothing to export (no visible layers).");
        return false;
    }

    const QString lower = path.toLower();
    if (lower.endsWith(QStringLiteral(".dxf"))) {
        // DXF R12 polyline subset: vector nodes flatten to polylines, raster
        // layers contribute their bounds (CAD tools trace the frame).
        std::vector<vector::DxfPolyline> lines;
        for (auto& el : art.elements) {
            if (el.raster) {
                vector::DxfPolyline frame;
                frame.closed = true;
                frame.points = {{el.image.x, el.image.y},
                                {el.image.x + el.image.w, el.image.y},
                                {el.image.x + el.image.w, el.image.y + el.image.h},
                                {el.image.x, el.image.y + el.image.h}};
                lines.push_back(frame);
                continue;
            }
            auto flat = vector::flattenSegments(el.node.segments, 0.5f);
            for (auto& sp : flat.subpaths) {
                if (sp.size() < 2) continue;
                vector::DxfPolyline pl;
                pl.closed = el.node.evenOdd ? false : flat.closed.empty() ? false : false;
                for (auto [x, y] : sp) {
                    double wx = el.node.matrix[0] * x + el.node.matrix[2] * y +
                                el.node.matrix[4];
                    double wy = el.node.matrix[1] * x + el.node.matrix[3] * y +
                                el.node.matrix[5];
                    pl.points.emplace_back(wx, wy);
                }
                lines.push_back(pl);
            }
        }
        const std::string dxf = vector::writeDxf(lines);
        QSaveFile file(path);
        if (!file.open(QIODevice::WriteOnly)) {
            if (error) *error = QObject::tr("Cannot write %1").arg(path);
            return false;
        }
        file.write(dxf.data(), (qint64)dxf.size());
        return file.commit();
    }
    if (lower.endsWith(QStringLiteral(".pdf"))) {
        // Minimal vector PDF: one page with stroked/filled path operators and
        // an internal-link annotation when a single named destination exists.
        std::string content;
        char buf[128];
        for (auto& el : art.elements) {
            if (el.raster) continue;  // raster embedding rides the SVG path
            auto flat = vector::flattenSegments(el.node.segments, 0.5f);
            for (auto& sp : flat.subpaths) {
                if (sp.size() < 2) continue;
                for (size_t i = 0; i < sp.size(); i++) {
                    double wx = el.node.matrix[0] * sp[i].first +
                                el.node.matrix[2] * sp[i].second + el.node.matrix[4];
                    double wy = el.node.matrix[1] * sp[i].first +
                                el.node.matrix[3] * sp[i].second + el.node.matrix[5];
                    snprintf(buf, sizeof(buf), "%.2f %.2f %c ", wx, wy, i == 0 ? 'm' : 'l');
                    content += buf;
                }
                content += el.node.paint.hasFill ? "f " : "S ";
            }
        }
        if (content.empty()) content = "0 0 m ";
        vector::PdfPage pg;
        pg.wPt = art.width > 0 ? art.width * 72.0 / 96.0 : 595;
        pg.hPt = art.height > 0 ? art.height * 72.0 / 96.0 : 842;
        pg.content = content;
        const auto bytes = vector::writePdf({pg}, "Pittore Studio");
        QSaveFile file(path);
        if (!file.open(QIODevice::WriteOnly)) {
            if (error) *error = QObject::tr("Cannot write %1").arg(path);
            return false;
        }
        file.write(reinterpret_cast<const char*>(bytes.data()), (qint64)bytes.size());
        return file.commit();
    }

    return writeSvg(art);
}

}  // namespace pittore::ui
