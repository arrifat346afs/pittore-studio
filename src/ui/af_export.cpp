// Layered Affinity export: DocumentItem -> AfLayersDoc (+ thumbnail).
//
// Mirrors ui/psd_export.cpp's structure (panel order becomes file order with
// group opacity/visibility unfolded), but targets the .af graph model:
// placed 8→16-bit pixels, doc-space masks, blend names in Affinity terms.
// Everything without an Affinity record (adjustments, live text, vector
// art, effects, fill kinds) bakes into the raster and is reported in
// AfExportDoc::baked so the dialog can say so.
#include "ui/af_export.h"

#include <QImage>
#include <QSize>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <ctime>
#include <functional>

#include "engine/io/af_layers/emit/af_write.h"
#include "ui/app_state.h"

#include "ui/app_state.h"
#include "ui/export/shared/export_helpers.h"
#include "ui/live_filter.h"
#include "ui/mask_finish.h"
#include "ui/project_manager.h"

namespace pittore::ui {
namespace {

std::uint16_t floatToU16(float v) {
    const long t = std::lround(static_cast<double>(v) * 65535.0);
    return static_cast<std::uint16_t>(std::clamp(t, 0L, 65535L));
}

std::uint8_t floatToU8(float v) {
    const long t = std::lround(static_cast<double>(v) * 255.0);
    return static_cast<std::uint8_t>(std::clamp(t, 0L, 255L));
}

// Document-space footprint of raw native pixels, canvas-clipped. Empty when
// the layer has no pixels or sits fully off-canvas.
QRect pixelFootprint(const LayerItem& l, const pittore::Image* px, const QSize& canvas) {
    if (!px || px->width() == 0 || px->height() == 0) return {};
    const double fw = static_cast<double>(px->width()) * l.scaleX;
    const double fh = static_cast<double>(px->height()) * l.scaleY;
    if (!(fw > 0.0) || !(fh > 0.0)) return {};
    const QRectF fp(l.offset, QSizeF(fw, fh));
    return fp.intersected(QRectF(QPointF(0, 0), QSizeF(canvas))).toAlignedRect();
}

// Straight RGBA16 samples for `rect` (document space) from raw native
// pixels. Scale 1:1 crops floats directly; anything else goes through a
// QImage smooth-resample of the whole footprint first.
std::vector<std::uint16_t> rasterizePixels(const LayerItem& l, const pittore::Image* px,
                                           const QRect& rect) {
    std::vector<std::uint16_t> out;
    if (rect.isEmpty() || !px) return out;
    const std::uint32_t nw = px->width(), nh = px->height();
    const int ox = qRound(l.offset.x()), oy = qRound(l.offset.y());
    if (l.scaleX == 1.0 && l.scaleY == 1.0) {
        out.resize(static_cast<std::size_t>(rect.width()) * rect.height() * 4);
        const pittore::RGBAf* src = px->data();
        std::size_t k = 0;
        for (int dy = 0; dy < rect.height(); ++dy) {
            const int sy = rect.top() + dy - oy;
            for (int dx = 0; dx < rect.width(); ++dx) {
                const int sx = rect.left() + dx - ox;
                float r = 0, g = 0, b = 0, a = 0;
                if (sx >= 0 && sy >= 0 && static_cast<std::uint32_t>(sx) < nw &&
                    static_cast<std::uint32_t>(sy) < nh) {
                    const pittore::RGBAf& p =
                        src[static_cast<std::size_t>(sy) * nw + sx];
                    r = p.r;
                    g = p.g;
                    b = p.b;
                    a = p.a;
                }
                out[k++] = floatToU16(r);
                out[k++] = floatToU16(g);
                out[k++] = floatToU16(b);
                out[k++] = floatToU16(a);
            }
        }
        return out;
    }
    QImage native = imageToStraightRgba64(*px);
    if (native.isNull()) return {};
    const int fw = std::max(1, qRound(static_cast<double>(nw) * l.scaleX));
    const int fh = std::max(1, qRound(static_cast<double>(nh) * l.scaleY));
    const QImage scaled = native.scaled(fw, fh, Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
    if (scaled.isNull()) return {};
    QImage tile(rect.width(), rect.height(), QImage::Format_RGBA64);
    tile.fill(0);
    for (int dy = 0; dy < rect.height(); ++dy) {
        const int sy = rect.top() + dy - oy;
        if (sy < 0 || sy >= scaled.height()) continue;
        auto* dst = reinterpret_cast<quint16*>(tile.scanLine(dy));
        const auto* srcRow = reinterpret_cast<const quint16*>(scaled.constScanLine(sy));
        for (int dx = 0; dx < rect.width(); ++dx) {
            const int sx = rect.left() + dx - ox;
            if (sx < 0 || sx >= scaled.width()) continue;
            dst[dx * 4 + 0] = srcRow[sx * 4 + 0];
            dst[dx * 4 + 1] = srcRow[sx * 4 + 1];
            dst[dx * 4 + 2] = srcRow[sx * 4 + 2];
            dst[dx * 4 + 3] = srcRow[sx * 4 + 3];
        }
    }
    return rgba16FromQImage(tile);
}

// Doc-space coverage grid over `rect` from the layer's mask image, with
// density/feather baked. Empty when there is no enabled mask.
std::vector<std::uint8_t> rasterizeMask(const LayerItem& l, const QRect& rect) {
    std::vector<std::uint8_t> out;
    if (rect.isEmpty() || !l.hasMask || !l.mask || !l.maskEnabled) return out;
    if (l.mask->width() == 0 || l.mask->height() == 0) return out;
    if (!(l.maskScaleX > 0.0) || !(l.maskScaleY > 0.0)) return out;
    const pittore::Image* m = l.mask.get();
    std::shared_ptr<pittore::Image> finished;
    if (l.maskFeather > 0.0f || l.maskDensity != 1.0f) {
        finished = finishMaskImage(*m, l.maskDensity, l.maskFeather);
        m = finished.get();
    }
    out.resize(static_cast<std::size_t>(rect.width()) * rect.height());
    std::size_t k = 0;
    for (int dy = 0; dy < rect.height(); ++dy)
        for (int dx = 0; dx < rect.width(); ++dx) {
            const double mx = (rect.left() + dx - l.maskOffset.x()) / l.maskScaleX;
            const double my = (rect.top() + dy - l.maskOffset.y()) / l.maskScaleY;
            out[k++] = floatToU8(sampleCoverageBilinear(*m, mx, my));
        }
    return out;
}

// Our blend names to Affinity's. Most match; Linear Dodge paints as Add;
// modes with no equivalent stay untouched for the encoder to fold to
// Normal (it reports them).
std::string affinityBlend(const QString& name) {
    if (name == QStringLiteral("Linear Dodge (Add)")) return "Add";
    return name.toStdString();
}

void fillCommon(pittore::io::AfLayer& f, const LayerItem& l, int indent,
                int unfoldedOpacity, bool unfoldedVisible) {
    f.name = l.name.toStdString();
    f.blend = affinityBlend(l.blendMode.isEmpty() ? QStringLiteral("Normal") : l.blendMode);
    f.opacity = static_cast<std::uint16_t>(qBound(0, qRound(unfoldedOpacity * 255.0 / 100.0), 255));
    f.visible = unfoldedVisible;
    f.clipped = l.clipped;
    f.indent = indent;
}

pittore::io::AfLayer exportPixel(const LayerItem& l, int indent, int unfoldedOpacity,
                                  bool unfoldedVisible, const QSize& canvas,
                                  std::vector<std::string>& baked) {
    pittore::io::AfLayer f;
    fillCommon(f, l, indent, unfoldedOpacity, unfoldedVisible);
    if (l.isText && !l.liveText) baked.push_back("text '" + f.name + "' baked");
    if (l.liveText) baked.push_back("live text '" + f.name + "' baked");
    if (l.art) baked.push_back("vector shape '" + f.name + "' baked");
    if (l.hasLiveFilter) baked.push_back("live filter on '" + f.name + "' baked");
    // Live filters bake: Affinity has no live-filter record, so the
    // filtered render goes out as plain pixels.
    ensureLayerFilter(l);
    const pittore::Image* base = liveFilterBase(l);
    const QRect rect = pixelFootprint(l, base, canvas);
    if (rect.isEmpty()) return f;
    f.left = rect.left();
    f.top = rect.top();
    f.width = static_cast<std::uint32_t>(rect.width());
    f.height = static_cast<std::uint32_t>(rect.height());
    f.rgba = rasterizePixels(l, base, rect);
    if (f.rgba.empty()) {
        f.width = f.height = 0;
        return f;
    }
    auto mask = rasterizeMask(l, rect);
    if (!mask.empty()) {
        pittore::io::AfMask m;
        m.left = rect.left();
        m.top = rect.top();
        m.width = f.width;
        m.height = f.height;
        m.px = std::move(mask);
        f.masks.push_back(std::move(m));
    }
    return f;
}

}  // namespace

AfExportDoc buildAfLayersDoc(DocumentItem& doc) {
    AfExportDoc out;
    out.doc.width = static_cast<std::uint32_t>(std::max(0, doc.size.width()));
    out.doc.height = static_cast<std::uint32_t>(std::max(0, doc.size.height()));
    out.doc.depth = 8;

    struct Node {
        int panelIdx = -1;
        int indent = 0;
        QVector<int> children;
    };
    QVector<Node> nodes;
    QVector<int> roots;
    QVector<int> open;
    for (int i = 0; i < doc.layers.size(); ++i) {
        const LayerItem& l = doc.layers[i];
        while (!open.isEmpty() && nodes[open.back()].indent >= l.indent) open.pop_back();
        Node n;
        n.panelIdx = i;
        n.indent = open.isEmpty() ? 0 : std::min(l.indent, nodes[open.back()].indent + 1);
        const int idx = nodes.size();
        nodes.append(n);
        if (!open.isEmpty())
            nodes[open.back()].children.append(idx);
        else
            roots.append(idx);
        if (l.kind == LayerItem::Kind::Group) open.append(idx);
    }

    std::function<void(const QVector<int>&, double, bool)> emitSiblings =
        [&](const QVector<int>& ids, double ancestorProduct, bool ancestorHidden) {
            for (int k = ids.size() - 1; k >= 0; --k) {
                const Node& n = nodes[ids[k]];
                const LayerItem& l = doc.layers[n.panelIdx];
                if (l.kind == LayerItem::Kind::Group) {
                    pittore::io::AfLayer f;
                    fillCommon(f, l, n.indent, l.opacity, l.visible);
                    f.isGroup = true;
                    f.groupExpanded = l.groupExpanded;
                    out.doc.layers.push_back(std::move(f));
                    emitSiblings(n.children, ancestorProduct * (l.opacity / 100.0),
                                 ancestorHidden || !l.visible);
                    continue;
                }
                const int unfoldedOpacity = ancestorProduct > 0.0
                                                ? qBound(1, qRound(l.opacity / ancestorProduct), 100)
                                                : l.opacity;
                const bool unfoldedVisible = l.visible || ancestorHidden;
                if (l.kind == LayerItem::Kind::Adjustment) {
                    if (l.adjustmentKind != 0)
                        out.baked.push_back("adjustment '" + l.name.toStdString() +
                                            "' has no Affinity form and was dropped");
                    continue;
                }
                out.doc.layers.push_back(
                    exportPixel(l, n.indent, unfoldedOpacity, unfoldedVisible, doc.size, out.baked));
            }
        };
    emitSiblings(roots, 1.0, false);
    out.doc.complete = true;
    return out;
}

std::optional<pittore::io::AfEncodeResult> AppState::exportAfLayers(
    DocumentItem& doc, QString* error) const {
    if (doc.size.isEmpty()) {
        if (error) *error = tr("Nothing to export (empty canvas).");
        return std::nullopt;
    }
    // A layer packed behind the flattened base expands for the read and packs
    // back afterwards: the encoder needs real pixels, but exporting must not
    // be what leaves a document inflated at 16 B/px for good.
    const QVector<int> expanded = materializeDeferredPixels(doc);
    AfExportDoc exp = buildAfLayersDoc(doc);
    stashDeferredPixels(doc, expanded);
    if (exp.doc.layers.empty()) {
        if (error) *error = tr("Nothing to export (no layers).");
        return std::nullopt;
    }
    // Template graph is built, not loaded — see afBuildTemplateDoc().
    const std::vector<std::uint8_t> tplDat = pittore::io::afBuildTemplateDoc();
    // Browser thumbnail: the live composite scaled to fit 512px.
    pittore::io::AfThumb thumb;
    {
        QImage flat = doc.composite;
        if (!flat.isNull()) {
            if (std::max(flat.width(), flat.height()) > 512)
                flat = flat.scaled(512, 512, Qt::KeepAspectRatio, Qt::SmoothTransformation);
            QImage rgba = flat.convertToFormat(QImage::Format_RGBA8888);
            thumb.width = static_cast<std::uint32_t>(rgba.width());
            thumb.height = static_cast<std::uint32_t>(rgba.height());
            thumb.rgba.assign(rgba.constBits(), rgba.constBits() + rgba.sizeInBytes());
        }
    }
    std::string codecError;
    auto enc = pittore::io::afEncodeLayers(
        exp.doc, tplDat, thumb, doc.title.toStdString(),
        static_cast<std::uint64_t>(std::time(nullptr)), &codecError);
    if (!enc) {
        if (error)
            *error = codecError.empty() ? tr("Could not encode Affinity file.")
                                        : QString::fromStdString(codecError);
        return std::nullopt;
    }
    enc->skipped.insert(enc->skipped.end(), exp.baked.begin(), exp.baked.end());
    return enc;
}

}  // namespace pittore::ui
