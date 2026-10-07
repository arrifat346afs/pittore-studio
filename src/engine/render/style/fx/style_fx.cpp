#include "engine/render/layer_style.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <utility>
#include <vector>

#include "engine/render/style/shared/style_plane.h"
#include "engine/render/style/blend/style_blend.h"
#include "engine/render/style/blur/style_blur.h"
#include "engine/render/style/sdf/style_sdf.h"
#include "engine/render/style/fx/style_fx.h"

namespace pittore::render {
namespace detail {


std::vector<float> offsetAlpha(const std::vector<float>& alpha, int w, int h, float dx, float dy) {
    if (dx == 0.0f && dy == 0.0f) return alpha;
    std::vector<float> out(alpha.size());
    auto sample = [&](int x, int y) -> float {
        if (x < 0 || y < 0 || x >= w || y >= h) return 0.0f;
        return alpha[std::size_t(y) * w + x];
    };
    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            const float sx = x - dx, sy = y - dy;
            const int x0 = static_cast<int>(std::floor(sx));
            const int y0 = static_cast<int>(std::floor(sy));
            const float fx = sx - x0, fy = sy - y0;
            out[std::size_t(y) * w + x] =
                sample(x0, y0) * (1.0f - fx) * (1.0f - fy) +
                sample(x0 + 1, y0) * fx * (1.0f - fy) +
                sample(x0, y0 + 1) * (1.0f - fx) * fy +
                sample(x0 + 1, y0 + 1) * fx * fy;
        }
    }
    return out;
}


// The intensity slider is stored inverted in `Comp` and hardens the
// matte after the blur: the glow returns as the plain blur scaled by
// 1 / (1 - intensity) and clipped.
void applySpread(std::vector<float>& a, float spread) {
    if (spread <= 0.0f) return;
    const float k = std::max(1.0f - std::clamp(spread, 0.0f, 0.99f), 0.01f);
    for (float& v : a) v = std::min(v / k, 1.0f);
}


float smoothBand(float d, float lo, float hi) {
    if (hi <= lo) return 0.0f;
    const float up = std::clamp((d - lo) / 0.5f + 0.5f, 0.0f, 1.0f);
    const float down = std::clamp((hi - d) / 0.5f + 0.5f, 0.0f, 1.0f);
    return std::clamp(up * down, 0.0f, 1.0f);
}


std::pair<float, float> polar(float angleDeg, float distance) {
    const float rad = angleDeg * 3.14159265358979323846f / 180.0f;
    return {-std::cos(rad) * distance, std::sin(rad) * distance};
}

void shadowPlane(Plane& out, const std::vector<float>& alpha, int w, int h,
                   const Rect& rect, const ShadowStyle& s, bool inner) {
    const auto [dx, dy] = polar(s.angle, s.distance);
    std::vector<float> a = offsetAlpha(alpha, w, h, dx, dy);
    if (inner)
        for (float& v : a) v = 1.0f - v;
    gaussianBlurPlane(a, w, h, s.size);
    applySpread(a, s.spread);
    out = planeFromAlpha(rect, a, s.color);
}


void glowPlane(Plane& out, const std::vector<float>& alpha, int w, int h, const Rect& rect,
                 const GlowStyle& g, bool inner) {
    std::vector<float> a(alpha.size());
    if (inner)
        for (std::size_t i = 0; i < a.size(); ++i) a[i] = 1.0f - alpha[i];
    else
        a = alpha;
    gaussianBlurPlane(a, w, h, g.size);
    applySpread(a, g.spread);
    out = planeFromAlpha(rect, a, g.color);
}


void satinPlane(Plane& out, const std::vector<float>& alpha, int w, int h, const Rect& rect,
                const SatinStyle& s) {
    const auto [dx, dy] = polar(s.angle, s.distance);
    std::vector<float> a = offsetAlpha(alpha, w, h, dx, dy);
    std::vector<float> b = offsetAlpha(alpha, w, h, -dx, -dy);
    gaussianBlurPlane(a, w, h, s.size);
    gaussianBlurPlane(b, w, h, s.size);
    std::vector<float> d(a.size());
    for (std::size_t i = 0; i < d.size(); ++i) {
        const float diff = std::abs(a[i] - b[i]);
        d[i] = s.invert ? 1.0f - diff : diff;
    }
    out = planeFromAlpha(rect, d, s.color);
}


void strokePlane(Plane& out, const std::vector<float>& alpha, int w, int h, const Rect& rect,
                   const StrokeStyle& s) {
    float outer = 0.0f, inner = 0.0f;
    if (s.position == 0) {
        outer = s.size;
    } else if (s.position == 2) {
        inner = s.size;
    } else {
        outer = inner = s.size / 2.0f;
    }
    const std::vector<float> dist =
        signedDistance(alpha, w, h, std::max(outer, inner) + 2.0f);
    std::vector<float> band(dist.size());
    for (std::size_t i = 0; i < band.size(); ++i)
        band[i] = smoothBand(dist[i], -inner, outer);
    out = planeFromAlpha(rect, band, s.color);
}


