// The smudge engines: the travelling patch lives in AppState (one per
// stroke) and the engine primes / lays / reloads it per dab. One helper
// resamples the composite for "sample all layers" pickup; the two dabs below
// differ only in how they build the dab coverage (auto tip vs library stamp).

#include "ui/app_state.h"
#include "ui/app_state_detail.h"

#include "engine/compute/brushes/smudge/smudge.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>

namespace pittore::ui {

namespace {
// Coverage box of a smudge dab in layer pixels, clamped to the image,
// grown by the smear trail so sample-all pickup never clamps at the edge.
QRect smudgeTileRect(float lcx, float lcy, float lradius, std::uint32_t lw,
                     std::uint32_t lh, float trailX = 0.0f,
                     float trailY = 0.0f) {
    const float grow =
        lradius + std::hypot(trailX, trailY) + 2.0f;
    const int x0 = std::max(0, static_cast<int>(std::floor(lcx - grow)));
    const int y0 = std::max(0, static_cast<int>(std::floor(lcy - grow)));
    const int x1 = std::min(static_cast<int>(lw) - 1,
                            static_cast<int>(std::ceil(lcx + grow)));
    const int y1 = std::min(static_cast<int>(lh) - 1,
                            static_cast<int>(std::ceil(lcy + grow)));
    if (x1 < x0 || y1 < y0) return QRect();
    return QRect(x0, y0, x1 - x0 + 1, y1 - y0 + 1);
}

// Resample the composite into a layer-space tile (nearest, un-premultiplied):
// the "sample all layers" pickup source. Mirrors the spot-heal match buffer,
// but only over the dab footprint instead of the whole layer.
std::vector<pittore::RGBAf> smudgeCompositeTile(const QImage& comp,
                                                 const QPointF& offset,
                                                 double lsx, double lsy,
                                                 const QRect& tile) {
    std::vector<pittore::RGBAf> out;
    if (tile.isEmpty() || comp.isNull()) return out;
    QImage src = comp;
    if (src.format() != QImage::Format_ARGB32_Premultiplied &&
        src.format() != QImage::Format_ARGB32)
        src = src.convertToFormat(QImage::Format_ARGB32_Premultiplied);
    out.resize(static_cast<std::size_t>(tile.width()) * tile.height());
    for (int ty = 0; ty < tile.height(); ++ty) {
        const double docY = offset.y() + (tile.top() + ty + 0.5) * lsy;
        const int py = std::clamp(static_cast<int>(std::floor(docY)), 0,
                                  src.height() - 1);
        const QRgb* row =
            reinterpret_cast<const QRgb*>(src.constScanLine(py));
        for (int tx = 0; tx < tile.width(); ++tx) {
            const double docX = offset.x() + (tile.left() + tx + 0.5) * lsx;
            const int px = std::clamp(static_cast<int>(std::floor(docX)), 0,
                                      src.width() - 1);
            const QRgb c = row[px];
            const int al = qAlpha(c);
            pittore::RGBAf& dstC =
                out[std::size_t(ty) * tile.width() + tx];
            if (al <= 0) {
                dstC = pittore::RGBAf{0, 0, 0, 0};
            } else {
                const float inv = 1.0f / static_cast<float>(al);
                dstC = pittore::RGBAf{qRed(c) * inv, qGreen(c) * inv,
                                       qBlue(c) * inv,
                                       al / 255.0f};
            }
        }
    }
    return out;
}
}  // namespace

bool AppState::smudgeTipDab(const QPointF& docPos, double radius,
                            double hardness, double ratio, double angleDeg,
                            bool squareTip, double rate, double radiusFrac,
                            bool sampleAll,
                            pittore::compute::BlendMode blend, bool finger,
                            int smudgeMode, double colorRate, double trailX,
                            double trailY) {
    DocumentItem* d = activeDocument();
    if (!d || d->size.isEmpty()) return false;
    LayerItem* layer = activeLayer();
    if (!layer || !layer->visible || layer->lockTransparency) return false;
    if (layer->kind != LayerItem::Kind::Pixel || layer->maskSelected)
        return false;
    ensureLayerPixels(*d, *layer);
    QPointF targetOffset = layer->offset;
    const double lsx = std::max(layer->scaleX, 1e-6);
    const double lsy = std::max(layer->scaleY, 1e-6);
    const pittore::Image* targetImage = layer->pixels.get();
    const std::uint32_t lw = targetImage->width();
    const std::uint32_t lh = targetImage->height();
    const float lcx = static_cast<float>((docPos.x() - targetOffset.x()) / lsx);
    const float lcy = static_cast<float>((docPos.y() - targetOffset.y()) / lsy);
    const float lradius = static_cast<float>(radius / std::min(lsx, lsy));
    // Per-stroke cached selection (one resample per stroke, not per dab).
    const auto* selPtr = strokeSelectionMask(*d, *layer, targetImage,
                                             targetOffset, lsx, lsy);
    pittore::compute::AutoTip tip;
    tip.silhouette = squareTip ? pittore::compute::TipSilhouette::Square
                               : pittore::compute::TipSilhouette::Round;
    tip.ratio = static_cast<float>(std::clamp(ratio, 0.01, 1.0));
    tip.angleDeg = static_cast<float>(angleDeg);
    tip.hardness = static_cast<float>(std::clamp(hardness, 0.0, 1.0));
    tip.spikes = qBound(
        0, option(activeTool_, QStringLiteral("brush_spikes")).toInt(), 12);
    tip.fadeAniso = static_cast<float>(qBound(
        -100.0,
        option(activeTool_, QStringLiteral("brush_fade_aniso")).toDouble(),
        100.0) /
                                        100.0);
    tip.falloff = qBound(
        0, option(activeTool_, QStringLiteral("brush_falloff")).toInt(), 1);
    tip.sharpness = static_cast<float>(qBound(
        0.0, option(activeTool_, QStringLiteral("brush_sharpness")).toDouble(),
        100.0) /
                                        100.0);
    tip.soften = static_cast<float>(qBound(
        0.0, option(activeTool_, QStringLiteral("brush_soften")).toDouble(),
        100.0) /
                                     100.0);
    tip.sanitize();
    pittore::compute::SmudgeCtl ctl;
    ctl.mode = smudgeMode == 1 ? pittore::compute::SmudgeMode::Smear
                               : pittore::compute::SmudgeMode::Dulling;
    ctl.colorRate = static_cast<float>(std::clamp(colorRate, 0.0, 1.0));
    ctl.fg = pittore::RGBAf{foreground_.redF(), foreground_.greenF(),
                             foreground_.blueF(), 1.0f};
    ctl.trailX = static_cast<float>(trailX / std::max(lsx, 1e-6));
    ctl.trailY = static_cast<float>(trailY / std::max(lsy, 1e-6));
    ctl.fingerPaint = finger;
    const pittore::compute::PatternTex tex =
        resolveTexture(activeTool_, lsx, lsy, targetOffset, dabPressure01_);
    const pittore::compute::DabDensity dabDenSmudge =
        resolveDensity(activeTool_);
    const pittore::compute::MaskTip maskSmudge =
        resolveMaskTip(activeTool_, dabPressure01_);
    // Sample-all-layers pickup: composite resampled into a dab-sized tile,
    // grown by the smear trail so trailing reads stay inside the tile.
    std::vector<pittore::RGBAf> pickBuf;
    pittore::compute::SmudgePick pick;
    const pittore::compute::SmudgePick* pickPtr = nullptr;
    if (sampleAll && !d->composite.isNull()) {
        const QRect tile = smudgeTileRect(lcx, lcy, lradius, lw, lh,
                                          ctl.trailX, ctl.trailY);
        if (!tile.isEmpty()) {
            pickBuf = smudgeCompositeTile(d->composite, targetOffset, lsx,
                                          lsy, tile);
            if (!pickBuf.empty()) {
                pick.data = pickBuf.data();
                pick.w = static_cast<std::uint32_t>(tile.width());
                pick.h = static_cast<std::uint32_t>(tile.height());
                pick.ox = static_cast<float>(tile.left());
                pick.oy = static_cast<float>(tile.top());
                pickPtr = &pick;
            }
        }
    }
    pittore::compute::smudge_tip_dab_host(
        layer->pixels->data(), lw, lh, lcx, lcy, lradius, tip,
        static_cast<float>(std::clamp(rate, 0.0, 1.0)),
        static_cast<float>(radiusFrac), smudgeCarry_, selPtr, &tex, &dabDenSmudge,
        &maskSmudge, 0, pickPtr, blend, &ctl,
        heightPlaneFor(*layer, lw, lh));
    d->touchPixelsAfterDab(*layer, lcx, lcy, lradius, lw, lh);
    const double ex = (static_cast<double>(lradius) + 1.0) * lsx + 1.0;
    const double ey = (static_cast<double>(lradius) + 1.0) * lsy + 1.0;
    const QRect dabRect =
        QRect(int(std::floor(docPos.x() - ex)), int(std::floor(docPos.y() - ey)),
              int(std::ceil(2 * ex)) + 1, int(std::ceil(2 * ey)) + 1)
            .intersected(QRect(QPoint(0, 0), d->size));
    d->paintDirty = d->paintDirty.isNull() ? dabRect : d->paintDirty.united(dabRect);
    return true;
}

bool AppState::smudgeStampDab(const QPointF& docPos, double radius,
                              double angleDeg,
                              const pittore::compute::StampTip& tip,
                              double rate, double radiusFrac, int flip,
                              int filter, bool sampleAll,
                              pittore::compute::BlendMode blend,
                              bool finger, int smudgeMode, double colorRate,
                              double trailX, double trailY) {
    DocumentItem* d = activeDocument();
    if (!d || d->size.isEmpty()) return false;
    LayerItem* layer = activeLayer();
    if (!layer || !layer->visible || layer->lockTransparency) return false;
    if (layer->kind != LayerItem::Kind::Pixel || layer->maskSelected)
        return false;
    ensureLayerPixels(*d, *layer);
    QPointF targetOffset = layer->offset;
    const double lsx = std::max(layer->scaleX, 1e-6);
    const double lsy = std::max(layer->scaleY, 1e-6);
    const pittore::Image* targetImage = layer->pixels.get();
    const std::uint32_t lw = targetImage->width();
    const std::uint32_t lh = targetImage->height();
    const float lcx = static_cast<float>((docPos.x() - targetOffset.x()) / lsx);
    const float lcy = static_cast<float>((docPos.y() - targetOffset.y()) / lsy);
    const float lradius = static_cast<float>(radius / std::min(lsx, lsy));
    // Per-stroke cached selection (one resample per stroke, not per dab).
    const auto* selPtr = strokeSelectionMask(*d, *layer, targetImage,
                                             targetOffset, lsx, lsy);
    const pittore::compute::PatternTex tex =
        resolveTexture(activeTool_, lsx, lsy, targetOffset, dabPressure01_);
    const pittore::compute::DabDensity dabDenSmudgeStamp =
        resolveDensity(activeTool_);
    const pittore::compute::MaskTip maskSmudgeStamp =
        resolveMaskTip(activeTool_, dabPressure01_);
    pittore::compute::SmudgeCtl ctl;
    ctl.mode = smudgeMode == 1 ? pittore::compute::SmudgeMode::Smear
                               : pittore::compute::SmudgeMode::Dulling;
    ctl.colorRate = static_cast<float>(std::clamp(colorRate, 0.0, 1.0));
    ctl.fg = pittore::RGBAf{foreground_.redF(), foreground_.greenF(),
                             foreground_.blueF(), 1.0f};
    ctl.trailX = static_cast<float>(trailX / std::max(lsx, 1e-6));
    ctl.trailY = static_cast<float>(trailY / std::max(lsy, 1e-6));
    ctl.fingerPaint = finger;
    std::vector<pittore::RGBAf> pickBufStamp;
    pittore::compute::SmudgePick pickStamp;
    const pittore::compute::SmudgePick* pickStampPtr = nullptr;
    if (sampleAll && !d->composite.isNull()) {
        const QRect tile = smudgeTileRect(lcx, lcy, lradius, lw, lh,
                                          ctl.trailX, ctl.trailY);
        if (!tile.isEmpty()) {
            pickBufStamp = smudgeCompositeTile(d->composite, targetOffset,
                                               lsx, lsy, tile);
            if (!pickBufStamp.empty()) {
                pickStamp.data = pickBufStamp.data();
                pickStamp.w = static_cast<std::uint32_t>(tile.width());
                pickStamp.h = static_cast<std::uint32_t>(tile.height());
                pickStamp.ox = static_cast<float>(tile.left());
                pickStamp.oy = static_cast<float>(tile.top());
                pickStampPtr = &pickStamp;
            }
        }
    }
    pittore::compute::smudge_stamp_dab_host(
        layer->pixels->data(), lw, lh, lcx, lcy, lradius, tip,
        static_cast<float>(std::clamp(rate, 0.0, 1.0)),
        static_cast<float>(radiusFrac), static_cast<float>(angleDeg),
        smudgeCarry_, selPtr, &tex, &dabDenSmudgeStamp, &maskSmudgeStamp,
        flip, filter, pickStampPtr, blend, &ctl,
        heightPlaneFor(*layer, lw, lh));
    d->touchPixelsAfterDab(*layer, lcx, lcy, lradius, lw, lh);
    const double ex = (static_cast<double>(lradius) + 1.0) * lsx + 1.0;
    const double ey = (static_cast<double>(lradius) + 1.0) * lsy + 1.0;
    const QRect dabRect =
        QRect(int(std::floor(docPos.x() - ex)), int(std::floor(docPos.y() - ey)),
              int(std::ceil(2 * ex)) + 1, int(std::ceil(2 * ey)) + 1)
            .intersected(QRect(QPoint(0, 0), d->size));
    d->paintDirty = d->paintDirty.isNull() ? dabRect : d->paintDirty.united(dabRect);
    return true;
}

}  // namespace pittore::ui
