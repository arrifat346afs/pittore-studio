// Wash baking and the stamp-brush dabs: folding an armed wash stroke back into
// the layer at tool opacity, resolving a tool's density/tip/texture options
// into the engine's dab descriptors, and painting or erasing with a stamped
// tip. Split out of app_state.cpp.

#include "ui/app_state.h"
#include "ui/app_state_detail.h"

#include "engine/compute/brushes/stamp/stamp.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <random>

namespace pittore::ui {

// Folds an armed wash stroke into the active layer at the tool opacity.
bool AppState::bakeWashStroke() {
    if (!washArmed_ || washDirty_.isNull()) {
        washArmed_ = false;
        washDirty_ = QRect();
        return false;
    }    DocumentItem* d = activeDocument();
    LayerItem* layer = activeLayer();
    const ToolId tool = activeTool_;
    const QRect baked = washDirty_;
    washArmed_ = false;
    washDirty_ = QRect();
    if (!d || !layer || layer->kind != LayerItem::Kind::Pixel) return false;
    if (!layer->pixels || washScratch_.width() != layer->pixels->width() ||
        washScratch_.height() != layer->pixels->height())
        return false;
    const QVariant o = option(tool, QStringLiteral("opacity"));
    const double op = o.isValid() ? std::clamp(o.toDouble() / 100.0, 0.0, 1.0)
                                  : 1.0;
    if (op <= 0.0) return true;  // invisible stroke: nothing to fold in
    const std::uint32_t w = layer->pixels->width();
    const std::uint32_t h = layer->pixels->height();
    const int x0 = std::max(0, baked.left());
    const int y0 = std::max(0, baked.top());
    const int x1 = std::min<int>(w, baked.left() + baked.width());
    const int y1 = std::min<int>(h, baked.top() + baked.height());
    pittore::RGBAf* dst = layer->pixels->data();
    const pittore::RGBAf* src = washScratch_.data();
    for (int y = y0; y < y1; ++y) {
        for (int x = x0; x < x1; ++x) {
            const std::size_t i = std::size_t(y) * w + x;
            const float sa =
                std::clamp(src[i].a * float(op), 0.0f, 1.0f);
            if (sa <= 0.0f) continue;
            RGBAf& dd = dst[i];
            const float inv = 1.0f - sa;
            const float out_a = sa + dd.a * inv;
            if (out_a > 0.0f) {
                dd.r = (src[i].r * sa + dd.r * dd.a * inv) / out_a;
                dd.g = (src[i].g * sa + dd.g * dd.a * inv) / out_a;
                dd.b = (src[i].b * sa + dd.b * inv) / out_a;
                dd.a = out_a;
            }
        }
    }
    ++layer->sourceStamp;
    layer->thumbnail = QImage();
    return true;
}

pittore::compute::DabDensity AppState::resolveDensity(ToolId tool) {    const QVariant v = option(tool, QStringLiteral("brush_density"));
    const double pct = v.isValid() ? std::clamp(v.toDouble(), 0.0, 100.0) : 100.0;
    pittore::compute::DabDensity den;
    den.density = float(pct / 100.0);
    den.seed = static_cast<std::uint32_t>(
        (strokeSeed_ & 0xFFFFFFFFull) ^ (std::uint64_t(dabCount_) * 2654435761ull));
    ++dabCount_;
    return den;
}

double AppState::strokeRandom() {
    return std::uniform_real_distribution<double>(0.0, 1.0)(strokeRng_);
}

pittore::compute::MaskTip AppState::resolveMaskTip(ToolId tool,
                                                     double pressure01) const {
    pittore::compute::MaskTip m;
    const QString id =
        option(tool, QStringLiteral("brush_mask_stamp")).toString();
    if (id.isEmpty()) return m;
    const pittore::compute::StampTip* tip = brushStamp(id);
    if (!tip || !tip->valid()) return m;
    auto dbl = [&](const char* key, double fallback) {
        const QVariant v = option(tool, QString::fromUtf8(key));
        return v.isValid() ? v.toDouble() : fallback;
    };
    m.bitmap.alpha = tip->alpha.data();
    m.bitmap.w = tip->w;
    m.bitmap.h = tip->h;
    m.sizeRatio = float(
        std::clamp(dbl("brush_mask_ratio", 100.0) / 100.0, 0.05, 4.0));
    // Pressure shrinks the mask with the dab when the tool opts in.
    if (option(tool, QStringLiteral("brush_mask_pressure")).toBool())
        m.sizeRatio *= float(0.25 + 0.75 * std::clamp(pressure01, 0.0, 1.0));
    m.angleDeg =
        float(std::clamp(dbl("brush_mask_angle", 0.0), -180.0, 180.0));
    m.mode = std::clamp(int(dbl("brush_mask_mode", 0.0)), 0, 3);
    return m;
}

// Paper grain for the active texture options, mapped into the dab's target
// space. Returns an invalid (disabled) texture when no pattern applies.
pittore::compute::PatternTex AppState::resolveTexture(
    ToolId tool, double lsx, double lsy, const QPointF& targetOffset,
    double pressure01) const {
    pittore::compute::PatternTex tex;
    const QString id =
        option(tool, QStringLiteral("brush_texture")).toString();
    if (id.isEmpty()) return tex;
    const PatternGray* pat = brushPattern(id);
    if (!pat) return tex;
    auto dbl = [&](const char* key, double fallback) {
        const QVariant v = option(tool, QString::fromUtf8(key));
        return v.isValid() ? v.toDouble() : fallback;
    };
    tex.gray = pat->gray.data();
    tex.w = pat->w;
    tex.h = pat->h;
    tex.strength = float(std::clamp(dbl("brush_texture_strength", 80.0) / 100.0,
                                    0.0, 1.0));
    // Pressure thins the grain with the dab when the tool opts in.
    if (option(tool, QStringLiteral("brush_texture_pressure")).toBool())
        tex.strength *=
            float(0.25 + 0.75 * std::clamp(pressure01, 0.0, 1.0));
    const double sc = dbl("brush_texture_scale", 100.0) / 100.0;
    tex.scale = float(sc > 1e-6 ? sc : 1.0);
    const double sx = std::max(lsx, 1e-6), sy = std::max(lsy, 1e-6);
    tex.offsetX =
        float((strokePatternOrigin_.x() - targetOffset.x()) / sx);
    tex.offsetY =
        float((strokePatternOrigin_.y() - targetOffset.y()) / sy);
    tex.neutral = float(std::clamp(dbl("brush_texture_neutral", 50.0) / 100.0,
                                   0.0, 1.0));
    tex.brightness = float(std::clamp(dbl("brush_texture_brightness", 0.0) / 100.0,
                                      -1.0, 1.0));
    tex.contrast = float(std::clamp(dbl("brush_texture_contrast", 100.0) / 100.0,
                                    0.0, 4.0));
    const QVariant iv = option(tool, QStringLiteral("brush_texture_invert"));
    tex.invert = iv.isValid() && iv.toBool();
    // Auto-invert for eraser: flip the grain when erasing so the same
    // preset cuts instead of deposits (opt-in per preset).
    if (option(tool, QStringLiteral("brush_texture_auto_invert_eraser"))
            .toBool()) {
        const bool erasing =
            tool == ToolId::Eraser ||
            option(tool, QStringLiteral("brush_erase_blend")).toBool();
        if (erasing) tex.invert = !tex.invert;
    }
    tex.mode = std::clamp(
        option(tool, QStringLiteral("brush_texture_mode")).toInt(0), 0, 6);
    tex.soft =
        option(tool, QStringLiteral("brush_texture_soft")).toBool();
    tex.cutoffPolicy =
        std::clamp(option(tool, QStringLiteral("brush_texture_cutoff_policy"))
                       .toInt(0),
                   0, 2);
    tex.cutLo = float(
        std::clamp(dbl("brush_texture_cutlo", 0.0), 0.0, 1.0));
    tex.cutHi =
        float(std::clamp(dbl("brush_texture_cuthi", 1.0), 0.0, 1.0));
    if (tex.cutHi < tex.cutLo) std::swap(tex.cutLo, tex.cutHi);
    return tex;
}

bool AppState::stampDab(const QPointF& docPos, double radius, int mode,
                        double opacity, const QColor& color, double angleDeg,
                        const QString& stampId) {
    const pittore::compute::StampTip* tip = brushStamp(stampId);
    if (!tip) return false;
    return stampTipDab(docPos, radius, mode, opacity, color, angleDeg, *tip);
}

bool AppState::stampTipDab(const QPointF& docPos, double radius, int mode,
                           double opacity, const QColor& color,
                           double angleDeg,
                           const pittore::compute::StampTip& tip,
                           int flip, int filter) {
    DocumentItem* d = activeDocument();
    if (!d || d->size.isEmpty()) return false;
    LayerItem* layer = activeLayer();
    if (!layer || !layer->visible || layer->lockTransparency) return false;
    if (layer->kind != LayerItem::Kind::Pixel &&
        layer->kind != LayerItem::Kind::Adjustment)
        return false;
    const bool toMask = layer->maskSelected;
    if (toMask || layer->kind != LayerItem::Kind::Pixel) {
        setStatusHint(tr("Stamp brushes paint pixels only (no mask target)."));
        return false;
    }
    ensureLayerPixels(*d, *layer);
    // Same document → target mapping as paintDab.
    QPointF targetOffset = layer->offset;
    double targetScaleX = layer->scaleX, targetScaleY = layer->scaleY;
    const pittore::Image* targetImage = layer->pixels.get();
    const double lsx = std::max(targetScaleX, 1e-6);
    const double lsy = std::max(targetScaleY, 1e-6);
    const std::uint32_t lw = targetImage->width();
    const std::uint32_t lh = targetImage->height();
    const float lcx = static_cast<float>((docPos.x() - targetOffset.x()) / lsx);
    const float lcy = static_cast<float>((docPos.y() - targetOffset.y()) / lsy);
    const float lradius = static_cast<float>(radius / std::min(lsx, lsy));
    // Per-stroke cached selection (one resample per stroke, not per dab).
    const auto* selPtr = strokeSelectionMask(*d, *layer, targetImage,
                                             targetOffset, lsx, lsy);
    const auto smode = mode == 1   ? pittore::compute::StampMode::ColorImage
                       : mode == 2 ? pittore::compute::StampMode::LightnessMap
                       : mode == 3 ? pittore::compute::StampMode::GradientMap
                                   : pittore::compute::StampMode::AlphaMask;
    auto optDbl = [&](const char* key, double fallback) {
        const QVariant v = option(activeTool_, QString::fromUtf8(key));
        return v.isValid() ? v.toDouble() : fallback;
    };
    pittore::compute::StampLevels levels;
    levels.neutral = float(
        std::clamp(optDbl("brush_tip_neutral", 50.0) / 100.0, 0.0, 1.0));
    levels.brightness = float(
        std::clamp(optDbl("brush_tip_brightness", 0.0) / 100.0, -1.0, 1.0));
    levels.contrast = float(
        std::clamp(optDbl("brush_tip_contrast", 100.0) / 100.0, 0.0, 4.0));
    const QColor bgQ = background();
    const pittore::RGBAf bgC{float(bgQ.redF()), float(bgQ.greenF()),
                              float(bgQ.blueF()), 1.0f};
    const pittore::compute::PatternTex tex =
        resolveTexture(activeTool_, lsx, lsy, targetOffset, dabPressure01_);
    const pittore::compute::DabDensity dabDenStamp =
        resolveDensity(activeTool_);
    const pittore::compute::MaskTip maskStamp =
        resolveMaskTip(activeTool_, dabPressure01_);
    // Relief deposition (lightness mode with thickness): ensure a plane
    // matching the target and scale the tip lightness by the thickness.
    float* heightPtr = nullptr;
    float heightAmt = 0.0f;
    if (mode == 2) {
        const QVariant tv =
            option(activeTool_, QStringLiteral("brush_tip_thickness"));
        heightAmt = float(std::clamp(tv.isValid() ? tv.toDouble() / 100.0 : 0.0,
                                     0.0, 1.0));
        if (heightAmt > 0.0f) {
            ensureLayerHeight(*layer);
            heightPtr = heightPlaneFor(*layer, lw, lh);
        }
    }
    pittore::compute::stamp_dab_host(
        layer->pixels->data(), lw, lh, lcx, lcy, lradius, tip, smode,
        static_cast<float>(std::clamp(opacity, 0.0, 1.0)),
        pittore::RGBAf{color.redF(), color.greenF(), color.blueF(), 1.0f},
        static_cast<float>(angleDeg), selPtr, &tex, &dabDenStamp, &maskStamp,
        flip, filter, &levels, &bgC, heightPtr, heightAmt);
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

bool AppState::stampEraseDab(const QPointF& docPos, double radius,
                             double opacity, double angleDeg,
                             const QString& stampId, int flip, int filter) {
    const pittore::compute::StampTip* tip = brushStamp(stampId);
    if (!tip) return false;
    return stampTipEraseDab(docPos, radius, opacity, angleDeg, *tip, flip,
                            filter);
}

bool AppState::stampTipEraseDab(const QPointF& docPos, double radius,
                                double opacity, double angleDeg,
                                const pittore::compute::StampTip& tip,
                                int flip, int filter) {
    DocumentItem* d = activeDocument();
    if (!d || d->size.isEmpty()) return false;
    LayerItem* layer = activeLayer();
    if (!layer || !layer->visible || layer->lockTransparency) return false;
    if (layer->kind != LayerItem::Kind::Pixel) return false;
    if (layer->maskSelected) {
        setStatusHint(tr("Stamp brushes paint pixels only (no mask target)."));
        return false;
    }
    // Background parity (see eraseDab): paint the canvas paper through
    // the same tip as an alpha mask so the eraser shape matches exactly;
    // a transparent bottom falls through to alpha erase below.
    if (layer == &d->layers.last() && !d->canvasTransparent) {
        return stampTipDab(docPos, radius, 0, opacity, d->canvasPaper,
                           angleDeg, tip, flip, filter);
    }
    ensureLayerPixels(*d, *layer);
    QPointF targetOffset = layer->offset;
    double targetScaleX = layer->scaleX, targetScaleY = layer->scaleY;
    const pittore::Image* targetImage = layer->pixels.get();
    const double lsx = std::max(targetScaleX, 1e-6);
    const double lsy = std::max(targetScaleY, 1e-6);
    const float lcx = static_cast<float>((docPos.x() - targetOffset.x()) / lsx);
    const float lcy = static_cast<float>((docPos.y() - targetOffset.y()) / lsy);
    const float lradius = static_cast<float>(radius / std::min(lsx, lsy));
    // Per-stroke cached selection (one resample per stroke, not per dab).
    const auto* selPtr = strokeSelectionMask(*d, *layer, targetImage,
                                             targetOffset, lsx, lsy);
    const pittore::compute::PatternTex stampEraseTex =
        resolveTexture(activeTool_, lsx, lsy, targetOffset, dabPressure01_);
    const pittore::compute::DabDensity dabDenStampErase =
        resolveDensity(activeTool_);
    const pittore::compute::MaskTip maskStampErase =
        resolveMaskTip(activeTool_, dabPressure01_);
    // Erased paint takes its relief with it (no plane created just to
    // carve nothing; stale-sized planes are skipped, never written).
    float* eraseHeight =
        heightPlaneFor(*layer, targetImage->width(), targetImage->height());
    pittore::compute::stamp_erase_dab_host(
        layer->pixels->data(), targetImage->width(), targetImage->height(),
        lcx, lcy, lradius, tip,
        static_cast<float>(std::clamp(opacity, 0.0, 1.0)),
        static_cast<float>(angleDeg), selPtr, &stampEraseTex, &dabDenStampErase,
        &maskStampErase, flip, filter, eraseHeight);
    d->touchPixelsAfterDab(*layer, lcx, lcy, lradius, targetImage->width(),
                           targetImage->height());
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
