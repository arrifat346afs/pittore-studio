#pragma once
// Lens filters. Split from engine/filter/filters.h.
#include <string>
#include <vector>

#include "engine/core/image.h"
#include "engine/filter/core/filter_detail.h"
#include "engine/filter/core/filter_params.h"

namespace pittore::filter {

inline bool applyLens(Image& img, Image& scratch, const std::string& id,
                       const std::vector<double>& par) {
    using namespace detail;
    const std::uint32_t w = img.width();
    const std::uint32_t h = img.height();
    auto p0 = pv(par, 0, 0.0);
    auto p1 = pv(par, 1, 0.0);
    if (id == "lens_correction") {
        const double k = p0 / 100.0 * 0.35;
        const double red = p1;
        const double blue = p2v(par);
        const double vig = p3v(par) / 100.0;
        const double mid = std::clamp(p4v(par), 0.0, 100.0) / 100.0;
        const double vert = p5v(par) / 100.0;
        const double horiz = p6v(par) / 100.0;
        const double straighten = p7v(par) * 3.14159265 / 180.0;
        const double scale = std::clamp(p8v(par), 50.0, 200.0) / 100.0;
        for (std::uint32_t y = 0; y < h; ++y) {
            for (std::uint32_t x = 0; x < w; ++x) {
                double nx = (static_cast<double>(x) / w) * 2 - 1;
                double ny = (static_cast<double>(y) / h) * 2 - 1;
                nx += horiz * 0.5;
                ny += vert * 0.5;
                const double r2 = nx * nx + ny * ny;
                const double f = 1.0 + k * r2;
                double sx = (nx * f + 1) * 0.5 * w;
                double sy = (ny * f + 1) * 0.5 * h;
                sx = (sx - w * 0.5) / scale + w * 0.5;
                sy = (sy - h * 0.5) / scale + h * 0.5;
                if (std::fabs(straighten) > 1e-4) {
                    const double cx = w * 0.5;
                    const double cy = h * 0.5;
                    const double dx = sx - cx;
                    const double dy = sy - cy;
                    sx = cx + dx * std::cos(straighten) - dy * std::sin(straighten);
                    sy = cy + dx * std::sin(straighten) + dy * std::cos(straighten);
                }
                RGBAf s = bilin(img, sx, sy);
                if (std::fabs(red) > 0.5 || std::fabs(blue) > 0.5) {
                    s.r = bilin(img, sx + nx * red * 0.002 * w * 0.01, sy).r;
                    s.b = bilin(img, sx - nx * blue * 0.002 * w * 0.01, sy).b;
                }
                const double vr = std::sqrt(r2) / 1.5;
                const double v = c01(1.0 - vig * std::clamp((vr - mid) / (1.0 - mid + 1e-6), 0.0, 1.0));
                s.r = static_cast<float>(c01(s.r * v));
                s.g = static_cast<float>(c01(s.g * v));
                s.b = static_cast<float>(c01(s.b * v));
                scratch.at(x, y) = s;
            }
        }
        std::memcpy(img.data(), scratch.data(), sizeof(RGBAf) * img.pixel_count());
        return true;
    }
    if (id == "adaptive_wide_angle") {
        const int proj = std::clamp(static_cast<int>(std::round(p0)), 0, 2);
        const double f = std::clamp(p1, 4.0, 60.0);
        const double crop = std::clamp(p2v(par), 0.5, 3.0);
        const double scale = std::clamp(p3v(par), 50.0, 200.0) / 100.0;
        const double k = (24.0 - f) / 24.0 * (proj == 2 ? 0.45 : 0.25);
        remapWarp(img, scratch, [&](double x, double y, double ww, double hh) {
            double nx = (x / ww) * 2 - 1;
            double ny = (y / hh) * 2 - 1;
            const double r2 = nx * nx + ny * ny;
            const double ff = proj == 1 ? (1.0 + k * r2 * 0.4) : (1.0 + k * r2);
            nx = nx * ff * crop / scale;
            ny = ny * ff * crop / scale;
            return std::make_pair((nx + 1) * 0.5 * ww, (ny + 1) * 0.5 * hh);
        });
        return true;
    }
    if (id == "camera_raw") {
        const double temp = p0 / 100.0;
        const double tint = p1 / 100.0;
        const double exp = p2v(par);
        const double con = p3v(par) / 100.0;
        const double hi = p4v(par) / 100.0;
        const double sh = p5v(par) / 100.0;
        const double wh = p6v(par) / 100.0;
        const double bl = p7v(par) / 100.0;
        const double clar = p8v(par) / 100.0;
        const double deh = p9v(par) / 100.0;
        const double vib = p10v(par) / 100.0;
        const double sat = p11v(par) / 100.0;
        const double sharp = p12v(par) / 150.0;
        const double noise = p13v(par) / 100.0;
        const double vig = p14v(par) / 100.0;
        Image soft = img.clone();
        if (std::fabs(clar) > 0.01 || noise > 0.01) {
            boxBlurInto(img, soft, 2);
        }
        for (std::uint32_t y = 0; y < h; ++y) {
            for (std::uint32_t x = 0; x < w; ++x) {
                RGBAf &t = img.at(x, y);
                double r = t.r * std::pow(2.0, exp) * (1 + temp * 0.1);
                double b = t.b * std::pow(2.0, exp) * (1 - temp * 0.1);
                double g = t.g * std::pow(2.0, exp) * (1 + tint * 0.05);
                const double l0 = (r + g + b) / 3;
                r = (r - 0.5) * (1 + con) + 0.5;
                g = (g - 0.5) * (1 + con) + 0.5;
                b = (b - 0.5) * (1 + con) + 0.5;
                const double l = (r + g + b) / 3;
                if (l > 0.6) {
                    const double tt = (l - 0.6) / 0.4;
                    r -= hi * 0.3 * tt;
                    g -= hi * 0.3 * tt;
                    b -= hi * 0.3 * tt;
                } else if (l < 0.4) {
                    const double tt = (0.4 - l) / 0.4;
                    r += sh * 0.3 * tt;
                    g += sh * 0.3 * tt;
                    b += sh * 0.3 * tt;
                }
                r = r * (1 - wh * 0.2 * l0) - wh * 0.05 * (1 - l0);
                g = g * (1 - wh * 0.2 * l0) - wh * 0.05 * (1 - l0);
                b = b * (1 - wh * 0.2 * l0) - wh * 0.05 * (1 - l0);
                r += bl * 0.25 * (1 - l0);
                g += bl * 0.25 * (1 - l0);
                b += bl * 0.25 * (1 - l0);
                const RGBAf &s = soft.at(x, y);
                const double cd = ((r + g + b) / 3 - (s.r + s.g + s.b) / 3);
                r += clar * cd;
                g += clar * cd;
                b += clar * cd;
                const double avg = (r + g + b) / 3;
                const double vibAmt = vib * (1.0 - std::min(1.0, std::fabs(avg - 0.5) * 2.0) * 0.5);
                r = avg + (r - avg) * (1 + vibAmt * 0.8 + sat * 0.8);
                g = avg + (g - avg) * (1 + vibAmt * 0.8 + sat * 0.8);
                b = avg + (b - avg) * (1 + vibAmt * 0.8 + sat * 0.8);
                r = r * (1 - deh * 0.15) + avg * deh * 0.15 + deh * 0.02;
                g = g * (1 - deh * 0.15) + avg * deh * 0.15 + deh * 0.02;
                b = b * (1 - deh * 0.15) + avg * deh * 0.15 + deh * 0.02;
                r = r * (1 - noise * 0.5) + s.r * noise * 0.5;
                g = g * (1 - noise * 0.5) + s.g * noise * 0.5;
                b = b * (1 - noise * 0.5) + s.b * noise * 0.5;
                const double nx = (static_cast<double>(x) / w) * 2 - 1;
                const double ny = (static_cast<double>(y) / h) * 2 - 1;
                const double vv = 1.0 - vig * 0.5 * (nx * nx + ny * ny) / 2.0;
                t.r = static_cast<float>(c01(r * vv));
                t.g = static_cast<float>(c01(g * vv));
                t.b = static_cast<float>(c01(b * vv));
            }
        }
        if (sharp > 0.01) unsharpF(img, sharp * 2.0, 1, 0.01);
        return true;
    }
    return false;
}

}  // namespace pittore::filter
