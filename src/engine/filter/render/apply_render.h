#pragma once
// Render filters. Split from engine/filter/filters.h.
#include <string>
#include <vector>

#include "engine/core/image.h"
#include "engine/filter/core/filter_detail.h"
#include "engine/filter/core/filter_params.h"

namespace pittore::filter {

inline bool applyRender(Image& img, Image& /*scratch*/, const std::string& id,
                       const std::vector<double>& par) {
    using namespace detail;
    const std::uint32_t w = img.width();
    const std::uint32_t h = img.height();
    auto p0 = pv(par, 0, 0.0);
    auto p1 = pv(par, 1, 0.0);
    if (id == "clouds" || id == "difference_clouds") {
        const std::uint64_t s = static_cast<std::uint64_t>(std::max(0.0, p0)) + 7;
        for (std::uint32_t y = 0; y < h; ++y) {
            for (std::uint32_t x = 0; x < w; ++x) {
                const double n = fbm(x * 0.015, y * 0.015, 5, s);
                RGBAf &t = img.at(x, y);
                if (id == std::string("clouds")) {
                    t.r = t.g = t.b = static_cast<float>(n * 1.4);
                } else {
                    const double d = std::fabs(t.r - n);
                    t.r = t.g = t.b = static_cast<float>(d);
                }
                t.r = static_cast<float>(c01(t.r));
                t.g = static_cast<float>(c01(t.g));
                t.b = static_cast<float>(c01(t.b));
            }
        }
        return true;
    }
    if (id == "fibers") {
        const double v = std::max(1.0, p0);
        const double st = std::max(1.0, p1);
        const std::uint64_t seed = static_cast<std::uint64_t>(std::max(0.0, p2v(par))) + 913;
        for (std::uint32_t y = 0; y < h; ++y) {
            for (std::uint32_t x = 0; x < w; ++x) {
                const double n = vnoise(static_cast<double>(x) * 0.4, static_cast<double>(y) * 0.02 * (17.0 - v * 0.2), seed) * st / 8.0;
                RGBAf &t = img.at(x, y);
                const double m = 0.7 + n * 0.6;
                t.r = static_cast<float>(c01(t.r * m));
                t.g = static_cast<float>(c01(t.g * m));
                t.b = static_cast<float>(c01(t.b * m));
            }
        }
        return true;
    }
    if (id == "lens_flare") {
        const double cx = w * std::clamp(p0, 0.0, 100.0) / 100.0;
        const double cy = h * std::clamp(p1, 0.0, 100.0) / 100.0;
        const double b = std::clamp(p2v(par), 10.0, 300.0) / 100.0;
        const int lens = std::clamp(static_cast<int>(std::round(p3v(par))), 0, 3);
        const double tint[4][3] = {{1.0, 0.95, 0.85}, {1.0, 0.9, 0.75}, {0.9, 0.95, 1.0}, {1.0, 0.85, 0.9}};
        for (std::uint32_t y = 0; y < h; ++y) {
            for (std::uint32_t x = 0; x < w; ++x) {
                const double d = std::hypot(static_cast<double>(x) - cx, static_cast<double>(y) - cy) / std::min(w, h);
                const double g = std::exp(-d * d * 18.0) * b;
                const double ring = std::exp(-std::pow((d - 0.35) * 9.0, 2.0)) * b * 0.25;
                RGBAf &t = img.at(x, y);
                t.r = static_cast<float>(c01(t.r + (g + ring * 0.6) * tint[lens][0]));
                t.g = static_cast<float>(c01(t.g + (g * 0.95 + ring * 0.8) * tint[lens][1]));
                t.b = static_cast<float>(c01(t.b + (g * 0.85 + ring) * tint[lens][2]));
            }
        }
        return true;
    }
    if (id == "lighting_effects") {
        const int type = std::clamp(static_cast<int>(std::round(p0)), 0, 2);
        const double px = w * std::clamp(p1, 0.0, 100.0) / 100.0;
        const double py = h * std::clamp(p2v(par), 0.0, 100.0) / 100.0;
        const double ang = p3v(par) * 3.141592653589793 / 180.0;
        const double inten = std::clamp(p4v(par), 0.0, 300.0) / 100.0;
        const double spread = std::max(5.0, p5v(par)) / 100.0;
        const double amb = std::clamp(p6v(par), 0.0, 100.0) / 100.0;
        const double gloss = std::clamp(p7v(par), 0.0, 100.0) / 100.0;
        const double height = std::max(1.0, p8v(par));
        const double dx = std::cos(ang);
        const double dy = std::sin(ang);
        for (std::uint32_t y = 0; y < h; ++y) {
            for (std::uint32_t x = 0; x < w; ++x) {
                double light = 0;
                if (type == 2) {
                    const double along = ((static_cast<double>(x) - px) * dx + (static_cast<double>(y) - py) * dy) / std::min(w, h);
                    light = std::clamp(0.5 + along * 2.0, 0.0, 1.5) * inten;
                } else {
                    const double d = std::hypot(static_cast<double>(x) - px, static_cast<double>(y) - py) / (height * 4.0);
                    if (type == 0) {
                        const double cone = ((static_cast<double>(x) - px) * dx + (static_cast<double>(y) - py) * dy) / (std::hypot(static_cast<double>(x) - px, static_cast<double>(y) - py) + 1e-6);
                        light = std::clamp((cone - (1.0 - spread)) / (spread + 1e-6), 0.0, 1.0) * std::exp(-d * d * 2.0) * inten * 2.0;
                    } else {
                        light = std::exp(-d * d * 2.0) * inten * 2.0;
                    }
                }
                const double e = edgeMag(img, x, y);
                const double spec = std::pow(std::clamp(light, 0.0, 1.0), 3.0) * e * gloss;
                RGBAf &t = img.at(x, y);
                const double k = amb * 0.5 + light * 0.75;
                t.r = static_cast<float>(c01(t.r * (0.35 + k) + spec));
                t.g = static_cast<float>(c01(t.g * (0.35 + k) + spec * 0.9));
                t.b = static_cast<float>(c01(t.b * (0.35 + k) + spec * 0.8));
            }
        }
        return true;
    }
    if (id == "flame") {
        const int n = std::max(1, static_cast<int>(std::round(p0)));
        const double hh = std::max(10.0, p1);
        const double fw = std::max(10.0, p2v(par));
        const double ang = p3v(par) * 3.141592653589793 / 180.0;
        const double turb = std::clamp(p4v(par), 0.0, 100.0) / 100.0;
        const double opac = std::clamp(p5v(par), 0.0, 100.0) / 100.0;
        const std::uint64_t seed = static_cast<std::uint64_t>(std::max(0.0, p6v(par))) + 300;
        const double ca = std::cos(ang);
        const double sa = std::sin(ang);
        for (std::uint32_t y = 0; y < h; ++y) {
            for (std::uint32_t x = 0; x < w; ++x) {
                const double rx = ((static_cast<double>(x) - w * 0.5) * ca - (static_cast<double>(y) - h * 0.5) * sa) / fw + 0.5;
                const double ry = ((static_cast<double>(x) - w * 0.5) * sa + (static_cast<double>(y) - h * 0.5) * ca) / hh + 0.5;
                double v = 0;
                for (int i = 0; i < n; ++i) {
                    const double fx = rx * n - i;
                    const double fl = fbm(fx * 3.0 + i * 9.0, ry * 4.0, 4, seed + i);
                    const double band = std::max(0.0, 1.0 - std::fabs(fx - 0.5) * 2.0);
                    v += band * (fl * (1 - turb * 0.5) + turb * 0.5 * fbm(fx * 9.0, ry * 12.0, 2, seed + 50 + i));
                }
                v /= n;
                const double heat = c01(v * 1.6 - ry * 0.7) * opac;
                RGBAf &t = img.at(x, y);
                t.r = static_cast<float>(c01(t.r * (1 - opac * 0.75) + heat * 1.0));
                t.g = static_cast<float>(c01(t.g * (1 - opac * 0.75) + heat * heat * 0.7));
                t.b = static_cast<float>(c01(t.b * (1 - opac * 0.75) + heat * heat * heat * 0.3));
            }
        }
        return true;
    }
    if (id == "picture_frame") {
        const int style = std::clamp(static_cast<int>(std::round(p0)), 0, 4);
        const double frac = std::clamp(p1, 1.0, 40.0) / 100.0;
        const double tone = std::clamp(p2v(par), 0.0, 100.0) / 100.0;
        const double relief = std::clamp(p3v(par), 0.0, 100.0) / 100.0;
        const int bw = std::max(1, static_cast<int>(std::round(std::min(w, h) * frac)));
        for (std::uint32_t y = 0; y < h; ++y) {
            for (std::uint32_t x = 0; x < w; ++x) {
                const std::uint32_t m = std::min({x, y, w - 1 - x, h - 1 - y});
                if (m < static_cast<std::uint32_t>(bw)) {
                    RGBAf &t = img.at(x, y);
                    const double f = static_cast<double>(m) / bw;
                    double sh = 0.35 + 0.65 * f;
                    if (style == 1) sh *= 0.75 + 0.5 * std::fabs(std::sin(f * 12.56));
                    if (style == 2) sh = 0.55 + 0.2 * f;
                    if (style == 3) sh = 0.5 + 0.5 * f * f;
                    if (style == 4) sh *= 0.7 + 0.6 * fbm(static_cast<double>(x) * 0.05, static_cast<double>(y) * 0.05, 3, 77);
                    sh *= 1.0 - relief * 0.3 + relief * 0.3 * f;
                    t.r = static_cast<float>(c01(t.r * sh * 0.6 + tone * 0.25 * sh));
                    t.g = static_cast<float>(c01(t.g * sh * 0.55 + tone * 0.2 * sh));
                    t.b = static_cast<float>(c01(t.b * sh * 0.5 + tone * 0.15 * sh));
                }
            }
        }
        return true;
    }
    if (id == "tree") {
        const double hh = std::clamp(p0, 20.0, 100.0) / 100.0 * h;
        const double trunk = std::max(1.0, p1);
        const double spread = std::clamp(p2v(par), 5.0, 90.0) * 3.141592653589793 / 180.0;
        const double leaves = std::clamp(p3v(par), 0.0, 100.0) / 100.0;
        const double leafSize = std::max(1.0, p4v(par));
        const double light = p5v(par) / 100.0;
        const std::uint64_t seed = static_cast<std::uint64_t>(std::max(0.0, p6v(par))) + 5150;
        const double cx = w * 0.5;
        for (std::uint32_t y = 0; y < h; ++y) {
            for (std::uint32_t x = 0; x < w; ++x) {
                const double t = 1.0 - static_cast<double>(y) / h;
                const double growH = t * hh;
                if (static_cast<double>(h - 1 - y) > growH) continue;
                const double sway = std::sin(static_cast<double>(y) / hh * 9.0 + seed) * w * 0.02 * t;
                const double halfW = (trunk * 0.5 + t * std::min(w, h) * 0.16 * std::sin(spread * 0.5 + 0.2));
                const double d = std::fabs(static_cast<double>(x) - cx - sway);
                RGBAf &px = img.at(x, y);
                if (d < trunk * 0.5) {
                    const double li = 1.0 + light * 0.4;
                    px.r = static_cast<float>(c01((px.r * 0.4 + 0.16) * li));
                    px.g = static_cast<float>(c01((px.g * 0.4 + 0.09) * li));
                    px.b = static_cast<float>(c01((px.b * 0.4 + 0.04) * li));
                } else if (d < halfW) {
                    const double n = fbm(static_cast<double>(x) / leafSize, static_cast<double>(y) / leafSize, 3, seed);
                    if (n < leaves) {
                        const double li = 1.0 + light * (n - 0.5);
                        px.r = static_cast<float>(c01((px.r * 0.5 + n * 0.15) * li));
                        px.g = static_cast<float>(c01((px.g * 0.5 + n * 0.35) * li));
                        px.b = static_cast<float>(c01((px.b * 0.5 + n * 0.12) * li));
                    }
                }
            }
        }
        return true;
    }
    return false;
}

}  // namespace pittore::filter