void bevelPlane(Plane& out, const std::vector<float>& alpha, int w, int h, const Rect& rect,
                  const BevelStyle& b) {
    std::vector<float> height = alpha;
    gaussianBlurPlane(height, w, h, std::max(b.size, 0.5f));
    if (b.soften > 0.0f) gaussianBlurPlane(height, w, h, b.soften);
    const float rad = b.angle * 3.14159265358979323846f / 180.0f;
    const float alt = b.altitude * 3.14159265358979323846f / 180.0f;
    const float lx = std::cos(rad) * std::cos(alt);
    const float ly = -std::sin(rad) * std::cos(alt);
    const float lz = std::max(std::sin(alt), 1e-3f);
    const float sign = b.style == 3 ? -1.0f : 1.0f;  // pillow emboss flips
    std::vector<float> hi(alpha.size(), 0.0f), lo(alpha.size(), 0.0f);
    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            const std::size_t i = std::size_t(y) * w + x;
            const float l = height[i - (x > 0 ? 1 : 0)];
            const float r = height[(x + 1 < w) ? i + 1 : i];
            const float u = height[(y > 0) ? i - w : i];
            const float d = height[(y + 1 < h) ? i + w : i];
            const float nx = (l - r) * b.depth * sign;
            const float ny = (u - d) * b.depth * sign;
            const float len = std::sqrt(nx * nx + ny * ny + 1.0f);
            const float dot = (nx * lx + ny * ly + lz) / len;
            const float shade = (dot - lz) / std::max(1.0f - lz, 1e-3f);
            float gate = 1.0f;
            if (b.style == 0) gate = 1.0f - alpha[i];
            else if (b.style == 1) gate = alpha[i];
            if (shade > 0.0f)
                hi[i] = std::min(shade, 1.0f) * gate;
            else
                lo[i] = std::min(-shade, 1.0f) * gate;
        }
    }
    const std::vector<float> mask =
        b.style == 0 ? std::vector<float>(alpha.size(), 1.0f) : alpha;
    Plane hp = planeFromAlpha(rect, hi, b.highlight);
    blendOver(out, hp, b.highlightBlend, b.highlightOpacity, &mask);
    Plane sp = planeFromAlpha(rect, lo, b.shadow);
    blendOver(out, sp, b.shadowBlend, b.shadowOpacity, &mask);
}


void gradientPlane(Plane& out, const Rect& rect, const GradientStyle& o,
                     const Rect& content) {
    out = blankPlane(rect);
    const int w = out.w(), h = out.h();
    const float cx = (content.x0 + content.x1) / 2.0f;
    const float cy = (content.y0 + content.y1) / 2.0f;
    const float half = std::max(static_cast<float>(std::max(content.width(), content.height())) /
                                    2.0f,
                                1.0f) *
                       std::max(o.scale, 0.1f);
    const float rad = o.angle * 3.14159265358979323846f / 180.0f;
    const float ux = std::cos(rad), uy = -std::sin(rad);
    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            const float px = rect.x0 + x - cx;
            const float py = rect.y0 + y - cy;
            float t = o.radial ? std::hypot(px, py) / half
                               : (px * ux + py * uy) / (2.0f * half) + 0.5f;
            t = std::clamp(t, 0.0f, 1.0f);
            if (o.reverse) t = 1.0f - t;
            const std::size_t i = (std::size_t(y) * w + x) * 4;
            out.px[i] = o.from.r + (o.to.r - o.from.r) * t;
            out.px[i + 1] = o.from.g + (o.to.g - o.from.g) * t;
            out.px[i + 2] = o.from.b + (o.to.b - o.from.b) * t;
            out.px[i + 3] = o.from.a + (o.to.a - o.from.a) * t;
        }
    }
}


void compositeBehind(Plane& dst, const Plane& src, float opacity,
                       const std::vector<float>* knockout) {
    Plane top = blankPlane(dst.rect);
    top.px = src.px;
    for (std::size_t i = 0; i < top.px.size() / 4; ++i) {
        float a = top.px[i * 4 + 3] * opacity;
        if (knockout) a *= 1.0f - (*knockout)[i];
        top.px[i * 4 + 3] = a;
    }
    blendOver(top, dst, StyleBlend::Normal, 1.0f, nullptr);
    dst.px = std::move(top.px);
}


void blurContent(Plane& base, std::vector<float>& alpha, int w, int h, const BlurStyle& b) {
    if (b.radius < 0.5f) return;
    const std::size_t n = std::size_t(w) * h;
    std::vector<float> chan(n), kept(n);
    for (std::size_t i = 0; i < n; ++i) kept[i] = base.px[i * 4 + 3];
    for (int c = 0; c < 4; ++c) {
        for (std::size_t i = 0; i < n; ++i) {
            const float a = base.px[i * 4 + 3];
            chan[i] = c == 3 ? a : base.px[i * 4 + c] * a;
        }
        gaussianBlurPlane(chan, w, h, b.radius);
        for (std::size_t i = 0; i < n; ++i) base.px[i * 4 + c] = chan[i];
    }
    for (std::size_t i = 0; i < n; ++i) {
        const float a = base.px[i * 4 + 3];
        if (a > 1e-6f) {
            const float un = 1.0f / a;
            for (int c = 0; c < 3; ++c) base.px[i * 4 + c] *= un;
        }
        if (b.preserveAlpha) base.px[i * 4 + 3] = kept[i];
    }
    if (!b.preserveAlpha) gaussianBlurPlane(alpha, w, h, b.radius);
}

}  // namespace detail
}  // namespace pittore::render
