#include "engine/render/layer_style.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <utility>
#include <vector>

#include "engine/core/parallel.h"
#include "engine/render/style/shared/style_plane.h"
#include "engine/render/style/blend/style_blend.h"
#include "engine/render/style/blur/style_blur.h"
#include "engine/render/style/sdf/style_sdf.h"
#include "engine/render/style/fx/style_fx.h"

namespace pittore::render {

using namespace detail;


bool LayerStyle::empty() const {
    return !(hasBlur || hasDropShadow || hasInnerShadow || hasOuterGlow || hasInnerGlow ||
             hasColorOverlay || hasSatin || hasStroke || hasBevel || hasGradient);
}


int LayerStyle::outset() const {
    float out = 0.0f;
    const float blurReach = hasBlur ? blur.radius * 1.7321f : 0.0f;
    if (hasDropShadow)
        out = std::max(out, dropShadow.distance + dropShadow.size +
                                dropShadow.spread * dropShadow.size);
    if (hasOuterGlow)
        out = std::max(out, outerGlow.size + outerGlow.spread * outerGlow.size);
    if (hasStroke)
        out = std::max(out, stroke.position == 0 ? stroke.size
                          : stroke.position == 1 ? stroke.size / 2.0f : 0.0f);
    if (hasInnerGlow) out = std::max(out, innerGlow.size * 1.7321f);
    if (hasInnerShadow)
        out = std::max(out, (innerShadow.size + innerShadow.distance) * 1.7321f);
    if (hasBevel && bevel.style != 1) out = std::max(out, bevel.size + bevel.soften);
    return static_cast<int>(std::ceil(out + blurReach)) + 1;
}


bool applyLayerStyle(Rgba8Image& img, const LayerStyle& style, int* grow) {
    if (style.empty()) return true;
    const int cw = static_cast<int>(img.w), ch = static_cast<int>(img.h);
    if (cw <= 0 || ch <= 0) return true;
    const int pad = style.outset();
    const std::size_t grown = std::size_t(cw + 2 * pad) * std::size_t(ch + 2 * pad);
    if (grown > kMaxStylePixels) return false;
    const Rect rect{-pad, -pad, cw + pad, ch + pad};
    const int w = rect.width(), h = rect.height();

    // Read the straight-alpha source into a float plane over the grown rect.
    // Nothing writes `img` until the very end, so a failure to size the style
    // leaves the layer's own pixels untouched.
    const float inv = 1.0f / 255.0f;
    Plane base = blankPlane(rect);
    std::vector<float> alpha(std::size_t(w) * h, 0.0f);
    // Source rows are independent (each writes its own span of `base`/`alpha`
    // and only reads `img`), so the gather runs across cores bit-identically.
    auto gatherRow = [&](int y) {
        for (int x = 0; x < w; ++x) {
            const int sx = rect.x0 + x;
            const int sy = rect.y0 + y;
            const std::size_t i = std::size_t(y) * w + x;
            if (sx < 0 || sy < 0 || sx >= cw || sy >= ch) continue;
            const std::size_t at = (std::size_t(sy) * cw + sx) * 4;
            const float r = img.px[at] * inv;
            const float g = img.px[at + 1] * inv;
            const float b = img.px[at + 2] * inv;
            const float a = img.px[at + 3] * inv;
            alpha[i] = a;
            base.px[i * 4] = r;
            base.px[i * 4 + 1] = g;
            base.px[i * 4 + 2] = b;
            base.px[i * 4 + 3] = a;
        }
    };
    pittore::core::parallel_rows(static_cast<std::uint32_t>(h),
                                 [&](std::uint32_t y0, std::uint32_t y1) {
                                     for (int y = static_cast<int>(y0);
                                          y < static_cast<int>(y1); ++y)
                                         gatherRow(y);
                                 });
    if (style.hasBlur) blurContent(base, alpha, w, h, style.blur);

    Plane out = blankPlane(rect);
    if (style.hasDropShadow) {
        Plane shadow;
        shadowPlane(shadow, alpha, w, h, rect, style.dropShadow, false);
        const std::vector<float>* knock = style.dropShadow.knockout ? &alpha : nullptr;
        compositeBehind(out, shadow, style.dropShadow.opacity, knock);
    }
    if (style.hasOuterGlow) {
        Plane glow;
        glowPlane(glow, alpha, w, h, rect, style.outerGlow, false);
        compositeBehind(out, glow, style.outerGlow.opacity, &alpha);
    }

    blendOver(out, base, StyleBlend::Normal, 1.0f, nullptr);

    if (style.hasGradient) {
        Plane grad;
        gradientPlane(grad, rect, style.gradient, Rect{0, 0, cw, ch});
        blendOver(out, grad, style.gradient.blend, style.gradient.opacity, &alpha);
    }
    if (style.hasColorOverlay) {
        const std::vector<float> ones(std::size_t(w) * h, 1.0f);
        Plane flat = planeFromAlpha(rect, ones, style.colorOverlay.color);
        blendOver(out, flat, style.colorOverlay.blend, style.colorOverlay.opacity, &alpha);
    }
    if (style.hasSatin) {
        Plane satin;
        satinPlane(satin, alpha, w, h, rect, style.satin);
        blendOver(out, satin, style.satin.blend, style.satin.opacity, &alpha);
    }
    if (style.hasInnerGlow) {
        Plane glow;
        glowPlane(glow, alpha, w, h, rect, style.innerGlow, true);
        blendOver(out, glow, style.innerGlow.blend, style.innerGlow.opacity, &alpha);
    }
    if (style.hasInnerShadow) {
        Plane shadow;
        shadowPlane(shadow, alpha, w, h, rect, style.innerShadow, true);
        blendOver(out, shadow, style.innerShadow.blend, style.innerShadow.opacity, &alpha);
    }
    if (style.hasBevel) bevelPlane(out, alpha, w, h, rect, style.bevel);
    if (style.hasStroke) {
        Plane stroke;
        strokePlane(stroke, alpha, w, h, rect, style.stroke);
        blendOver(out, stroke, style.stroke.blend, style.stroke.opacity, nullptr);
    }

    if (grow) *grow = pad;
    img.w = static_cast<std::uint32_t>(w);
    img.h = static_cast<std::uint32_t>(h);
    img.px.resize(std::size_t(w) * h * 4);
    // Element-wise narrowing; each i writes only its own four bytes.
    {
        const std::uint32_t n = static_cast<std::uint32_t>(std::size_t(w) * h);
        pittore::core::parallel_for(n, 4096, [&](std::uint32_t i0, std::uint32_t i1) {
            for (std::size_t i = i0; i < i1; ++i) {
                for (int c = 0; c < 4; ++c)
                    img.px[i * 4 + c] = static_cast<std::uint8_t>(
                        std::clamp(out.px[i * 4 + c], 0.0f, 1.0f) * 255.0f + 0.5f);
            }
        });
    }
    return true;
}

}  // namespace pittore::render
