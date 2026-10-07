#include "ui/export/shared/export_helpers.h"

#include <QHash>
#include <QStringList>

namespace pittore::ui {
namespace export_detail {

// conventional built-in presets, in dialog order. Every preset is a full
// ExportSettings bundle so applying one is a pure widget fill (see
// ExportDialog::showSettings); edits afterwards drop the combo to Custom.
QVector<ExportPreset> builtInExportPresets(const QString& format) {
    QVector<ExportPreset> out;
    auto base = [&](const char* id, const char* label) {
        ExportPreset p;
        p.id = QString::fromLatin1(id);
        p.format = format;
        p.label = QString::fromUtf8(label);
        p.settings.format = format;
        return p;
    };
    if (format == QLatin1String("png")) {
        ExportPreset p = base("png", "PNG");
        out.append(p);
        p = base("png8", "PNG-8 (Dithered)");
        p.settings.palettized = true;
        p.settings.paletteColors = 256;
        p.settings.paletteDither = true;
        out.append(p);
        const char* hdrIds[] = {"png-hdr-pq-full", "png-hdr-pq-narrow",
                                "png-hdr-hlg-full", "png-hdr-hlg-narrow"};
        const char* hdrLabels[] = {
            "PNG HDR (PQ, Rec.2020, Full range)",
            "PNG HDR (PQ, Rec.2020, Narrow range)",
            "PNG HDR (HLG, Rec.2020, Full range)",
            "PNG HDR (HLG, Rec.2020, Narrow range)",
        };
        for (int i = 0; i < 4; ++i) {
            ExportPreset h = base(hdrIds[i], hdrLabels[i]);
            h.settings.transferFunction = (i < 2) ? 1 : 2;
            h.settings.primaries = 1;
            h.settings.fullRange = (i % 2) == 0;
            h.settings.pixelFormat = QStringLiteral("rgb16");
            out.append(h);
        }
    } else if (format == QLatin1String("jpg")) {
        const struct {
            const char* id;
            const char* label;
            int quality;
        } kQual[] = {
            {"jpeg-best", "JPEG (Best quality)", 100},
            {"jpeg-high", "JPEG (High quality)", 85},
            {"jpeg-medium", "JPEG (Medium quality)", 65},
            {"jpeg-low", "JPEG (Low quality)", 40},
        };
        for (const auto& q : kQual) {
            ExportPreset p = base(q.id, q.label);
            p.settings.quality = q.quality;
            out.append(p);
        }
    } else if (format == QLatin1String("gif")) {
        ExportPreset p = base("gif-rgb", "GIF RGB");
        p.settings.palettized = true;
        p.settings.paletteColors = 256;
        p.settings.paletteDither = true;
        out.append(p);
        p = base("gif-gray", "GIF Grayscale");
        p.settings.palettized = true;
        p.settings.paletteColors = 256;
        p.settings.paletteDither = true;
        p.settings.pixelFormat = QStringLiteral("gray8");
        out.append(p);
    } else if (format == QLatin1String("tiff")) {
        ExportPreset p = base("tiff-rgb8", "TIFF RGB 8-bit");
        p.settings.pixelFormat = QStringLiteral("rgb8");
        out.append(p);
        p = base("tiff-rgb16", "TIFF RGB 16-bit");
        p.settings.pixelFormat = QStringLiteral("rgb16");
        out.append(p);
        p = base("tiff-gray8", "TIFF Grayscale 8-bit");
        p.settings.pixelFormat = QStringLiteral("gray8");
        out.append(p);
        p = base("tiff-gray16", "TIFF Grayscale 16-bit");
        p.settings.pixelFormat = QStringLiteral("gray16");
        out.append(p);
    } else if (format == QLatin1String("svg")) {
        ExportPreset p = base("svg-export", "SVG (for export)");
        out.append(p);
        p = base("svg-small", "SVG (digital, Small size)");
        p.settings.svgAllowJpeg = true;
        p.settings.svgJpegQuality = 70;
        p.settings.svgDownsample = true;
        p.settings.svgAboveDpi = 300;
        out.append(p);
        p = base("svg-high", "SVG (digital, High quality)");
        p.settings.svgAllowJpeg = false;
        p.settings.svgDownsample = false;
        p.settings.svgDecimalPlaces = 4;
        out.append(p);
        p = base("svg-flatten", "SVG (flatten)");
        p.settings.svgRasterize = 1;
        out.append(p);
    }
    return out;
}

const ExportPreset* findExportPreset(const QString& id) {
    if (id.isEmpty()) return nullptr;
    // Small table: linear scan over every format's presets.
    static const QStringList kFormats{QStringLiteral("png"), QStringLiteral("jpg"),
                                      QStringLiteral("gif"), QStringLiteral("tiff"),
                                      QStringLiteral("svg")};
    for (const QString& f : kFormats) {
        const QVector<ExportPreset> list = builtInExportPresets(f);
        for (const ExportPreset& p : list) {
            if (p.id == id) {
                // Point into a stable copy: keep one cache per id.
                static QHash<QString, ExportPreset> cache;
                cache.insert(id, p);
                return &cache[id];
            }
        }
    }
    return nullptr;
}

}  // namespace export_detail
}  // namespace pittore::ui
